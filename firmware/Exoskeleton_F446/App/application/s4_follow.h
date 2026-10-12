#ifndef S4_FOLLOW_H
#define S4_FOLLOW_H
/* S4 application lifecycle. Candidate calculation never sends motor frames. */
#include "s3_feedback.h"
#include "s3_shoulder_service.h"
#include "s4_site.h"

typedef enum { S4_FOLLOW_DISABLED, S4_FOLLOW_IDLE, S4_FOLLOW_OBSERVING,
               S4_FOLLOW_STOPPED, S4_FOLLOW_FAULT, S4_FOLLOW_PREPARING,
               S4_FOLLOW_READY, S4_FOLLOW_RUNNING, S4_FOLLOW_RAMPING_DOWN,
               S4_FOLLOW_STOPPING } S4FollowPhase;
typedef struct {
    S4FollowPhase phase;
    S3ReaderStatus reader;
    S3ShoulderObservation observation;
    float model_joint_torque_nm;
    uint32_t model_sequence, model_sample_ms;
    bool model_valid; /* False after stale data, cancellation or reader fault. */
    bool new_model_sample; /* Only a new complete seven-property cycle. */
    uint32_t sample_asof_ms, sample_age_ms;
    bool sample_fresh_now; /* Query-time property/heartbeat freshness, not output permit. */
    OutputState output_state;
    OutputReason output_reason;
    SafetyReason safety_reason;
    uint32_t remaining_ms, ramp_in_ms, ramp_remaining_ms;
    bool output_bound, output_allowed;
    bool session_zero_valid; /* RAM only; never survives stop, fault or reset. */
    float session_zero_rev; /* Raw continuous rotor position at vertical reference. */
    uint32_t session_zero_sample_ms, session_zero_sequence;
    bool motor_effort_active; /* Command-side torque may persist; not measured physical support. */
    bool ramping_down, stop_attempted, stop_accepted, stop_delivery_failed;
    bool warning_due, warning_queued, warning_failed;
} S4FollowStatus;

void S4Follow_Init(const S4SiteProfile *profile);
bool S4Follow_IsSelected(void);
void S4Follow_Poll(uint32_t now_ms, uint32_t elapsed_ms);
bool S4Follow_Observe(uint32_t now_ms);
bool S4Follow_Prepare(uint32_t now_ms);
bool S4Follow_Start(uint32_t now_ms);
/* Returns true only when a healthy live session entered normal ramp-down. */
bool S4Follow_Cancel(void);
void S4Follow_GetStatus(S4FollowStatus *out);

#endif
