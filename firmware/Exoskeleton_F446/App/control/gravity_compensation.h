#ifndef GRAVITY_COMPENSATION_H
#define GRAVITY_COMPENSATION_H
/* L3: fixed-elbow shoulder gravity candidate, joint-side Nm only. */
#include "joint_state.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    float amplitude_nm;      /* Joint-side g * sum(mass_kg * lever_arm_m). */
    float gravity_zero_rad;  /* Joint angle at the model's zero-torque pose. */
    int8_t sign_g;            /* Confirmed positive-joint torque direction: +/-1. */
    bool confirmed;           /* Geometry, load and sign need site evidence. */
} GravityCompensationConfig;

/* A valid candidate still needs fresh feedback and the output safety gate.
 * On failure, out_joint_torque_nm is cleared and no candidate is produced. */
bool GravityCompensation_Compute(const GravityCompensationConfig *config,
                                 const JointState *state, float *out_joint_torque_nm);

#endif
