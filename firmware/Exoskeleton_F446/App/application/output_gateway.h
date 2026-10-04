#ifndef OUTPUT_GATEWAY_H
#define OUTPUT_GATEWAY_H
/* L4: sole output submission authority. Main-loop use only, static lifetime. */
#include "motor_command.h"
#include "safety_supervisor.h"
typedef struct {
    /* Must synchronously invalidate ALL previously pending normal commands,
     * including hardware mailboxes. True means no old normal frame remains.
     * submit copies exactly two frames, never retains caller pointers or a
     * backlog. Any partial/busy/error returns false. Stops also go through here.
     * No production binding exists until S2/protocol/stop behavior is proven. */
    bool (*purge)(void *context, uint8_t axes);
    bool (*submit)(void *context, const BoardCanFrame frames[2], uint8_t axes, bool stop);
    void *context;
} OutputTransport;
typedef enum { OUTPUT_SAFE_OFF, OUTPUT_ARMED, OUTPUT_ACTIVE, OUTPUT_FAULT,
               OUTPUT_PREPARING, OUTPUT_READY } OutputState;
typedef enum { DRIVER_NOT_PREPARED, DRIVER_WAIT_ZERO, DRIVER_WAIT_MODE,
               DRIVER_WAIT_CLOSED_LOOP, DRIVER_READY } OutputDriverPhase;
typedef struct {
    uint32_t received_ms, axis_error, zero_verified_ms, mode_verified_ms;
    uint8_t node, axis_state, fault_flags;
    bool valid, calibration_verified, zero_verified, mode_verified;
} OutputDriverStatus;
typedef enum {
    OUTPUT_REASON_NONE, OUTPUT_REASON_CONFIG, OUTPUT_REASON_HEALTH,
    OUTPUT_REASON_REQUEST, OUTPUT_REASON_EXPIRED, OUTPUT_REASON_SERVICE,
    OUTPUT_REASON_TRANSPORT, OUTPUT_REASON_CANCEL
} OutputReason;
typedef struct {
    SafetyConfig config;
    OutputTransport transport;
    OutputState state;
    OutputReason reason;
    SafetyReason safety_reason;
    uint8_t shoulder_node, elbow_id, selected_joint;
    uint32_t last_step_ms, request_ms, ttl_ms, armed_ms;
    uint32_t prepare_ms, prepare_phase_ms, prepare_timeout_ms;
    OutputDriverPhase driver_phase;
    float target, applied;
    bool request_valid, stop_attempted, stop_accepted, purge_ok;
    bool stop_delivery_failed; /* Bench recovery requires power removal then MCU restart. */
    bool shoulder_attempted; /* Shoulder bench: one accepted request per cold boot. */
} OutputGateway;
void OutputGateway_Init(OutputGateway *gateway, const SafetyConfig *config,
                        const OutputTransport *transport, uint8_t shoulder_node, uint8_t elbow_id);
/* Human-requested zero -> verified-zero -> mode -> verified-mode -> closed loop
 * -> NEW healthy heartbeat. CAN acceptance alone never marks the driver ready. */
bool OutputGateway_Prepare(OutputGateway *gateway, const SafetyInputs *inputs,
                          const OutputDriverStatus driver[SAFETY_JOINT_COUNT], uint32_t timeout_ms, uint32_t now_ms);
void OutputGateway_PollPreparation(OutputGateway *gateway, const SafetyInputs *inputs,
                                   const OutputDriverStatus driver[SAFETY_JOINT_COUNT], uint32_t now_ms);
bool OutputGateway_DriverReady(const OutputGateway *gateway,
                               const OutputDriverStatus driver[SAFETY_JOINT_COUNT], uint32_t now_ms);
void OutputGateway_RejectRequest(OutputGateway *gateway);
bool OutputGateway_Arm(OutputGateway *gateway, const SafetyInputs *inputs, uint32_t now_ms);
/* Value is motor-side effort along positive JOINT direction: shoulder rotor
 * Nm or elbow command A. The mapping direction converts it to motor sign.
 * One selected joint at a time; switch joints only after Cancel + fresh Arm. */
bool OutputGateway_Replace(OutputGateway *gateway, unsigned joint, float value,
                           uint32_t ttl_ms, uint32_t now_ms);
void OutputGateway_Step(OutputGateway *gateway, const SafetyInputs *inputs, uint32_t now_ms);
void OutputGateway_Cancel(OutputGateway *gateway);
/* Explicit acknowledgement after external stop verification, then fresh Arm.
 * No auto recovery, fault auto-clear, old requests, or persistent auto-permit. */
bool OutputGateway_AcknowledgeFault(OutputGateway *gateway, const SafetyInputs *inputs,
                                    uint32_t now_ms, bool manual_acknowledged);
#endif
