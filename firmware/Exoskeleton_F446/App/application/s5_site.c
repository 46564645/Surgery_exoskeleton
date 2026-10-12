#include "s5_site.h"
#include "s3_shoulder_service.h"
#include "s2_config.h"
#include "c620_standstill.h"
#include <math.h>
#include <stddef.h>

#if defined(S5_R5_DUAL_FOLLOW)
#if !defined(S5_R5_ELBOW_SUPPORT_SIGN) \
    || (S5_R5_ELBOW_SUPPORT_SIGN != 1 && S5_R5_ELBOW_SUPPORT_SIGN != -1)
#error "R5 dual FOLLOW requires an explicit +1 or -1 elbow-current sign"
#endif
#if !defined(S5_R5_ELBOW_SUPPORT_MA) \
    || (S5_R5_ELBOW_SUPPORT_MA < 1 || S5_R5_ELBOW_SUPPORT_MA > 2000)
#error "R5 dual FOLLOW requires an explicit 1..2000 mA elbow-current magnitude"
#endif
/* Isolated low-pose dual candidate. Shoulder values match the S4 40 deg/s
 * bench image; its 1.64 Nm model is a partial-load estimate. The elbow uses
 * a bounded current command, not a joint-torque or 3D gravity model. Elbow
 * mapping +1 denotes encoder-positive; support-current sign must be chosen
 * from the separate physical direction observation. Both zeroes are RAM-only. */
static const S5SiteProfile site = {
    .s3 = {
        .safety = {
            .joint[0] = {
                .mapping = {.rotor_per_joint = 8.0f, .rotor_zero_rad = 0.0f,
                            .direction = 1, .confirmed = true},
                .lower_rad = 0.34906585f, .upper_rad = 0.52359878f,
                .max_speed_rad_s = 0.69813170f,
                .max_motor_command = 0.040f, .motor_slew_per_s = 0.020f,
                .limits_confirmed = true, .feedback_confirmed = true,
                .protocol_confirmed = true
            },
            .joint[1] = {
                .mapping = {.rotor_per_joint = 1.0f, .rotor_zero_rad = 0.0f,
                            .direction = 1, .confirmed = true},
                .lower_rad = -0.08726646f, .upper_rad = 0.08726646f,
                .max_speed_rad_s = 0.34906585f,
                .max_motor_command = 2.0f, .motor_slew_per_s = 20.0f,
                .limits_confirmed = true, .feedback_confirmed = true,
                .protocol_confirmed = true
            },
            .feedback_timeout_ms = 150U, .service_limit_ms = 100U,
            .max_request_ms = 250U, .enabled_axes = SAFETY_ALL_AXES,
            .protection_confirmed = true, .stop_method_confirmed = true,
            .timing_confirmed = true,
            .stop_policy = SAFETY_STOP_SUPERVISED_DUAL_FOLLOW
        },
        .prepare_timeout_ms = 20000U,
        .shoulder_node = S2_GDS_NODE, .elbow_id = S2_C620_ID,
        .trusted_feedback_axes = SAFETY_ALL_AXES,
        .coordinates = {S3_ASSEMBLED_JOINT, S3_ASSEMBLED_JOINT},
        .operational_confirmed[1] = true,
        .shoulder_source = S3_SHOULDER_ENCODER_PROPERTIES,
        .shoulder_property_reply_ms = 20U,
        .shoulder_property_period_ms = 20U,
        .shoulder_property_window_ms = 35000U,
        .shoulder_standstill = {
            .span_rev = 0.001f, .max_velocity_rev_s = 0.02f,
            .dwell_ms = 200U, .freshness_ms = 150U
        },
        .shoulder_stop_window_ms = 1500U
    },
    .shoulder_gravity = {
        .amplitude_nm = 1.64f, .gravity_zero_rad = 0.0f,
        .sign_g = 1, .confirmed = true
    },
    .shoulder_gain = 0.36f,
    .elbow_support = {
        .current_at_zero_a = 0.001f * S5_R5_ELBOW_SUPPORT_MA
            * S5_R5_ELBOW_SUPPORT_SIGN,
        .slope_a_per_rad = 0.0f,
        .confirmed = true /* Bounded software candidate, not physical efficacy. */
    },
    .release_slew_per_s = {.shoulder_rotor_nm = 0.080f,
                           .elbow_current_a = 20.0f},
    .send_interval_ms = 50U,
    .session_ms = 10000U,
    .ramp_max_ms = 1000U,
    .shoulder_zero_dwell_ms = 20U,
    .output_enabled = true
};
#elif defined(S5_R4_ELBOW_FOLLOW)
#if !defined(S5_R4_ELBOW_SUPPORT_SIGN) \
    || (S5_R4_ELBOW_SUPPORT_SIGN != 1 && S5_R4_ELBOW_SUPPORT_SIGN != -1)
