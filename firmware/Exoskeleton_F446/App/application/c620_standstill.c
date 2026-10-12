/* L4: continuous receive window; no CAN TX, manual flag or GPIO fiction. */
#include "c620_standstill.h"
#include <stddef.h>
#include <string.h>
void C620Standstill_Reset(C620Standstill *s)
{ if (s != NULL) { memset(s, 0, sizeof(*s)); } }
void C620Standstill_After(C620Standstill *s, uint32_t after_ms)
{
    if (s == NULL) { return; }
    C620Standstill_Reset(s); s->after_ms = after_ms; s->fenced = true;
}
static void clear_window(C620Standstill *s)
{
    s->tracking = s->seen = false;
    s->start_ms = s->last_ms = 0U; s->anchor_ticks = 0U;
    s->minimum = s->maximum = 0;
}
static int16_t signed_offset(uint16_t ticks, uint16_t anchor)
{
    int32_t delta = (int32_t)ticks - (int32_t)anchor;
    if (delta > 4096) { delta -= 8192; }
    if (delta < -4096) { delta += 8192; }
    return (int16_t)delta;
}
void C620Standstill_Observe(C620Standstill *s, const C620Feedback *f, uint32_t now_ms)
{
    if (s == NULL) { return; }
    if (f == NULL || !f->has_sample || !f->valid || f->rotor_angle_ticks > 8191U
        || (uint32_t)(now_ms - f->received_ms) > C620_STILL_FRESH_MS) {
        clear_window(s); return;
    }
    if (s->fenced) {
        uint32_t after = f->received_ms - s->after_ms;
        if (after == 0U || after >= UINT32_C(0x80000000)) { clear_window(s); return; }
        /* Once even the newest pre-event frame must be stale, no fence is
         * needed. This also avoids a half-counter-cycle lifetime limit. */
        if ((uint32_t)(now_ms - s->after_ms) > C620_STILL_FRESH_MS) { s->fenced = false; }
    }
    uint32_t gap = f->received_ms - s->last_ms;
    if (s->seen && gap >= UINT32_C(0x80000000)) {
        clear_window(s); return; /* Out of order, not a new quiet window. */
    }
    if (s->seen && gap > C620_STILL_FRESH_MS) { s->tracking = false; }
    s->seen = true; s->last_ms = f->received_ms;
    if (f->rotor_speed_rpm != 0) { s->tracking = false; return; }
    int16_t offset = signed_offset(f->rotor_angle_ticks, s->anchor_ticks);
    int16_t low = s->minimum < offset ? s->minimum : offset;
    int16_t high = s->maximum > offset ? s->maximum : offset;
    if (!s->tracking || (uint32_t)((int32_t)high - (int32_t)low) > C620_STILL_SPAN_TICKS) {
        s->start_ms = f->received_ms;
        s->anchor_ticks = f->rotor_angle_ticks;
        s->minimum = s->maximum = 0;
        s->tracking = true;
    } else {
        s->minimum = low; s->maximum = high;
        /* Saturate elapsed evidence so continuous stationary operation cannot
         * overflow its dwell after one full millisecond-counter cycle. */
        if ((uint32_t)(s->last_ms - s->start_ms) > C620_STILL_WINDOW_MS) {
            s->start_ms = s->last_ms - C620_STILL_WINDOW_MS;
        }
    }
}
C620StandstillStatus C620Standstill_Read(C620Standstill *s, uint32_t now_ms)
{
    C620StandstillStatus out = {0};
    if (s == NULL || !s->seen) { return out; }
    out.seen = true; out.age_ms = now_ms - s->last_ms;
    out.fresh = out.age_ms <= C620_STILL_FRESH_MS;
    if (!out.fresh) { s->tracking = false; }
    if (s->tracking) { out.dwell_ms = s->last_ms - s->start_ms; }
    out.stationary = out.fresh && s->tracking && out.dwell_ms >= C620_STILL_WINDOW_MS;
    return out;
}
