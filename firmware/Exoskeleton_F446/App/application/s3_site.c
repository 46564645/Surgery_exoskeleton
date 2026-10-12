/* Production profile remains disabled. An explicit isolated-build macro below
 * supplies only the bounded S5 R2 bench candidate. */
#include "s3_site.h"
#include "s3_feedback.h"
#include "s2_config.h"
#include <stddef.h>
#include <string.h>
#if defined(S5_R2_ELBOW_TRIAL)
/* Isolated S5 R2 direction/stop candidate. +1 means encoder-positive for this
 * first pulse, not a verified anatomical flexion sign. The ±5 degree range is
 * a RAM-session software window, not a measured mechanical end stop. This
 * branch is absent from ordinary WindowsDebug/production builds. */
static const S3SiteProfile site = {
    .safety = {
        .joint[1] = {
            .mapping = {.rotor_per_joint = 1.0f, .rotor_zero_rad = 0.0f,
                        .direction = 1, .confirmed = true},
            .lower_rad = -0.08726646f, .upper_rad = 0.08726646f,
            .max_speed_rad_s = 0.34906585f,
            .max_motor_command = 2.0f, .motor_slew_per_s = 20.0f,
            .limits_confirmed = true, .feedback_confirmed = true,
            .protocol_confirmed = true
        },
        .feedback_timeout_ms = 100U, .service_limit_ms = 20U,
        .max_request_ms = 200U, .enabled_axes = SAFETY_ELBOW_AXES,
        .protection_confirmed = true, .stop_method_confirmed = true,
        .timing_confirmed = true,
        .stop_policy = SAFETY_STOP_SUPERVISED_C620_BENCH
    },
    .prepare_timeout_ms = 2500U,
    .shoulder_node = S2_GDS_NODE, .elbow_id = S2_C620_ID,
    .trusted_feedback_axes = SAFETY_ELBOW_AXES,
    .coordinates[1] = S3_ASSEMBLED_JOINT,
    .operational_confirmed[1] = true
};
#else
static const S3SiteProfile site = {
    .shoulder_node = 1U, /* GDS USB readout: node1, FW0.6.5; compatibility pending. */
    .elbow_id = S2_C620_ID
    /* All safety confirmations, limits, durations and provider flags false/0. */
};
#endif
const S3SiteProfile *S3Site_GetProfile(void) { return &site; }
bool S3Site_ReadSample(uint32_t now_ms, S3SiteSample *out)
{
    return S3Feedback_Read(&site, now_ms, out);
}
