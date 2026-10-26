#ifndef S5_FOLLOW_H
#define S5_FOLLOW_H
/* One RAM-origin elbow-only or dual FOLLOW session; gateway owns every frame. */
#include "s5_site.h"
#include "s3_shoulder_service.h"

typedef enum {
    S5_FOLLOW_DISABLED, S5_FOLLOW_IDLE, S5_FOLLOW_PREPARING,
    S5_FOLLOW_READY, S5_FOLLOW_RUNNING, S5_FOLLOW_RAMPING_DOWN,
    S5_FOLLOW_STOPPING, S5_FOLLOW_STOPPED, S5_FOLLOW_FAULT
} S5FollowPhase;

typedef struct {
    S5FollowPhase phase;
    uint8_t axes;
    S3ReaderStatus reader;
    JointState joint[SAFETY_JOINT_COUNT];
    S5FollowEffort target; /* Shoulder rotor Nm, elbow C620 command A. */
    S5FollowSample sample;
    bool elbow_zero_valid, shoulder_zero_valid, model_valid;
    bool output_bound, output_allowed, stop_attempted, stop_accepted;
    bool stop_delivery_failed;
    OutputState output_state;
    OutputReason output_reason;
    SafetyReason safety_reason;
    uint32_t remaining_ms;
    /* Soft speed pause record for the current RUNNING session. */
    bool assist_paused;
    uint16_t speed_pauses;
    float peak_speed_rad_s[SAFETY_JOINT_COUNT]; /* |joint velocity| while RUNNING. */
    /* Interaction-force trigger record (optional feature). */
    bool force_active;
    float force_error_n;
    uint16_t force_triggers;
} S5FollowStatus;

void S5Follow_Init(const S5SiteProfile *profile);
bool S5Follow_IsSelected(void);
/* Bench A/B: runtime assist gate. Off keeps the session RUNNING but commands
 * zero effort, so one window can compare the same poses with and without. */
void S5Follow_SetAssist(bool enabled);
bool S5Follow_AssistEnabled(void);
bool S5Follow_Prepare(uint32_t now_ms);
bool S5Follow_Start(uint32_t now_ms);
bool S5Follow_Cancel(void); /* True only for a healthy normal ramp. */
void S5Follow_Poll(uint32_t now_ms, uint32_t elapsed_ms);
void S5Follow_GetStatus(S5FollowStatus *out);
#endif
