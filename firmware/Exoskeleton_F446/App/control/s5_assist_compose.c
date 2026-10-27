#include "s5_assist_compose.h"
#include <math.h>
#include <stddef.h>

bool S5AssistCompose_Update(const S5AssistComposeConfig *config,
                            const JointState *shoulder, const JointState *elbow,
                            bool assist_enabled, bool force_valid, float force_n,
                            ShoulderAmplify *amplify, ShoulderAmplify *elbow_amplify,
                            ForceAssistState *force,
                            uint32_t now_ms, S5AssistComposeOutput *out)
{
    if (config == NULL || shoulder == NULL || elbow == NULL || out == NULL) { return false; }
    *out = (S5AssistComposeOutput){0};
    if (config->dual) {
        if (config->shoulder_gravity == NULL || config->elbow_gravity == NULL
            || config->shoulder_amplify == NULL || config->elbow_amplify == NULL
            || config->force_assist == NULL
            || amplify == NULL || elbow_amplify == NULL || force == NULL) { return false; }
        float joint_nm;
        if (!S5ElbowGravity_Compute(config->elbow_gravity, shoulder, elbow, &joint_nm)) {
            return false;
        }
        out->elbow_current_a = joint_nm / config->elbow_motor_kt_nm_per_a;
        /* Elbow Amplify: magnitude from |velocity|, sign from the movement, so
         * both a fast extension (strike) and a fast flexion get the increment. */
        const float elbow_v = elbow->velocity_rad_s;
        const float elbow_boost = ShoulderAmplify_Step(elbow_amplify, config->elbow_amplify,
                                                       fabsf(elbow_v), now_ms);
        out->elbow_current_a += (elbow_v < 0.0f ? -elbow_boost : elbow_boost);
        if (!GravityCompensation_Compute(config->shoulder_gravity, shoulder, &joint_nm)) {
            return false;
        }
        const float up = (float)config->shoulder_gravity->sign_g;
        out->shoulder_rotor_nm = joint_nm * config->shoulder_gain
            / config->shoulder_rotor_per_joint
            + up * ShoulderAmplify_Step(amplify, config->shoulder_amplify,
                                        up * shoulder->velocity_rad_s, now_ms);
    } else {
        if (config->elbow_support == NULL) { return false; }
        if (!S5ElbowSupport_Compute(config->elbow_support, elbow,
                                    &out->elbow_current_a)) { return false; }
    }
    if (!isfinite(out->shoulder_rotor_nm) || !isfinite(out->elbow_current_a)) { return false; }
    if (config->dual) {
        (void)ForceAssist_Update(force, config->force_assist, force_valid, force_n,
                                 shoulder->position_rad, elbow->position_rad, now_ms);
        const float gain = ForceAssist_Gain(force);
        out->shoulder_rotor_nm *= gain;
        out->elbow_current_a *= gain;
    }
    if (!assist_enabled) {
        out->shoulder_rotor_nm = 0.0f;
        out->elbow_current_a = 0.0f;
    }
    if (config->dual) {
        out->shoulder_rotor_nm = fmaxf(-config->shoulder_rotor_cap_nm,
            fminf(config->shoulder_rotor_cap_nm, out->shoulder_rotor_nm));
        out->elbow_current_a = fmaxf(-config->elbow_current_cap_a,
            fminf(config->elbow_current_cap_a, out->elbow_current_a));
    }
    return true;
}
