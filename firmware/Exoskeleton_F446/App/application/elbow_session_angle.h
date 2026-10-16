#ifndef ELBOW_SESSION_ANGLE_H
#define ELBOW_SESSION_ANGLE_H
/* One explicit RAM origin for a direct-drive C620 elbow in a limited sector. */
#include "c620_feedback.h"
#include "joint_state.h"

typedef enum { ELBOW_SESSION_UNSET, ELBOW_SESSION_WAITING,
               ELBOW_SESSION_ACTIVE, ELBOW_SESSION_FAULT } ElbowSessionPhase;
typedef struct {
    ElbowSessionPhase phase;
    uint32_t begin_ms, last_ms, last_sequence;
    uint16_t zero_ticks, last_ticks;
    JointState joint;
    bool dual_follow;
} ElbowSessionAngle;

void ElbowSessionAngle_Reset(ElbowSessionAngle *session);
bool ElbowSessionAngle_Begin(ElbowSessionAngle *session, uint32_t now_ms);
bool ElbowSessionAngle_BeginForDual(ElbowSessionAngle *session, uint32_t now_ms);
/* stationary is a fresh, post-Begin C620 standstill observation. */
void ElbowSessionAngle_Observe(ElbowSessionAngle *session, const C620Feedback *sample,
                               bool stationary, uint32_t now_ms, uint32_t timeout_ms,
                               float max_speed_rad_s, int8_t direction);
bool ElbowSessionAngle_Read(const ElbowSessionAngle *session, uint32_t now_ms,
                            uint32_t timeout_ms, JointState *out);
#endif
