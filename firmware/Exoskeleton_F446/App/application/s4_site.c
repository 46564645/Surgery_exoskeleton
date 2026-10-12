#include "s4_site.h"
#include "s3_shoulder_service.h"
#include "s2_config.h"
#include <math.h>
#include <stddef.h>

static const S4SiteProfile site = {
    .s3.safety.enabled_axes = 0U,
    .s3.safety.feedback_timeout_ms = 150U,
    .s3.shoulder_source = S3_SHOULDER_ENCODER_PROPERTIES,
    .s3.shoulder_property_reply_ms = 20U,
    .s3.shoulder_property_period_ms = 20U,
    .s3.shoulder_property_window_ms = 30000U,
    .s3.shoulder_node = S2_GDS_NODE,
    .s3.elbow_id = S2_C620_ID,
    .output_enabled = false,
    .session_zero_on_prepare = false
    /* Read timing follows the previous HSE seven-property bench profile;
     * this one bounded read-only session has a 30 s ceiling. Mapping,
     * model, motion limits and output confirmations remain false/zero. */
};

static bool duration(uint32_t ms)
{ return ms > 0U && ms < UINT32_C(0x80000000); }

#ifdef S4_SITE_HOST_TEST
static bool test_use_production;
static const S4SiteProfile test_s3_legacy = {0};
void S4Site_TestUseProduction(bool enabled) { test_use_production = enabled; }
const S4SiteProfile *S4Site_GetProfile(void)
{ return test_use_production ? &site : &test_s3_legacy; }
#else
const S4SiteProfile *S4Site_GetProfile(void) { return &site; }
#endif

bool S4Site_ReadReady(const S4SiteProfile *p)
{
    return p != NULL && S3ShoulderService_ReadProfileReady(&p->s3);
}

bool S4Site_ObservationReady(const S4SiteProfile *p)
{
    if (!S4Site_ReadReady(p)) { return false; }
    const S3SiteProfile *s = &p->s3;
    const JointMapping *m = &s->safety.joint[0].mapping;
    const GravityCompensationConfig *g = &p->gravity;
    return s->coordinates[0] == S3_ASSEMBLED_JOINT
        && s->rotor_reference_confirmed[0]
        && (s->trusted_feedback_axes & SAFETY_SHOULDER_AXES) != 0U
        && s->safety.joint[0].feedback_confirmed
        && s->safety.joint[0].protocol_confirmed
        && m->confirmed && (m->direction == 1 || m->direction == -1)
        && isfinite(m->rotor_per_joint) && m->rotor_per_joint > 0.0f
        && isfinite(m->rotor_zero_rad)
        && g->confirmed && isfinite(g->amplitude_nm) && g->amplitude_nm > 0.0f
        && isfinite(g->gravity_zero_rad) && (g->sign_g == 1 || g->sign_g == -1);
}

