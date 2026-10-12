#include "gravity_compensation.h"
#include <math.h>
#include <stddef.h>

bool GravityCompensation_Compute(const GravityCompensationConfig *config,
                                 const JointState *state, float *out_joint_torque_nm)
{
    if (out_joint_torque_nm == NULL) { return false; }
    *out_joint_torque_nm = 0.0f;
    if (config == NULL || state == NULL || !config->confirmed || !state->valid
        || !isfinite(config->amplitude_nm) || config->amplitude_nm <= 0.0f
        || !isfinite(config->gravity_zero_rad)
        || (config->sign_g != 1 && config->sign_g != -1)
        || !isfinite(state->position_rad) || !isfinite(state->velocity_rad_s)) {
        return false;
    }
    float relative_rad = state->position_rad - config->gravity_zero_rad;
    if (!isfinite(relative_rad)) { return false; }
    float torque_nm = (float)config->sign_g * config->amplitude_nm * sinf(relative_rad);
    if (!isfinite(torque_nm)) { return false; }
    *out_joint_torque_nm = torque_nm;
    return true;
}
