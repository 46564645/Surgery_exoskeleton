#include "s5_elbow_support.h"
#include <math.h>
#include <stddef.h>

bool S5ElbowSupport_Compute(const S5ElbowSupportConfig *config,
                            const JointState *elbow, float *out_current_a)
{
    if (out_current_a == NULL) { return false; }
    *out_current_a = 0.0f;
    if (config == NULL || elbow == NULL || !config->confirmed || !elbow->valid
        || !isfinite(config->current_at_zero_a) || !isfinite(config->slope_a_per_rad)
        || !isfinite(elbow->position_rad) || !isfinite(elbow->velocity_rad_s)) { return false; }
    float current_a = config->current_at_zero_a
        + config->slope_a_per_rad * elbow->position_rad;
    if (!isfinite(current_a)) { return false; }
    *out_current_a = current_a;
    return true;
}

bool S5ElbowGravity_Compute(const GravityCompensationConfig *config,
                            const JointState *shoulder, const JointState *elbow,
                            float *out_joint_torque_nm)
{
    if (out_joint_torque_nm == NULL) { return false; }
    *out_joint_torque_nm = 0.0f;
    if (shoulder == NULL || elbow == NULL || !shoulder->valid || !elbow->valid) {
        return false;
    }
    JointState forearm = *elbow;
    forearm.position_rad += shoulder->position_rad;
    forearm.velocity_rad_s += shoulder->velocity_rad_s;
    float torque_nm;
    if (!GravityCompensation_Compute(config, &forearm, &torque_nm)) { return false; }
    *out_joint_torque_nm = torque_nm * (float)config->sign_g > 0.0f ? torque_nm : 0.0f;
    return true;
}
