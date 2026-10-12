#include "elbow_session_angle.h"
#include <math.h>
#include <stddef.h>
#include <string.h>

#define ELBOW_TICKS_PER_REV 8192
#define ELBOW_RAD_PER_TICK (6.2831853071795864769f / 8192.0f)

static int32_t shortest_ticks(uint16_t now, uint16_t before)
{
    int32_t delta = (int32_t)now - (int32_t)before;
    if (delta > ELBOW_TICKS_PER_REV / 2) { delta -= ELBOW_TICKS_PER_REV; }
    if (delta < -ELBOW_TICKS_PER_REV / 2) { delta += ELBOW_TICKS_PER_REV; }
    return delta;
}

void ElbowSessionAngle_Reset(ElbowSessionAngle *session)
{ if (session != NULL) { memset(session, 0, sizeof(*session)); } }

bool ElbowSessionAngle_Begin(ElbowSessionAngle *session, uint32_t now_ms)
{
    if (session == NULL || session->phase != ELBOW_SESSION_UNSET) { return false; }
    session->begin_ms = now_ms;
    session->phase = ELBOW_SESSION_WAITING;
    return true;
}

void ElbowSessionAngle_Observe(ElbowSessionAngle *session, const C620Feedback *sample,
                               bool stationary, uint32_t now_ms, uint32_t timeout_ms,
                               float max_speed_rad_s, int8_t direction)
{
    if (session == NULL || (session->phase != ELBOW_SESSION_WAITING
        && session->phase != ELBOW_SESSION_ACTIVE)) { return; }
    if (timeout_ms == 0U || timeout_ms >= UINT32_C(0x80000000)
        || !isfinite(max_speed_rad_s) || max_speed_rad_s <= 0.0f
        || (direction != 1 && direction != -1)) { session->phase = ELBOW_SESSION_FAULT; return; }
    if (sample == NULL || !sample->has_sample || !sample->valid
        || sample->sequence == 0U
        || sample->rotor_angle_ticks >= ELBOW_TICKS_PER_REV
        || (uint32_t)(now_ms - sample->received_ms) > timeout_ms) {
        if (session->phase == ELBOW_SESSION_ACTIVE) { session->phase = ELBOW_SESSION_FAULT; }
        return;
    }
    uint32_t after_begin = sample->received_ms - session->begin_ms;
    if (session->phase == ELBOW_SESSION_WAITING
        && (after_begin == 0U || after_begin >= UINT32_C(0x80000000))) { return; }
    float velocity = (float)direction * sample->rotor_velocity_rad_s;
    if (!isfinite(velocity) || fabsf(velocity) > max_speed_rad_s) {
        session->phase = ELBOW_SESSION_FAULT; return;
    }
    if (session->phase == ELBOW_SESSION_WAITING) {
        if (!stationary) { return; }
        session->zero_ticks = session->last_ticks = sample->rotor_angle_ticks;
        session->last_ms = sample->received_ms;
        session->last_sequence = sample->sequence;
        session->joint = (JointState){0.0f, velocity, sample->received_ms, true};
        session->phase = ELBOW_SESSION_ACTIVE;
        return;
    }
    uint32_t sequence_step = sample->sequence - session->last_sequence;
    if (sequence_step == 0U) { return; } /* Re-reading a snapshot cannot refresh it. */
    if (sequence_step >= UINT32_C(0x80000000)) { session->phase = ELBOW_SESSION_FAULT; return; }
    uint32_t elapsed_ms = sample->received_ms - session->last_ms;
    if (elapsed_ms >= UINT32_C(0x80000000) || elapsed_ms > timeout_ms) {
        session->phase = ELBOW_SESSION_FAULT; return;
    }
    int32_t step_ticks = shortest_ticks(sample->rotor_angle_ticks, session->last_ticks);
    /* Two encoder counts allow quantization at small sample intervals. A larger
     * jump cannot be distinguished from a missed turn, so revoke the origin. */
    float allowed_ticks = max_speed_rad_s * ((float)elapsed_ms / 1000.0f)
        / ELBOW_RAD_PER_TICK + 2.0f;
    if (!isfinite(allowed_ticks) || fabsf((float)step_ticks) > allowed_ticks) {
        session->phase = ELBOW_SESSION_FAULT; return;
    }
    int32_t from_zero = shortest_ticks(sample->rotor_angle_ticks, session->zero_ticks);
    session->joint = (JointState){(float)direction * (float)from_zero * ELBOW_RAD_PER_TICK,
        velocity, sample->received_ms, true};
    session->last_ticks = sample->rotor_angle_ticks;
    session->last_ms = sample->received_ms;
    session->last_sequence = sample->sequence;
}

bool ElbowSessionAngle_Read(const ElbowSessionAngle *session, uint32_t now_ms,
                            uint32_t timeout_ms, JointState *out)
{
    if (out == NULL) { return false; }
    *out = (JointState){0};
    if (session == NULL || session->phase != ELBOW_SESSION_ACTIVE
        || (uint32_t)(now_ms - session->last_ms) > timeout_ms) { return false; }
    *out = session->joint;
    return true;
}
