#ifndef JOINT_STATE_H
#define JOINT_STATE_H
/* L3: main-loop state, angles rad and angular velocity rad/s. */
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    float rotor_per_joint, rotor_zero_rad;
    int8_t direction;
    bool confirmed;
} JointMapping;
typedef struct {
    float position_rad, velocity_rad_s;
    uint32_t received_ms;
    bool valid;
} JointState;
/* Continuous rotor angle is mandatory. A raw C620 modulo angle cannot be
 * passed as continuous=true without a separately established turn reference. */
bool JointState_FromRotor(const JointMapping *mapping, float rotor_rad,
                          float rotor_rad_s, uint32_t received_ms,
                          bool continuous, JointState *out);
#endif
