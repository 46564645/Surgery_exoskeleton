#ifndef SAFETY_SUPERVISOR_H
#define SAFETY_SUPERVISOR_H
/* L3: pure two-joint permit checks; no HAL, driver writes or persistence. */
#include "joint_state.h"
#define SAFETY_JOINT_COUNT 2U
#define SAFETY_SHOULDER_AXES 1U
#define SAFETY_ELBOW_AXES 2U
#define SAFETY_ALL_AXES 3U
typedef enum {
    SAFETY_STOP_EXTERNAL_INPUT = 0,
    SAFETY_STOP_SUPERVISED_C620_BENCH,
    SAFETY_STOP_SUPERVISED_SHOULDER_BENCH,
    SAFETY_STOP_SUPERVISED_SHOULDER_FOLLOW,
    SAFETY_STOP_SUPERVISED_ELBOW_FOLLOW,
    SAFETY_STOP_SUPERVISED_DUAL_FOLLOW
} SafetyStopPolicy;
typedef struct {
    JointMapping mapping;
    float lower_rad, upper_rad, max_speed_rad_s;
    /* SHOULDER: rotor Nm; ELBOW: command A. Never interchangeable. */
    float max_motor_command, motor_slew_per_s;
    bool limits_confirmed, feedback_confirmed, protocol_confirmed;
    /* Relative native rotor software envelope, not measured physical limits. */
    bool relative_native_envelope, software_envelope_approved;
} SafetyJointConfig;
typedef struct {
    SafetyJointConfig joint[SAFETY_JOINT_COUNT];
    uint32_t feedback_timeout_ms, service_limit_ms, max_request_ms;
    uint8_t enabled_axes; /* Immutable for this boot; 0 grants no permit. */
    bool protection_confirmed;
    bool stop_method_confirmed, timing_confirmed;
    SafetyStopPolicy stop_policy; /* Bench excludes an uninstalled physical stop input. */
} SafetyConfig;
typedef struct {
    JointState joint[SAFETY_JOINT_COUNT];
    bool transport_healthy, operator_stop_clear, drivers_ready, independently_stopped;
    bool feedback_stationary; /* Encoder/RPM evidence only, never independent stop. */
    uint8_t stationary_axes; /* S5 independent standstill proofs: bit 0 shoulder, bit 1 elbow. */
} SafetyInputs;
typedef enum {
    SAFETY_OK, SAFETY_UNCONFIGURED, SAFETY_TRANSPORT,
    SAFETY_STOP_INPUT, SAFETY_DRIVER_NOT_READY, SAFETY_FEEDBACK,
    SAFETY_RANGE, SAFETY_SPEED
} SafetyReason;
bool SafetySupervisor_AxisEnabled(const SafetyConfig *config, unsigned axis);
bool SafetySupervisor_ConfigReady(const SafetyConfig *config);
bool SafetySupervisor_StopObserved(const SafetyConfig *config, const SafetyInputs *inputs);
/* Preparation still checks BOTH joints/config/stop/transport; only closed-loop
 * driver readiness is deferred until independently observed. */
SafetyReason SafetySupervisor_CheckBeforePrepare(const SafetyConfig *config,
                                                const SafetyInputs *inputs, uint32_t now_ms);
SafetyReason SafetySupervisor_Check(const SafetyConfig *config,
                                    const SafetyInputs *inputs, uint32_t now_ms);
#endif
