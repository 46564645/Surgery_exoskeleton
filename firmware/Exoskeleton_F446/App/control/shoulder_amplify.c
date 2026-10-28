#include "shoulder_amplify.h"
#include <math.h>
#include <stddef.h>

static bool duration(uint32_t ms)
{ return ms > 0U && ms < UINT32_C(0x80000000); }

bool ShoulderAmplify_ConfigValid(const ShoulderAmplifyConfig *config)
{
    if (config == NULL || !isfinite(config->increment_rotor_nm)
        || config->increment_rotor_nm < 0.0f) { return false; }
    if (config->increment_rotor_nm == 0.0f) { return true; }
    return isfinite(config->starting_velocity_rad_s)
        && config->starting_velocity_rad_s > 0.0f
        && duration(config->suspension_ms)
        && (config->ramp_ms == 0U || duration(config->ramp_ms));
}

void ShoulderAmplify_Reset(ShoulderAmplify *amplify, uint32_t now_ms)
{
    if (amplify == NULL) { return; }
    *amplify = (ShoulderAmplify){0};
    amplify->initial_movement = true;
    amplify->step_ms = now_ms;
}

/* ExoKit AmplifyReaction::runLoop(): engage above startingVelocity in an
 * allowed direction; on a direction change switch off and wait
 * dirChangeSuspensionTime; below startingVelocity keep the torque until
 * dirChangeSuspensionTime after enabling. The maxVelocity cut is the
 * existing S5 soft speed pause, applied by the caller. */
float ShoulderAmplify_Step(ShoulderAmplify *amplify, const ShoulderAmplifyConfig *config,
                           float raise_velocity_rad_s, uint32_t now_ms)
{
    if (amplify == NULL || config == NULL || config->increment_rotor_nm <= 0.0f
        || !isfinite(raise_velocity_rad_s)) { return 0.0f; }
    const float v = raise_velocity_rad_s;
    const bool up = v > config->starting_velocity_rad_s;
    if (up || v < -config->starting_velocity_rad_s) {
        if (amplify->raising != up) {
            amplify->raising = up;
            if (amplify->initial_movement) {
                amplify->initial_movement = false;
            } else {
                amplify->direction_changed = true;
                amplify->direction_change_ms = now_ms;
            }
            amplify->engaged = false;
        }
        if (up && !amplify->engaged && (!amplify->direction_changed
            || (uint32_t)(now_ms - amplify->direction_change_ms) > config->suspension_ms)) {
            amplify->engaged = true;
            amplify->engage_ms = now_ms;
        }
    } else if (amplify->engaged
               && (uint32_t)(now_ms - amplify->engage_ms) > config->suspension_ms) {
        amplify->engaged = false;
    }
    /* ramp_ms == 0 switches instantly, which is what ExoKit does; a ramp was
     * a local addition and is far too slow for a 250 ms burst (it needs
     * 241 ms to reach 90 %, i.e. the whole movement). */
    float delta = config->ramp_ms == 0U
        ? 1.0f
        : (float)(uint32_t)(now_ms - amplify->step_ms) / (float)config->ramp_ms;
    amplify->step_ms = now_ms;
    amplify->scale = amplify->engaged ? fminf(1.0f, amplify->scale + delta)
                                      : fmaxf(0.0f, amplify->scale - delta);
    return config->increment_rotor_nm * amplify->scale;
}
