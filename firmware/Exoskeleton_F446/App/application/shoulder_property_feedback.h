#ifndef SHOULDER_PROPERTY_FEEDBACK_H
#define SHOULDER_PROPERTY_FEEDBACK_H
/* Main-loop receive provider, not a CAN sender or output permit. The future
 * single TX owner must serialize reads with the gateway and diagnostics. */
#include "gds_sdo_read.h"
typedef struct {
    GdsPropertyValue value[GDS_PROPERTY_COUNT];
    uint32_t requested_ms[GDS_PROPERTY_COUNT], received_ms[GDS_PROPERTY_COUNT];
    uint32_t sequence; /* Complete published cycles only; queries never advance it. */
    bool valid;
} ShoulderPropertySnapshot;
typedef struct {
    ShoulderPropertySnapshot published, working;
    GdsPropertyField next;
    uint32_t request_ms, last_reply_ms, reply_timeout_ms;
    bool pending, quarantined, has_reply;
} ShoulderPropertyFeedback;
/* Cold initialization only; zero timeout leaves the provider disabled. */
void ShoulderPropertyFeedback_Init(ShoulderPropertyFeedback *reader, uint32_t reply_timeout_ms);
/* Call ONLY after successful submission of exactly this read by the exclusive
 * TX owner. Constructing a request alone is not submission evidence. */
bool ShoulderPropertyFeedback_Expect(ShoulderPropertyFeedback *reader,
                                    GdsPropertyField field, uint32_t submitted_ms);
void ShoulderPropertyFeedback_Receive(ShoulderPropertyFeedback *reader,
                                     BoardCanBus bus, const BoardCanFrame *frame, uint32_t now_ms);
void ShoulderPropertyFeedback_Poll(ShoulderPropertyFeedback *reader, uint32_t now_ms);
/* Failure is latched for the boot; late/repeated responses cannot recover it. */
void ShoulderPropertyFeedback_Invalidate(ShoulderPropertyFeedback *reader);
bool ShoulderPropertyFeedback_Get(const ShoulderPropertyFeedback *reader,
                                 uint32_t now_ms, uint32_t freshness_ms, ShoulderPropertySnapshot *out);
#endif