#error "R4 elbow FOLLOW requires an explicit +1 or -1 support-current sign"
#endif
#if !defined(S5_R4_ELBOW_SUPPORT_MA) \
    || (S5_R4_ELBOW_SUPPORT_MA < 1 || S5_R4_ELBOW_SUPPORT_MA > 4000)
#error "R4 elbow FOLLOW requires an explicit 1..4000 mA support-current magnitude"
#endif
/* Isolated, bounded software candidate only. The sign must be chosen after
 * observing the R2 physical direction; the current is not a measured support fit.
 * The window is relative to this session's RAM zero, not a mechanical stop. */
static const S5SiteProfile site = {
    .s3 = {
        .safety = {
            .joint[1] = {
                .mapping = {.rotor_per_joint = 1.0f, .rotor_zero_rad = 0.0f,
                            .direction = 1, .confirmed = true},
                .lower_rad = -0.08726646f, .upper_rad = 0.08726646f,
                .max_speed_rad_s = 0.34906585f,
                .max_motor_command = 0.001f * S5_R4_ELBOW_SUPPORT_MA,
                .motor_slew_per_s = 20.0f,
                .limits_confirmed = true, .feedback_confirmed = true,
                .protocol_confirmed = true
            },
            .feedback_timeout_ms = 100U, .service_limit_ms = 20U,
            .max_request_ms = 200U, .enabled_axes = SAFETY_ELBOW_AXES,
            .protection_confirmed = true, .stop_method_confirmed = true,
            .timing_confirmed = true,
            .stop_policy = SAFETY_STOP_SUPERVISED_ELBOW_FOLLOW
        },
        .prepare_timeout_ms = 2500U,
        .shoulder_node = S2_GDS_NODE, .elbow_id = S2_C620_ID,
        .trusted_feedback_axes = SAFETY_ELBOW_AXES,
        .coordinates[1] = S3_ASSEMBLED_JOINT,
        .operational_confirmed[1] = true
    },
    .elbow_support = {
        .current_at_zero_a = 0.001f * S5_R4_ELBOW_SUPPORT_MA
            * S5_R4_ELBOW_SUPPORT_SIGN,
        .slope_a_per_rad = 0.0f,
        .confirmed = true /* Bounded software profile, not physical efficacy. */
    },
    .release_slew_per_s.elbow_current_a = 20.0f,
    .send_interval_ms = 10U,
    .session_ms = 5000U,
    .ramp_max_ms = 250U,
    .output_enabled = true
};
#else
static const S5SiteProfile site = {
    .s3.safety.enabled_axes = 0U,
    .s3.shoulder_node = S2_GDS_NODE,
    .s3.elbow_id = S2_C620_ID,
    .output_enabled = false
};
#endif

#ifdef S5_SITE_HOST_TEST
static const S5SiteProfile *test_profile;
void S5Site_TestUseProfile(const S5SiteProfile *profile) { test_profile = profile; }
const S5SiteProfile *S5Site_GetProfile(void)
{ return test_profile == NULL ? &site : test_profile; }
#else
const S5SiteProfile *S5Site_GetProfile(void) { return &site; }
#endif

static bool duration(uint32_t ms)
{ return ms > 0U && ms < UINT32_C(0x80000000); }

static bool shoulder_peak_within_limit(const S5SiteProfile *p)
{
    const SafetyJointConfig *sh = &p->s3.safety.joint[0];
    const GravityCompensationConfig *g = &p->shoulder_gravity;
    double lower = (double)sh->lower_rad - (double)g->gravity_zero_rad;
    double upper = (double)sh->upper_rad - (double)g->gravity_zero_rad;
    double peak_fraction = 1.0;
    const double half_pi = 1.57079632679489661923;
    /* In a one-sided monotonic sine sector, the endpoint is the peak.
     * Other sectors retain the conservative full-wave bound. */
    if (isfinite(lower) && isfinite(upper) && lower >= 0.0 && upper <= half_pi) {
        peak_fraction = sin(upper);
    } else if (isfinite(lower) && isfinite(upper)
               && lower >= -half_pi && upper <= 0.0) {
        peak_fraction = -sin(lower);
    }
    double peak_motor_nm = (double)g->amplitude_nm * (double)p->shoulder_gain
        * peak_fraction / (double)sh->mapping.rotor_per_joint;
    return isfinite(peak_motor_nm)
        && peak_motor_nm <= (double)sh->max_motor_command;
}

