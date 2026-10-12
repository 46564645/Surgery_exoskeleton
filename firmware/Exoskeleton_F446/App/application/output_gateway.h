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
    /* FOLLOW: positive proof on every selected bus that the last accepted
     * normal frame left CAN. The buses do not transmit atomically.
     * A free mailbox alone is insufficient if it was aborted or errored. */
    bool (*drained)(void *context, uint8_t axes);
    /* FOLLOW: any selected bus completed without TXOK. */
    bool (*failed)(void *context, uint8_t axes);
    /* Optional bxCAN completion queries for the supervised S3 elbow bench.
     * A pending purge is not a completed purge or a transport failure. */
    bool (*purge_pending)(void *context, uint8_t axes);
    bool (*pending)(void *context, uint8_t axes);
} OutputTransport;
typedef enum { OUTPUT_SAFE_OFF, OUTPUT_ARMED, OUTPUT_ACTIVE, OUTPUT_FAULT,
               OUTPUT_PREPARING, OUTPUT_READY, OUTPUT_RAMPING_DOWN } OutputState;
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
/* Motor-side candidates keep dissimilar units in named fields. Disabled-axis
 * fields must be zero. A pair is one logical request, not a CAN-wire atomic TX. */
typedef struct { float shoulder_rotor_nm; float elbow_current_a; } S5FollowEffort;
typedef struct {
    uint32_t sample_ms[SAFETY_JOINT_COUNT];
    uint32_t sequence[SAFETY_JOINT_COUNT];
} S5FollowSample;
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
    bool stop_pending_purge, stop_pending_tx;
    uint32_t stop_started_ms, s3_output_submit_ms;
    bool shoulder_attempted; /* Shoulder bench: one accepted request per cold boot. */
    /* S4/S5 finite FOLLOW session. All times use unsigned elapsed comparisons. */
    uint32_t send_interval_ms, session_ms, ramp_max_ms;
    uint32_t session_deadline_ms, ramp_start_ms, ramp_begin_ms, ramp_deadline_ms;
    uint32_t last_sent_ms, last_sequence, last_sample_ms;
    uint32_t follow_output_submit_ms;
    float release_slew_nm_s;
    bool follow_configured, follow_session_used, follow_sequence_valid, follow_target_pending;
    bool follow_reference_set; /* One RAM reference install before preparation. */
    bool follow_zero_pending;
    bool follow_output_pending;
    S5FollowEffort axes_target, axes_applied, axes_release_slew_per_s;
    uint32_t axes_last_sequence[SAFETY_JOINT_COUNT], axes_last_sample_ms[SAFETY_JOINT_COUNT];
    bool axes_sequence_valid[SAFETY_JOINT_COUNT], elbow_reference_set;
} OutputGateway;
void OutputGateway_Init(OutputGateway *gateway, const SafetyConfig *config,
                        const OutputTransport *transport, uint8_t shoulder_node, uint8_t elbow_id);
/* S4 or S5 dual session-zero: update shoulder rotor origin once in SAFE_OFF. Never
 * resets a gateway or clears an earlier fault/stop latch. */
bool OutputGateway_SetFollowReference(OutputGateway *gateway, float rotor_zero_rad);
/* S5 elbow RAM zero is owned by the feedback mapper. Pass relative zero 0;
 * this confirms its installation once, keeping gateway mapping relative. */
bool OutputGateway_SetElbowFollowReference(OutputGateway *gateway, float rotor_zero_rad);
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
/* S4 configuration is accepted only in SAFE_OFF before its sole session.
 * A candidate is a fresh complete sample's motor-side Nm along positive
 * JOINT direction; duplicates, stale samples and invalid values fault-stop. */
bool OutputGateway_ConfigureFollow(OutputGateway *gateway, uint32_t send_interval_ms,
                                   uint32_t session_ms, uint32_t ramp_max_ms,
                                   float release_slew_nm_s);
bool OutputGateway_RefreshFollow(OutputGateway *gateway, float motor_effort_nm,
                                 uint32_t sample_ms, uint32_t sequence, uint32_t now_ms);
/* S5 elbow-only (mask 2) or dual-axis (mask 3) finite FOLLOW. Candidate
 * samples for every selected axis must be new, ordered and fresh. */
bool OutputGateway_ConfigureAxesFollow(OutputGateway *gateway, uint32_t send_interval_ms,
                                       uint32_t session_ms, uint32_t ramp_max_ms,
                                       S5FollowEffort release_slew_per_s);
bool OutputGateway_RefreshAxesFollow(OutputGateway *gateway, S5FollowEffort effort,
                                     S5FollowSample sample, uint32_t now_ms);
/* Normal FOLLOW cancel/expiry. Revokes new targets immediately and retains
 * bounded stop-only authority until selected zero commands are sent with
 * TXOK proof, then selected terminal stop frames are accepted by HAL. */
bool OutputGateway_BeginNormalStop(OutputGateway *gateway, OutputReason reason, uint32_t now_ms);
void OutputGateway_Step(OutputGateway *gateway, const SafetyInputs *inputs, uint32_t now_ms);
/* Timestamped cancel for application commands between control ticks. */
void OutputGateway_CancelAt(OutputGateway *gateway, uint32_t now_ms);
void OutputGateway_Cancel(OutputGateway *gateway);
/* Explicit acknowledgement after external stop verification, then fresh Arm.
 * No auto recovery, fault auto-clear, old requests, or persistent auto-permit. */
bool OutputGateway_AcknowledgeFault(OutputGateway *gateway, const SafetyInputs *inputs,
                                    uint32_t now_ms, bool manual_acknowledged);
#endif
