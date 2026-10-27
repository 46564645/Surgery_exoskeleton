#include "s5_site.h"
#include "s3_shoulder_service.h"
#include "s2_config.h"
#include "c620_standstill.h"
#include <math.h>
#include <stddef.h>

#if defined(S5_R5_DUAL_FOLLOW)
#define S5_ELBOW_MOTOR_KT_NM_PER_A 0.020f
#if !defined(S5_R5_SHOULDER_GAIN)
#define S5_R5_SHOULDER_GAIN 1.5f
#endif
/* Rotor-torque cap for the shoulder channel. 205 mNm (2026-10-16) covers the
 * exoskeleton's own weight; larger values ask the GDS68 to carry part of the
 * human arm too (575 mNm = 4.6 N*m joint-side, the old-24V rating / 8). */
#if !defined(S5_R5_SHOULDER_CAP_MNM)
#define S5_R5_SHOULDER_CAP_MNM 300
#endif
#if S5_R5_SHOULDER_CAP_MNM < 1 || S5_R5_SHOULDER_CAP_MNM > 575
#error "R5 shoulder rotor cap must be 1..575 mNm"
#endif
#if !defined(S5_R5_FORCE_BOOST_PCT)
#define S5_R5_FORCE_BOOST_PCT 50
#endif
#if !defined(S5_R5_ELBOW_SUPPORT_SIGN) \
    || (S5_R5_ELBOW_SUPPORT_SIGN != 1 && S5_R5_ELBOW_SUPPORT_SIGN != -1)
#error "R5 dual FOLLOW requires an explicit +1 or -1 elbow-current sign"
#endif
#if !defined(S5_R5_ELBOW_SUPPORT_MA) \
    || (S5_R5_ELBOW_SUPPORT_MA < 1 || S5_R5_ELBOW_SUPPORT_MA > 20000)
#error "R5 dual FOLLOW requires an explicit 1..20000 mA elbow-current peak"
#endif
#if !defined(S5_R5_SHOULDER_AMPLIFY_MNM)
#define S5_R5_SHOULDER_AMPLIFY_MNM 0
#endif
#if S5_R5_SHOULDER_AMPLIFY_MNM < 0 || S5_R5_SHOULDER_AMPLIFY_MNM > 100
#error "R5 shoulder Amplify increment must be 0..100 mNm (0 = off)"
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
 * cap. Elbow lower edge -10 -> -20 deg: the hanging forearm reached -8.9 deg.
 * Shoulder Amplify (2026-10-20, optional S7, unmeasured): ExoKit
 * AmplifyReaction defaults are startingVelocity 30 deg/s and a 400 ms
 * direction-change suspension; 20 deg/s here so slow raises engage. The
 * build-selected increment is added to the gravity target before the
 * unchanged 205 mNm cap, so high poses leave little room for it. */
