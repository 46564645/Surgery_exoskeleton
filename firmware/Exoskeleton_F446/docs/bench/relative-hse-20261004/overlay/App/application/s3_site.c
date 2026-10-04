/* Isolated single supervised shoulder trial candidate, not production site.
 * User Sentinel_cf5158fc3be481919ba4701d4a6b19e2 accepted the described
 * 0.020 rotor Nm / 100 ms trial and reported support and about >=2 degrees
 * collision-free space on each side. Space is a user estimate, not measured
 * stopping distance. Native relative coordinates do not establish anatomy.
 * Startup is SAFE_OFF; no automatic read, preparation, Arm or request.
 */
#include "s3_site.h"
#include "s3_feedback.h"
#include "s2_config.h"
static const S3SiteProfile site = {
    .safety = {
        .joint = {{
            .mapping = { .rotor_per_joint = 1.0f, .rotor_zero_rad = 0.0f,
                         .direction = 1, .confirmed = true },
            .lower_rad = -0.06283185307179586f,
            .upper_rad = 0.06283185307179586f,
            .max_speed_rad_s = 0.6283185307179586f,
            .max_motor_command = 0.020f, .motor_slew_per_s = 0.20f,
            .limits_confirmed = false,
            .feedback_confirmed = true, .protocol_confirmed = true,
            .relative_native_envelope = true, .software_envelope_approved = true
        }},
        .feedback_timeout_ms = 150U, .service_limit_ms = 5U, .max_request_ms = 100U,
        .enabled_axes = SAFETY_SHOULDER_AXES,
        .protection_confirmed = true, .stop_method_confirmed = true,
        .timing_confirmed = true, /* Budgeted from actual 6 ms qualification;
                                   runtime freshness/service gates remain mandatory. */
        .stop_policy = SAFETY_STOP_SUPERVISED_SHOULDER_BENCH
    },
    .prepare_timeout_ms = 1500U,
    .shoulder_source = S3_SHOULDER_ENCODER_PROPERTIES,
    .shoulder_property_reply_ms = 20U, .shoulder_property_period_ms = 20U,
    .shoulder_property_window_ms = 2500U,
    .shoulder_standstill = { .span_rev = 0.001f, .max_velocity_rev_s = 0.02f,
                            .dwell_ms = 250U, .freshness_ms = 40U },
    .shoulder_stop_window_ms = 1000U,
    .shoulder_node = 1U, .elbow_id = S2_C620_ID,
    .trusted_feedback_axes = SAFETY_SHOULDER_AXES,
    .coordinates = { S3_BENCH_RELATIVE_ROTOR, S3_COORDINATE_UNSET },
    .rotor_reference_confirmed = { false, false }
    /* No physical absolute limits, anatomical zero or GPIO stop is invented. */
};
const S3SiteProfile *S3Site_GetProfile(void) { return &site; }
bool S3Site_ReadSample(uint32_t now_ms, S3SiteSample *out)
{ return S3Feedback_Read(&site, now_ms, out); }
