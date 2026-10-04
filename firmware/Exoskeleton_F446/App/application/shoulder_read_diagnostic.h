#ifndef SHOULDER_READ_DIAGNOSTIC_H
#define SHOULDER_READ_DIAGNOSTIC_H
#include "gds_sdo_read.h"
#define SHOULDER_READ_TIMEOUT_MS 250U
typedef enum {
    SHOULDER_READ_IDLE, SHOULDER_READ_PENDING, SHOULDER_READ_RECEIVED,
    SHOULDER_READ_TIMEOUT, SHOULDER_READ_TX_FAILED, SHOULDER_READ_STREAM_OWNED,
    SHOULDER_READ_S3_OWNED, SHOULDER_READ_QUALIFICATION_OWNED
} ShoulderReadPhase;
typedef struct {
    ShoulderReadPhase phase;
    GdsReadField field;
    uint32_t sequence, sent_ms, received_ms, rejected;
    float value;
    bool accepted, has_value, has_raw, quarantined;
    uint8_t used_fields;
    BoardCanFrame matched, raw;
} ShoulderReadStatus;
/* Main loop only. One request per field per MCU boot; no retries or polling TX.
 * CAN SDO has no transaction token. Empty RX queue and an isolated single CAN
 * master are required; local sequence is only a log identifier.
 */
void ShoulderRead_Init(void);
bool ShoulderRead_Start(GdsReadField field, uint32_t now_ms);
void ShoulderRead_Receive(BoardCanBus bus, const BoardCanFrame *frame, uint32_t now_ms);
void ShoulderRead_Poll(uint32_t now_ms);
void ShoulderRead_GetStatus(ShoulderReadStatus *out);
/* Owners may claim a pristine boot only. Claim persists after they stop;
 * no mode may reset/reuse another's untagged SDO transaction history. */
bool ShoulderRead_ClaimStream(void);
bool ShoulderRead_ClaimS3(void);
bool ShoulderRead_ClaimQualification(void);
#endif
