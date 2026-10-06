#include "recorder.h"
#include <stddef.h>

static void increment(uint32_t *n) { if (*n != UINT32_MAX) { ++*n; } }
void Bb_Push(BbRecorder *r, const BbEvent *event, bool fault)
{
    if (r->frozen) { increment(&r->suppressed); return; }
    /* Leave room for the trigger and its follow-up; preserve the first fault. */
    if (!r->triggered && r->count == BB_PRE_EVENTS) {
        r->head = (uint16_t)((r->head + 1U) % BB_CAPACITY);
        --r->count;
        increment(&r->overwritten);
    }
    uint16_t at = (uint16_t)((r->head + r->count) % BB_CAPACITY);
    r->events[at] = *event;
    r->events[at].seq = r->next_seq++;
    ++r->count;
    if (!r->triggered && fault) {
        r->triggered = true;
        r->trigger_seq = r->events[at].seq;
        r->post_left = BB_POST_EVENTS;
    } else if (r->triggered && --r->post_left == 0U) {
        r->frozen = true;
    }
}
bool Bb_Read(const BbRecorder *r, uint16_t index, BbEvent *out)
{
    if (!r->frozen || out == NULL || index >= r->count) { return false; }
    *out = r->events[(r->head + index) % BB_CAPACITY];
    return true;
}
void Bb_Freeze(BbRecorder *r) { r->frozen = true; }
