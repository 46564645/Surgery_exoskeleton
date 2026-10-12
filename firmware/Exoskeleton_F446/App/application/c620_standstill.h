#ifndef C620_STANDSTILL_H
#define C620_STANDSTILL_H
/* Receive-only bench observation, NOT independent stop/power/torque proof. */
#include "c620_feedback.h"
#define C620_STILL_WINDOW_MS 250U
#define C620_STILL_FRESH_MS 50U
#define C620_STILL_SPAN_TICKS 2U
typedef struct {
    uint32_t start_ms, last_ms, after_ms;
    uint16_t anchor_ticks;
    int16_t minimum, maximum; /* Signed ticks around anchor, including 8191 -> 0. */
    bool tracking, seen, fenced;
} C620Standstill;
typedef struct {
    uint32_t dwell_ms, age_ms;
    bool seen, fresh, stationary;
} C620StandstillStatus;
void C620Standstill_Reset(C620Standstill *observer);
/* Event completed at after_ms: queued frames received earlier cannot count. */
void C620Standstill_After(C620Standstill *observer, uint32_t after_ms);
/* Feed EACH successfully decoded target frame, including equal-ms samples. */
void C620Standstill_Observe(C620Standstill *observer, const C620Feedback *sample, uint32_t now_ms);
C620StandstillStatus C620Standstill_Read(C620Standstill *observer, uint32_t now_ms);
#endif