static bool base_ready(const S5SiteProfile *p)
{
    if (p == NULL || !p->output_enabled) { return false; }
    const S3SiteProfile *s = &p->s3;
    uint8_t axes = s->safety.enabled_axes;
    bool dual = axes == SAFETY_ALL_AXES;
    if (axes != SAFETY_ELBOW_AXES && !dual) { return false; }
    if (s->safety.stop_policy != (dual ? SAFETY_STOP_SUPERVISED_DUAL_FOLLOW
                                      : SAFETY_STOP_SUPERVISED_ELBOW_FOLLOW)
        || !SafetySupervisor_ConfigReady(&s->safety)
        || (s->trusted_feedback_axes & axes) != axes
        || s->elbow_id != S2_C620_ID || !s->operational_confirmed[1]
        || s->coordinates[1] != S3_ASSEMBLED_JOINT
        || !duration(s->prepare_timeout_ms)
        || s->prepare_timeout_ms <= C620_STILL_WINDOW_MS
        || !duration(p->send_interval_ms) || !duration(p->session_ms)
        || !duration(p->ramp_max_ms) || p->session_ms <= p->ramp_max_ms
        || p->send_interval_ms >= s->safety.service_limit_ms
        || p->send_interval_ms >= s->safety.max_request_ms
        || !isfinite(p->release_slew_per_s.elbow_current_a)
        || p->release_slew_per_s.elbow_current_a <= 0.0f
        || !p->elbow_support.confirmed) { return false; }
    const SafetyJointConfig *e = &s->safety.joint[1];
    if (e->mapping.rotor_per_joint != 1.0f || e->mapping.rotor_zero_rad != 0.0f
        || (e->mapping.direction != 1 && e->mapping.direction != -1)
        || e->lower_rad >= 0.0f || e->upper_rad <= 0.0f
        || e->lower_rad <= -3.14159265f || e->upper_rad >= 3.14159265f
        || (double)e->max_speed_rad_s * s->safety.feedback_timeout_ms / 1000.0
            >= 3.14159265) { return false; }
    JointState edge = {e->lower_rad, 0.0f, 0U, true};
    float low_a, high_a;
    if (!S5ElbowSupport_Compute(&p->elbow_support, &edge, &low_a)) { return false; }
    edge.position_rad = e->upper_rad;
    if (!S5ElbowSupport_Compute(&p->elbow_support, &edge, &high_a)
        || fabsf(low_a) > e->max_motor_command
        || fabsf(high_a) > e->max_motor_command) { return false; }
    if (!dual) { return true; }
    const SafetyJointConfig *sh = &s->safety.joint[0];
    const GravityCompensationConfig *g = &p->shoulder_gravity;
    return S3ShoulderService_ReadProfileReady(s)
        && s->coordinates[0] == S3_ASSEMBLED_JOINT
        && isfinite(sh->mapping.rotor_zero_rad)
        && isfinite(sh->mapping.rotor_per_joint) && sh->mapping.rotor_per_joint > 0.0f
        && g->confirmed && g->gravity_zero_rad == 0.0f
        && isfinite(g->amplitude_nm) && g->amplitude_nm > 0.0f
        && (g->sign_g == 1 || g->sign_g == -1)
        && isfinite(p->shoulder_gain) && p->shoulder_gain > 0.0f
        && p->shoulder_gain <= 1.0f
        && shoulder_peak_within_limit(p)
        && isfinite(p->release_slew_per_s.shoulder_rotor_nm)
        && p->release_slew_per_s.shoulder_rotor_nm > 0.0f
        && ShoulderStandstill_ConfigValid(&s->shoulder_standstill)
        && s->shoulder_standstill.freshness_ms <= s->safety.feedback_timeout_ms
        && s->shoulder_property_period_ms < s->shoulder_standstill.freshness_ms
        && duration(p->shoulder_zero_dwell_ms)
        && p->shoulder_zero_dwell_ms < s->safety.feedback_timeout_ms
        && duration(s->shoulder_stop_window_ms)
        && (uint64_t)s->shoulder_property_window_ms
            > (uint64_t)s->prepare_timeout_ms + p->session_ms;
}

bool S5Site_ReadReady(const S5SiteProfile *p)
{
    return base_ready(p) && !p->s3.rotor_reference_confirmed[1]
        && (!SafetySupervisor_AxisEnabled(&p->s3.safety, 0U)
            || (!p->s3.rotor_reference_confirmed[0]
                && p->s3.safety.joint[0].mapping.rotor_zero_rad == 0.0f));
}

bool S5Site_ProfileReady(const S5SiteProfile *p)
{
    return base_ready(p) && p->s3.rotor_reference_confirmed[1]
        && (!SafetySupervisor_AxisEnabled(&p->s3.safety, 0U)
            || p->s3.rotor_reference_confirmed[0]);
}
