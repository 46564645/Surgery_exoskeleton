#include "sim_bridge.h"
#include "s5_assist_compose.h"
#include "s5_site.h"
#include <stddef.h>

static const S5SiteProfile *site;
static ShoulderAmplify amplify;
static ForceAssistState force_state;

static bool dual(void)
{ return site != NULL && site->s3.safety.enabled_axes == SAFETY_ALL_AXES; }

SIM_EXPORT void SimBridge_Reset(uint32_t now_ms)
{
    site = S5Site_GetProfile();
    ShoulderAmplify_Reset(&amplify, now_ms);
    ForceAssist_Reset(&force_state, now_ms);
}

SIM_EXPORT int32_t SimBridge_Step(const SimBridgeInput *in, SimBridgeOutput *out)
{
    if (in == NULL || out == NULL) { return 0; }
    *out = (SimBridgeOutput){0};
    if (site == NULL) { SimBridge_Reset(in->now_ms); }
    /* The simulation feeds joint-side radians; the mapping to rotor counts and
     * the output caps stay in the firmware profile below. */
    const JointState shoulder = {in->shoulder_pos_rad, in->shoulder_vel_rad_s,
                                 in->now_ms, in->shoulder_valid != 0};
    const JointState elbow = {in->elbow_pos_rad, in->elbow_vel_rad_s,
                              in->now_ms, in->elbow_valid != 0};
    const S5AssistComposeConfig config = {
        .dual = dual(),
        .shoulder_gravity = &site->shoulder_gravity,
        .elbow_gravity = &site->elbow_gravity,
        .elbow_support = &site->elbow_support,
        .shoulder_amplify = &site->shoulder_amplify,
        .force_assist = &site->force_assist,
        .shoulder_gain = site->shoulder_gain,
        .elbow_motor_kt_nm_per_a = site->elbow_motor_kt_nm_per_a,
        .shoulder_rotor_per_joint = site->s3.safety.joint[0].mapping.rotor_per_joint,
        .shoulder_rotor_cap_nm = site->s3.safety.joint[0].max_motor_command,
        .elbow_current_cap_a = site->s3.safety.joint[1].max_motor_command
    };
    S5AssistComposeOutput command;
    if (!S5AssistCompose_Update(&config, &shoulder, &elbow,
                                in->assist_enabled != 0, in->force_valid != 0,
                                in->force_n, &amplify, &force_state,
                                in->now_ms, &command)) { return 0; }
    out->shoulder_rotor_nm = command.shoulder_rotor_nm;
    out->elbow_current_a = command.elbow_current_a;
    out->ok = 1;
    return 1;
}

SIM_EXPORT void SimBridge_GetSite(SimBridgeSite *out)
{
    if (out == NULL) { return; }
    if (site == NULL) { site = S5Site_GetProfile(); }
    const S3SiteProfile *s3 = &site->s3;
    *out = (SimBridgeSite){
        .shoulder_rotor_per_joint = s3->safety.joint[0].mapping.rotor_per_joint,
        .shoulder_rotor_zero_rad = s3->safety.joint[0].mapping.rotor_zero_rad,
        .shoulder_direction = s3->safety.joint[0].mapping.direction,
        .elbow_rotor_per_joint = s3->safety.joint[1].mapping.rotor_per_joint,
        .elbow_rotor_zero_rad = s3->safety.joint[1].mapping.rotor_zero_rad,
        .elbow_direction = s3->safety.joint[1].mapping.direction,
        .elbow_motor_kt_nm_per_a = site->elbow_motor_kt_nm_per_a,
        .shoulder_gain = site->shoulder_gain,
        .shoulder_gravity_amplitude_nm = site->shoulder_gravity.amplitude_nm,
        .elbow_gravity_amplitude_nm = site->elbow_gravity.amplitude_nm,
        .shoulder_cap_rotor_nm = s3->safety.joint[0].max_motor_command,
        .elbow_cap_a = s3->safety.joint[1].max_motor_command,
        .soft_speed_rad_s = {site->soft_speed_rad_s[0], site->soft_speed_rad_s[1]},
        .amplify_increment_rotor_nm = site->shoulder_amplify.increment_rotor_nm,
        .send_interval_ms = site->send_interval_ms,
        .session_ms = site->session_ms,
        .feedback_timeout_ms = s3->safety.feedback_timeout_ms,
        .shoulder_property_period_ms = s3->shoulder_property_period_ms,
        .force_freshness_ms = site->force_assist.freshness_ms,
        .dual = dual() ? 1 : 0,
        .force_confirmed = site->force_assist.confirmed ? 1 : 0
    };
}

SIM_EXPORT void SimBridge_Sizes(int32_t *input_size, int32_t *output_size,
                                int32_t *site_size)
{
    if (input_size != NULL) { *input_size = (int32_t)sizeof(SimBridgeInput); }
    if (output_size != NULL) { *output_size = (int32_t)sizeof(SimBridgeOutput); }
    if (site_size != NULL) { *site_size = (int32_t)sizeof(SimBridgeSite); }
}
