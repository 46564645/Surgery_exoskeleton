/* L3: ExoKit direction/zero semantics adapted to explicit rotor/joint ratio. */
#include "joint_state.h"
#include <math.h>
#include <stddef.h>

bool JointState_FromRotor(const JointMapping *m, float rotor_rad, float rotor_rad_s,
                          uint32_t received_ms, bool continuous, JointState *out)
{
    if (out == NULL) { return false; }
    *out = (JointState){0};
    if (m == NULL || !m->confirmed || !continuous
        || (m->direction != 1 && m->direction != -1)
        || !isfinite(m->rotor_per_joint) || m->rotor_per_joint <= 0.0f
        || !isfinite(m->rotor_zero_rad) || !isfinite(rotor_rad) || !isfinite(rotor_rad_s)) {
        return false;
    }
    float position = (float)m->direction * (rotor_rad - m->rotor_zero_rad) / m->rotor_per_joint;
    float velocity = (float)m->direction * rotor_rad_s / m->rotor_per_joint;
    if (!isfinite(position) || !isfinite(velocity)) { return false; }
    *out = (JointState){position, velocity, received_ms, true};
    return true;
}
