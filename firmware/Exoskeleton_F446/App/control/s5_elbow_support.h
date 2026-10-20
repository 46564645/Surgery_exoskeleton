#ifndef S5_ELBOW_SUPPORT_H
#define S5_ELBOW_SUPPORT_H
/* L3: local low-pose elbow current candidate, A. This is not joint torque. */
#include "joint_state.h"
#include "gravity_compensation.h"
#include <stdbool.h>

typedef struct {
    float current_at_zero_a;    /* Signed C620 command at the RAM session origin. */
    float slope_a_per_rad;      /* Signed local change per joint radian. */
    bool confirmed;             /* Site-selected bounded candidate; not proof of support. */
} S5ElbowSupportConfig;

bool S5ElbowSupport_Compute(const S5ElbowSupportConfig *config,
                            const JointState *elbow, float *out_current_a);
bool S5ElbowGravity_Compute(const GravityCompensationConfig *config,
                            const JointState *shoulder, const JointState *elbow,
                            float *out_joint_torque_nm);
#endif