bool S4Site_ProfileReady(const S4SiteProfile *p)
{
    if (!S4Site_ObservationReady(p) || !p->output_enabled
        || p->s3.safety.enabled_axes != SAFETY_SHOULDER_AXES
        || !SafetySupervisor_ConfigReady(&p->s3.safety)
        || !isfinite(p->assist_gain) || p->assist_gain <= 0.0f || p->assist_gain > 1.0f
        || !isfinite(p->release_slew_nm_s) || p->release_slew_nm_s <= 0.0f
        || !duration(p->send_interval_ms) || !duration(p->session_ms)
        || !duration(p->ramp_max_ms) || !duration(p->stop_submit_budget_ms)
        /* IDLE cannot be submitted until the preceding zero frame completes.
         * Its configured wait may consume one full service deadline. */
        || p->stop_submit_budget_ms < p->s3.safety.service_limit_ms
        /* Stop confirmation may take longer than one feedback lifetime. The
         * two session-zero cycles have their own minimum and must both fit. */
        || (p->session_zero_on_prepare
            && (!duration(p->session_zero_dwell_ms)
                || p->session_zero_dwell_ms >= p->s3.safety.feedback_timeout_ms))
        || p->send_interval_ms >= p->s3.safety.service_limit_ms
        || p->send_interval_ms >= p->s3.safety.max_request_ms
        || !duration(p->s3.prepare_timeout_ms)
        || !duration(p->s3.shoulder_stop_window_ms)
        || !ShoulderStandstill_ConfigValid(&p->s3.shoulder_standstill)
        || p->s3.shoulder_standstill.freshness_ms > p->s3.safety.feedback_timeout_ms
        || p->s3.shoulder_property_period_ms >= p->s3.shoulder_standstill.freshness_ms
        || (uint64_t)p->s3.shoulder_standstill.freshness_ms
            <= (uint64_t)GDS_PROPERTY_COUNT * p->s3.shoulder_property_reply_ms
        || (uint64_t)p->s3.safety.max_request_ms
            <= (uint64_t)GDS_PROPERTY_COUNT * p->s3.shoulder_property_reply_ms
                + p->send_interval_ms
        || p->session_ms <= p->ramp_max_ms
        || (uint64_t)p->s3.shoulder_property_window_ms
            <= (uint64_t)p->s3.prepare_timeout_ms + p->session_ms) { return false; }
    /* The gateway faults rather than clips an oversized FOLLOW candidate.
     * Reject a model/gain combination whose worst-case rotor command can
     * exceed the independently confirmed motor-side limit. */
    double peak_motor_nm = (double)p->gravity.amplitude_nm * (double)p->assist_gain
        / (double)p->s3.safety.joint[0].mapping.rotor_per_joint;
    if (!isfinite(peak_motor_nm)
        || peak_motor_nm > (double)p->s3.safety.joint[0].max_motor_command) { return false; }
    /* After IDLE, an in-flight cycle may finish before the next complete
     * seven-property cycle. This is a minimum budget, not proof that replies
     * or a new IDLE heartbeat will arrive; the live tail still times out. */
    uint64_t tail_min_ms = 2U * (uint64_t)GDS_PROPERTY_COUNT
        * p->s3.shoulder_property_reply_ms
        + p->s3.shoulder_property_period_ms + p->s3.safety.service_limit_ms
        + p->s3.shoulder_standstill.dwell_ms;
    if ((uint64_t)p->s3.shoulder_stop_window_ms <= tail_min_ms) { return false; }
    /* Dedicated S4 bench policy records an observed stationary shoulder, not
     * an independently proven physical stop. Its confirmation is site-specific
     * and remains false in the production profile until bench evidence exists. */
    if (p->s3.safety.stop_policy != SAFETY_STOP_SUPERVISED_SHOULDER_FOLLOW) { return false; }
    uint64_t fixed_ms = 2U * (uint64_t)p->send_interval_ms + p->stop_submit_budget_ms;
    if (p->ramp_max_ms <= fixed_ms) { return false; }
    uint64_t steps = (p->ramp_max_ms - fixed_ms) / p->send_interval_ms;
    double removable_nm = (double)steps * p->send_interval_ms
        * (double)p->release_slew_nm_s / 1000.0;
    return removable_nm >= (double)p->s3.safety.joint[0].max_motor_command;
}

bool S4Site_SessionZeroReady(const S4SiteProfile *p)
{
    if (p == NULL || !p->session_zero_on_prepare || !p->output_enabled
        || p->s3.rotor_reference_confirmed[0]
        || p->s3.safety.joint[0].mapping.rotor_zero_rad != 0.0f
        || p->gravity.gravity_zero_rad != 0.0f) { return false; }
    /* Mapping sign/ratio, model amplitude/sign, physical envelope and every
     * other protection must already be independently confirmed. Only this
     * session's absolute rotor origin is supplied by the fresh reader. */
    S4SiteProfile after_zero = *p;
    after_zero.s3.rotor_reference_confirmed[0] = true;
    return S4Site_ProfileReady(&after_zero);
}
