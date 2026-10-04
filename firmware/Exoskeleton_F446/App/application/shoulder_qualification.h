#ifndef SHOULDER_QUALIFICATION_H
#define SHOULDER_QUALIFICATION_H
/* L4 read-only endpoint qualification. Never enables or prepares an axis. */
#include "gds_sdo_read.h"
#include "gds_feedback.h"
#define SHOULDER_QUAL_WINDOW_MS 2000U
#define SHOULDER_QUAL_REPLY_MS 250U
typedef enum {
    SHOULDER_QUAL_IDLE, SHOULDER_QUAL_ACTIVE, SHOULDER_QUAL_COMPLETE,
    SHOULDER_QUAL_FAILED, SHOULDER_QUAL_CANCELLED
} ShoulderQualificationState;
typedef enum {
    SHOULDER_QUAL_NONE, SHOULDER_QUAL_GATE, SHOULDER_QUAL_HEALTH,
    SHOULDER_QUAL_OWNED, SHOULDER_QUAL_TX, SHOULDER_QUAL_REPLY,
    SHOULDER_QUAL_TIMEOUT, SHOULDER_QUAL_WINDOW, SHOULDER_QUAL_CANCEL,
    SHOULDER_QUAL_CANCEL_TX
} ShoulderQualificationReason;
typedef struct {
    GdsPropertyField field;
    uint16_t endpoint;
    bool requested, replied, decoded; /* Accepted TX, observed raw reply, valid decode. */
    uint32_t request_ms, reply_ms, raw_value_bits;
    GdsPropertyValue value;
    BoardCanFrame request, reply;
} ShoulderQualificationSample;
typedef struct {
    ShoulderQualificationState state;
    ShoulderQualificationReason reason;
    unsigned attempts, completed; /* Read submit attempts and decoded replies. */
    uint8_t completed_mask;
    uint32_t started_ms, finished_ms;
    bool claimed, pending, first_idle, last_idle, cancel_accepted;
    GdsFeedback first_heartbeat, last_heartbeat;
    ShoulderQualificationSample sample[GDS_PROPERTY_COUNT];
    bool has_unexpected;
    BoardCanFrame unexpected_frame;
} ShoulderQualificationStatus;
/* Init is cold-boot initialization only; no CLI recovery or owner transfer.
 * One isolated node1 CAN master is required: SDO has no transaction token.
 * Budgets above bound diagnostic traffic only, never motion or stop behavior.
 */
void ShoulderQualification_Init(void);
bool ShoulderQualification_Start(uint32_t now_ms);
void ShoulderQualification_Receive(BoardCanBus bus, const BoardCanFrame *frame, uint32_t now_ms);
void ShoulderQualification_Poll(uint32_t now_ms);
bool ShoulderQualification_Cancel(uint32_t now_ms);
void ShoulderQualification_GetStatus(ShoulderQualificationStatus *out);
#endif
