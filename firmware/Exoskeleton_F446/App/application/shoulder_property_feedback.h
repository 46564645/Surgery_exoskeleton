#ifndef SHOULDER_PROPERTY_FEEDBACK_H
#define SHOULDER_PROPERTY_FEEDBACK_H
/* Main-loop receive provider, not a CAN sender or output permit. The future
 * single TX owner must serialize reads with the gateway and diagnostics. */
#include "gds_sdo_read.h"
typedef struct {
    GdsPropertyValue value[GDS_PROPERTY_COUNT];
    uint32_t requested_ms[GDS_PROPERTY_COUNT], received_ms[GDS_PROPERTY_COUNT];
    uint32_t sequence; /* S3/S4 full cycles or S5 pairs; queries never advance it. */
    bool valid, zero_proof_valid, mode_pair_valid;
} ShoulderPropertySnapshot;
typedef struct {
    ShoulderPropertySnapshot published, working;
    GdsPropertyField next;
    uint32_t request_ms, last_reply_ms, reply_timeout_ms;
    bool pending, quarantined, has_reply;
    bool s5_fast_path, s5_bootstrap_complete;
    bool zero_proof_requested, mode_proof_requested;
    uint8_t reply_retries; /* Consecutive timeouts before the reader latches. */
} ShoulderPropertyFeedback;
/* Cold initialization only; zero timeout leaves the provider disabled. */
void ShoulderPropertyFeedback_Init(ShoulderPropertyFeedback *reader, uint32_t reply_timeout_ms);
/* S5 only: first publish a full seven-field proof, then publish each
 * associated position/velocity pair. S3/S4 retain full-cycle publication. */
void ShoulderPropertyFeedback_EnableS5FastPath(ShoulderPropertyFeedback *reader);
/* Queue a fresh property read after the current pair. These calls never send
 * CAN frames; the exclusive TX owner submits the next requested field. */
bool ShoulderPropertyFeedback_RequestZeroProof(ShoulderPropertyFeedback *reader);
bool ShoulderPropertyFeedback_RequestModeProof(ShoulderPropertyFeedback *reader);
/* Call ONLY after successful submission of exactly this read by the exclusive
 * TX owner. Constructing a request alone is not submission evidence. */
bool ShoulderPropertyFeedback_Expect(ShoulderPropertyFeedback *reader,
                                    GdsPropertyField field, uint32_t submitted_ms);
void ShoulderPropertyFeedback_Receive(ShoulderPropertyFeedback *reader,
                                     BoardCanBus bus, const BoardCanFrame *frame, uint32_t now_ms);
void ShoulderPropertyFeedback_Poll(ShoulderPropertyFeedback *reader, uint32_t now_ms);
/* Failure is latched for the boot; late/repeated responses cannot recover it. */
void ShoulderPropertyFeedback_Invalidate(ShoulderPropertyFeedback *reader);
/* Bounded retry budget: a single lost reply re-requests the same field and
 * only repeated timeouts latch the reader (CANopen SDO retry practice). */
#define SHOULDER_PROPERTY_MAX_RETRIES 4U
bool ShoulderPropertyFeedback_Get(const ShoulderPropertyFeedback *reader,
                                 uint32_t now_ms, uint32_t freshness_ms, ShoulderPropertySnapshot *out);
#endif
