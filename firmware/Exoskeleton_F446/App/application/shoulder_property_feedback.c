/* Receive association adapted from encoder_stream: one pending request,
 * timestamp checks, complete-cycle publication and no retry after ambiguity.
 * No IDLE restriction: axis state and output permission belong to S3. */
#include "shoulder_property_feedback.h"
#include <math.h>
#include <stddef.h>
#include <string.h>

static bool duration_valid(uint32_t value)
{ return value > 0U && value < UINT32_C(0x80000000); }
void ShoulderPropertyFeedback_Init(ShoulderPropertyFeedback *r, uint32_t reply_timeout_ms)
{
    if (r == NULL) { return; }
    memset(r, 0, sizeof(*r));
    if (duration_valid(reply_timeout_ms)) { r->reply_timeout_ms = reply_timeout_ms; }
}
void ShoulderPropertyFeedback_Invalidate(ShoulderPropertyFeedback *r)
{
    if (r == NULL) { return; }
    r->pending = false; r->quarantined = true; r->published.valid = false;
}
void ShoulderPropertyFeedback_Poll(ShoulderPropertyFeedback *r, uint32_t now_ms)
{
    if (r != NULL && r->pending && (uint32_t)(now_ms - r->request_ms) > r->reply_timeout_ms) {
        ShoulderPropertyFeedback_Invalidate(r);
    }
}
bool ShoulderPropertyFeedback_Expect(ShoulderPropertyFeedback *r,
                                    GdsPropertyField field, uint32_t submitted_ms)
{
    if (r == NULL || r->reply_timeout_ms == 0U || r->quarantined) { return false; }
    ShoulderPropertyFeedback_Poll(r, submitted_ms);
    if (r->quarantined) { return false; }
    if (r->pending || (unsigned)field >= GDS_PROPERTY_COUNT || field != r->next
        || (r->has_reply && (uint32_t)(submitted_ms - r->last_reply_ms) >= UINT32_C(0x80000000))) {
        ShoulderPropertyFeedback_Invalidate(r); return false;
    }
    if (field == GDS_PROPERTY_POSITION) { memset(&r->working, 0, sizeof(r->working)); }
    r->request_ms = submitted_ms; r->pending = true;
    return true;
}
void ShoulderPropertyFeedback_Receive(ShoulderPropertyFeedback *r,
                                     BoardCanBus bus, const BoardCanFrame *f, uint32_t now_ms)
{
    if (r == NULL || r->reply_timeout_ms == 0U || r->quarantined || f == NULL
        || bus != BOARD_CAN_SHOULDER || (f->id & 0x1FU) != 0x005U) { return; }
    GdsPropertyValue value;
    if (!r->pending || (uint32_t)(now_ms - r->request_ms) > r->reply_timeout_ms
        || (uint32_t)(f->received_ms - r->request_ms) > r->reply_timeout_ms
        || (uint32_t)(now_ms - f->received_ms) > r->reply_timeout_ms
        || !GdsSdoRead_PropertyDecode(r->next, bus, f, &value)
        || ((r->next == GDS_PROPERTY_POSITION || r->next == GDS_PROPERTY_VELOCITY)
            && !isfinite(value.real * 6.2831853071795864769f))) {
        ShoulderPropertyFeedback_Invalidate(r); return;
    }
    r->working.value[r->next] = value;
    r->working.requested_ms[r->next] = r->request_ms;
    r->working.received_ms[r->next] = f->received_ms;
    r->last_reply_ms = f->received_ms; r->has_reply = true;
    r->pending = false;
    if ((unsigned)r->next + 1U == GDS_PROPERTY_COUNT) {
        r->working.sequence = r->published.sequence + 1U;
        r->working.valid = true; r->published = r->working;
        r->next = GDS_PROPERTY_POSITION;
    } else { r->next = (GdsPropertyField)((unsigned)r->next + 1U); }
}
bool ShoulderPropertyFeedback_Get(const ShoulderPropertyFeedback *r,
                                 uint32_t now_ms, uint32_t freshness_ms, ShoulderPropertySnapshot *out)
{
    if (out == NULL) { return false; }
    memset(out, 0, sizeof(*out));
    if (r == NULL) { return false; }
    *out = r->published;
    out->valid = out->valid && !r->quarantined && duration_valid(freshness_ms)
        && !(r->pending && (uint32_t)(now_ms - r->request_ms) > r->reply_timeout_ms);
    for (unsigned i = 0U; i < GDS_PROPERTY_COUNT; i++) {
        /* Age from request is deliberately conservative: a response to a read
         * issued before a write must not certify that later write. */
        if ((uint32_t)(now_ms - out->requested_ms[i]) > freshness_ms) { out->valid = false; }
    }
    return out->valid;
}