static const S5SiteProfile site = {
    .s3 = {
        .safety = {
            .joint[0] = {
                .mapping = {.rotor_per_joint = 8.0f, .rotor_zero_rad = 0.0f,
                            .direction = 1, .confirmed = true},
                .lower_rad = -0.34906585f, .upper_rad = 1.83259571f,
                .max_speed_rad_s = 2.61799388f,
                .max_motor_command = 0.001f * S5_R5_SHOULDER_CAP_MNM,
                .motor_slew_per_s = 0.100f,
                .limits_confirmed = true, .feedback_confirmed = true,
                .protocol_confirmed = true
            },
            .joint[1] = {
                .mapping = {.rotor_per_joint = 1.0f, .rotor_zero_rad = 0.0f,
                            .direction = -1, .confirmed = true},
                .lower_rad = -0.52359878f, .upper_rad = 1.83259571f,
                .max_speed_rad_s = 6.28318531f,
                .max_motor_command = 20.0f, .motor_slew_per_s = 20.0f,
                .limits_confirmed = true, .feedback_confirmed = true,
                .protocol_confirmed = true
            },
            .feedback_timeout_ms = 250U, .service_limit_ms = 100U,
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
        .shoulder_property_reply_ms = 100U,
        .shoulder_property_period_ms = 50U,
        .shoulder_property_window_ms = 70000U,
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
    .shoulder_gain = S5_R5_SHOULDER_GAIN,
    .shoulder_amplify = {
        .increment_rotor_nm = 0.001f * S5_R5_SHOULDER_AMPLIFY_MNM,
        .starting_velocity_rad_s = 0.34906585f, /* 20 deg/s */
        .suspension_ms = 400U, .ramp_ms = 200U
    },
    .elbow_gravity = {
        .amplitude_nm = 0.001f * S5_R5_ELBOW_SUPPORT_MA * S5_ELBOW_MOTOR_KT_NM_PER_A,
        .gravity_zero_rad = 0.0f,
        .sign_g = S5_R5_ELBOW_SUPPORT_SIGN,
        .confirmed = true /* Bounded software candidate, not physical efficacy. */
    },
    .elbow_motor_kt_nm_per_a = S5_ELBOW_MOTOR_KT_NM_PER_A,
    .release_slew_per_s = {.shoulder_rotor_nm = 0.120f,
                           .elbow_current_a = 25.0f},
    /* 2026-10-27 user decision: 50 -> 10 ms (100 Hz). The simulation showed
     * the assist is frozen during a 180-250 ms burst at 50 ms; the running
     * target bypasses the slew (direct_target), so the update period is the
     * dominant latency. Still below service_limit_ms (100) and max_request_ms
     * (250), and the gateway's ramp budget checks hold. Not bench-verified. */
    .send_interval_ms = 10U,
    .session_ms = 45000U,
    .ramp_max_ms = 2000U,
    .shoulder_zero_dwell_ms = 1U,
    .soft_speed_rad_s = {1.57079633f, 3.49065850f}, /* 90 / 200 deg/s */
    .resume_quiet_ms = 250U, .resume_ramp_ms = 300U,
    /* Interaction-force trigger. Coefficients are bench-calibrated values;
     * confirmed=false keeps the feature off until a calibration run fills
     * S5_FORCE_C0..C2 (see tests/live_monitor/force_trigger_sim.py). */
    .force_assist = {
        /* Bench fit 2026-10-22 (worn, relaxed sweep, residual 0.104 N).
         * c1 stays zero: the link only follows the forearm angle. */
        .c0 = 0.0593f, .c1 = 0.0f, .c2 = 0.3995f,
        /* Bench 2026-10-22: |e_F| p90 is 0.21-0.25 N, so 0.5 N never fired. */
        .on_n = 0.30f, .off_n = 0.15f,
        .boost_pct = 0.001f * S5_R5_FORCE_BOOST_PCT,
        .ramp_ms = 200U, .max_hold_ms = 3000U,
        /* 10 SPS: one sample per 100 ms, so allow three sample periods. */
        .freshness_ms = 300U,
#if defined(S5_R5_FORCE_ASSIST)
        .confirmed = true
#else
        .confirmed = false
#endif
    },
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
        || !soft_speed_ready(p, dual)
        || (p->force_assist.confirmed && !ForceAssist_ConfigValid(&p->force_assist))) {
        return false;
    }
    const SafetyJointConfig *e = &s->safety.joint[1];
    if (e->mapping.rotor_per_joint != 1.0f || e->mapping.rotor_zero_rad != 0.0f
        || (e->mapping.direction != 1 && e->mapping.direction != -1)
        || (!dual && (e->lower_rad >= 0.0f || e->upper_rad <= 0.0f))
        || e->lower_rad <= -3.14159265f || e->upper_rad >= 3.14159265f
        || (double)e->max_speed_rad_s * s->safety.feedback_timeout_ms / 1000.0
            >= 3.14159265) { return false; }
    if (!dual) {
        JointState edge = {e->lower_rad, 0.0f, 0U, true};
        float low_a, high_a;
        if (!S5ElbowSupport_Compute(&p->elbow_support, &edge, &low_a)) { return false; }
        edge.position_rad = e->upper_rad;
        return S5ElbowSupport_Compute(&p->elbow_support, &edge, &high_a)
            && fabsf(low_a) <= e->max_motor_command
            && fabsf(high_a) <= e->max_motor_command;
    }
    const JointState down = {0.0f, 0.0f, 0U, true};
    const JointState horizontal = {1.57079633f, 0.0f, 0U, true};
    float peak_nm;
    if (!isfinite(p->elbow_motor_kt_nm_per_a) || p->elbow_motor_kt_nm_per_a <= 0.0f
        || p->elbow_gravity.gravity_zero_rad != 0.0f
        || !S5ElbowGravity_Compute(&p->elbow_gravity, &down, &horizontal, &peak_nm)
        || fabsf(peak_nm) / p->elbow_motor_kt_nm_per_a
            > e->max_motor_command) { return false; }
    const SafetyJointConfig *sh = &s->safety.joint[0];
    const GravityCompensationConfig *g = &p->shoulder_gravity;
    const ShoulderAmplifyConfig *amp = &p->shoulder_amplify;
    if (!ShoulderAmplify_ConfigValid(amp) || amp->increment_rotor_nm > sh->max_motor_command
        || (amp->increment_rotor_nm > 0.0f && p->soft_speed_rad_s[0] > 0.0f
            && amp->starting_velocity_rad_s >= p->soft_speed_rad_s[0])) { return false; }
    return S3ShoulderService_ReadProfileReady(s)
        && s->coordinates[0] == S3_ASSEMBLED_JOINT
        && isfinite(sh->mapping.rotor_zero_rad)
        && isfinite(sh->mapping.rotor_per_joint) && sh->mapping.rotor_per_joint > 0.0f
        && g->confirmed && g->gravity_zero_rad == 0.0f
        && isfinite(g->amplitude_nm) && g->amplitude_nm > 0.0f
        && (g->sign_g == 1 || g->sign_g == -1)
        && isfinite(p->shoulder_gain) && p->shoulder_gain > 0.0f
        && p->shoulder_gain <= 3.0f
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
