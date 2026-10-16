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
    || (S5_R5_ELBOW_SUPPORT_MA < 1 || S5_R5_ELBOW_SUPPORT_MA > 20000)
#error "R5 dual FOLLOW requires an explicit 1..20000 mA elbow-current target"
#endif
/* Isolated dual candidate. The operator measured 0..110 degrees without
 * interference on each axis with power off, and will set both RAM zeroes at
 * the physical 0-degree marks. The -10..105-degree window includes the RAM
 * zero with mark tolerance; it is a bench anomaly boundary, not a mechanical
 * or human-use limit. The lower edge was -1 degree until 2026-10-16: lifting
 * the shoulder let the hanging forearm swing the relative elbow angle past
 * it, and the elbow reached about -5.5 degrees after release, so the 0-degree
 * mark is not the extension stop. Assistance only pushes flexion; -10 degrees
 * still catches a wrong zero or mapping. The 1.64 Nm shoulder model
 * is a partial-load estimate. The elbow target is build-selected independently
 * from its 20 A command cap; neither value is measured joint assistance.
 * Speeds (2026-10-16, unmeasured on this bench): ExoKit AmplifyReaction
 * withdraws torque above its speed limit and re-applies later, so the soft
 * limits pause assistance only. Hard limits that latch FAULT are elbow
 * 180 deg/s and shoulder 120 deg/s, about the reported ADL elbow ceiling
 * (~210 deg/s, arXiv 2408.14361); soft limits are half of that. The old
 * 20 deg/s elbow gate was below 4 C620 RPM. Shoulder standstill is only used
 * for the RAM zero and is now 0.5 deg / 3 deg/s / 100 ms at the joint.
 * 2026-10-16 user choice for a 30 s self-test: shoulder gain 0.36 -> 0.6 and
 * rotor cap 40 -> 80 mNm (0.64 Nm joint, ~13% of the 5 Nm rev2.2 rating);
 * elbow target is a build option (4 A chosen). Unmeasured on this bench.
 * Second user step after a weak-feeling 30 s window: gain 0.8, cap 120 mNm
 * (0.96 Nm joint, ~19% of rating), rise slew 20 -> 100 mNm/s because the old
 * slew left applied torque up to ~2.6 s behind the pose (later corrected:
 * R5 direct_target bypasses this slew while running).
 * Third user step, "assist, not full support": gain 1.0 (whole 1.64 Nm
 * partial-load model), cap 205 mNm = model peak (1.64 Nm joint, ~33% of
 * rating), release slew 80 -> 120 mNm/s so the 1.9 s ramp still covers the
 * cap. Elbow lower edge -10 -> -20 deg: the hanging forearm reached -8.9 deg. */
static const S5SiteProfile site = {
    .s3 = {
        .safety = {
            .joint[0] = {
                .mapping = {.rotor_per_joint = 8.0f, .rotor_zero_rad = 0.0f,
                            .direction = 1, .confirmed = true},
                .lower_rad = -0.17453293f, .upper_rad = 1.83259571f,
                .max_speed_rad_s = 2.09439510f,
                .max_motor_command = 0.205f, .motor_slew_per_s = 0.100f,
                .limits_confirmed = true, .feedback_confirmed = true,
                .protocol_confirmed = true
            },
            .joint[1] = {
                .mapping = {.rotor_per_joint = 1.0f, .rotor_zero_rad = 0.0f,
                            .direction = -1, .confirmed = true},
                .lower_rad = -0.34906585f, .upper_rad = 1.83259571f,
                .max_speed_rad_s = 3.14159265f,
                .max_motor_command = 20.0f, .motor_slew_per_s = 20.0f,
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
        .shoulder_property_window_ms = 55000U,
        .shoulder_standstill = {
            .span_rev = 0.011f, .max_velocity_rev_s = 0.066f,
            .dwell_ms = 100U, .freshness_ms = 150U
        },
        .shoulder_stop_window_ms = 1500U
    },
    .shoulder_gravity = {
        .amplitude_nm = 1.64f, .gravity_zero_rad = 0.0f,
        .sign_g = 1, .confirmed = true
    },
    .shoulder_gain = 1.0f,
    .elbow_support = {
        .current_at_zero_a = 0.001f * S5_R5_ELBOW_SUPPORT_MA
            * S5_R5_ELBOW_SUPPORT_SIGN,
        .slope_a_per_rad = 0.0f,
        .confirmed = true /* Bounded software candidate, not physical efficacy. */
    },
    .release_slew_per_s = {.shoulder_rotor_nm = 0.120f,
                           .elbow_current_a = 25.0f},
    .send_interval_ms = 50U,
    .session_ms = 30000U,
    .ramp_max_ms = 2000U,
    .shoulder_zero_dwell_ms = 1U,
    .soft_speed_rad_s = {1.04719755f, 1.57079633f}, /* 60 / 90 deg/s */
    .resume_quiet_ms = 100U, .resume_ramp_ms = 300U,
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

/* Soft speed pause is optional and dual-only; when present each axis must sit
 * strictly below its latched hard limit. */
static bool soft_speed_ready(const S5SiteProfile *p, bool dual)
{
    bool any = p->soft_speed_rad_s[0] != 0.0f || p->soft_speed_rad_s[1] != 0.0f;
    if (!any) { return p->resume_quiet_ms == 0U && p->resume_ramp_ms == 0U; }
    if (!dual || !duration(p->resume_quiet_ms) || !duration(p->resume_ramp_ms)) {
        return false;
    }
    for (unsigned axis = 0U; axis < SAFETY_JOINT_COUNT; axis++) {
        float soft = p->soft_speed_rad_s[axis];
        if (!isfinite(soft) || soft <= 0.0f
            || soft >= p->s3.safety.joint[axis].max_speed_rad_s) { return false; }
    }
    return true;
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
        || !p->elbow_support.confirmed
        || !soft_speed_ready(p, dual)) { return false; }
    const SafetyJointConfig *e = &s->safety.joint[1];
    if (e->mapping.rotor_per_joint != 1.0f || e->mapping.rotor_zero_rad != 0.0f
        || (e->mapping.direction != 1 && e->mapping.direction != -1)
        || (!dual && (e->lower_rad >= 0.0f || e->upper_rad <= 0.0f))
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
