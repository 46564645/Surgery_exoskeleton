/* L4: main-loop-only state; bounded consumption keeps the console responsive. */
#include "can_monitor.h"
#include "s2_config.h"
#include "board_runtime.h"
#include "c620_feedback.h"
#include "shoulder_read_diagnostic.h"
#include "shoulder_qualification.h"
#include "encoder_stream.h"
#include "s3_site.h"
#include "s3_shoulder_service.h"
#include "board_can_tx.h"
#include <stddef.h>
#include <string.h>
static BoardCanFrame last[BOARD_CAN_COUNT];
static bool seen[BOARD_CAN_COUNT];
static C620Device elbow;
static C620Standstill elbow_standstill;
static GdsDevice shoulder_observation;
static ShoulderPropertyFeedback shoulder_properties;
_Static_assert(S2_C620_ID >= 1U && S2_C620_ID <= 8U, "C620 ID range");
_Static_assert(S2_C620_TIMEOUT_MS > 0U && S2_C620_TIMEOUT_MS < 0x80000000U,
               "C620 timeout range");

void CanMonitor_Init(const S3SiteProfile *site, ShoulderReaderMode mode)
{
    ShoulderPropertyFeedback_Init(&shoulder_properties,
        site != NULL && site->shoulder_source == S3_SHOULDER_ENCODER_PROPERTIES
            ? site->shoulder_property_reply_ms : 0U);
    ShoulderRead_Init();
    ShoulderQualification_Init();
    EncoderStream_Init();
    C620Standstill_Reset(&elbow_standstill);
    memset(last, 0, sizeof(last));
    memset(seen, 0, sizeof(seen));
    (void)C620_Init(&elbow, S2_C620_ID, S2_C620_TIMEOUT_MS);
    (void)GdsFeedback_Init(&shoulder_observation, S2_GDS_NODE,
        GDS_PROFILE_V22_FW_GE_0513, S2_GDS_OBSERVATION_TIMEOUT_MS);
    (void)BoardCan_Start(); /* Per-bus failure remains queryable. */
    S3ShoulderService_Init(site, mode);
}
void CanMonitor_Poll(void)
{
    for (unsigned bus = 0U; bus < BOARD_CAN_COUNT; bus++) {
        for (unsigned i = 0U; i < S2_CAN_POLL_BUDGET; i++) {
            BoardCanFrame frame;
            if (!BoardCan_Read((BoardCanBus)bus, &frame)) { break; }
            last[bus] = frame;
            seen[bus] = true;
            if (bus == BOARD_CAN_SHOULDER) {
                (void)GdsFeedback_Receive(&shoulder_observation, (BoardCanBus)bus,
                                          &frame, BoardRuntime_NowMs());
                ShoulderQualification_Receive((BoardCanBus)bus, &frame, BoardRuntime_NowMs());
                S3ShoulderService_Observe(&frame, BoardRuntime_NowMs());
                if (frame.id == 0x025U && shoulder_properties.pending
                    && BoardCanTx_ShoulderReadResult() != BOARD_CAN_READ_TX_SENT) {
                    /* A same-field late/stray reply must not certify a request
                     * that lost arbitration and was never put on the wire. */
                    ShoulderPropertyFeedback_Invalidate(&shoulder_properties);
                } else {
                    ShoulderPropertyFeedback_Receive(&shoulder_properties, (BoardCanBus)bus,
                        &frame, BoardRuntime_NowMs());
                }
                ShoulderRead_Receive((BoardCanBus)bus, &frame, BoardRuntime_NowMs());
                EncoderStream_Receive((BoardCanBus)bus, &frame, BoardRuntime_NowMs());
            } else if (bus == BOARD_CAN_ELBOW) {
                uint32_t now_ms = BoardRuntime_NowMs();
                if (C620_Receive(&elbow, (BoardCanBus)bus, &frame, now_ms)) {
                    C620Feedback sample;
                    (void)C620_GetFeedback(&elbow, now_ms, &sample);
                    C620Standstill_Observe(&elbow_standstill, &sample, now_ms);
                } else if (frame.id == (uint16_t)(0x200U + S2_C620_ID)) {
                    C620Standstill_Observe(&elbow_standstill, NULL, now_ms);
                }
            }
        }
    }
    C620_Poll(&elbow, BoardRuntime_NowMs());
    ShoulderPropertyFeedback_Poll(&shoulder_properties, BoardRuntime_NowMs());
    ShoulderRead_Poll(BoardRuntime_NowMs());
    ShoulderQualification_Poll(BoardRuntime_NowMs());
    EncoderStream_Poll(BoardRuntime_NowMs());
    (void)C620Standstill_Read(&elbow_standstill, BoardRuntime_NowMs());
}
void CanMonitor_ResetElbowStandstill(uint32_t after_ms)
{ C620Standstill_After(&elbow_standstill, after_ms); }
C620StandstillStatus CanMonitor_GetElbowStandstill(uint32_t now_ms)
{ return C620Standstill_Read(&elbow_standstill, now_ms); }
bool CanMonitor_GetLast(BoardCanBus bus, BoardCanFrame *out)
{
    if ((unsigned)bus >= BOARD_CAN_COUNT || out == NULL || !seen[bus]) { return false; }
    *out = last[bus];
    return true;
}
bool CanMonitor_GetElbowFeedback(C620Feedback *out)
{
    return C620_GetFeedback(&elbow, BoardRuntime_NowMs(), out);
}

bool CanMonitor_GetShoulderObservation(GdsFeedback *out, bool *wire_valid)
{
    if (out == NULL || wire_valid == NULL || !shoulder_observation.configured) { return false; }
    *wire_valid = GdsFeedback_Get(&shoulder_observation, BoardRuntime_NowMs(), out);
    return true;
}

bool CanMonitor_ExpectShoulderProperty(GdsPropertyField field, uint32_t submitted_ms)
{ return ShoulderPropertyFeedback_Expect(&shoulder_properties, field, submitted_ms); }
void CanMonitor_InvalidateShoulderProperties(void)
{ ShoulderPropertyFeedback_Invalidate(&shoulder_properties); }
bool CanMonitor_GetShoulderProperties(uint32_t now_ms, uint32_t freshness_ms, ShoulderPropertySnapshot *out)
{ return ShoulderPropertyFeedback_Get(&shoulder_properties, now_ms, freshness_ms, out); }

bool CanMonitor_NextShoulderProperty(GdsPropertyField *field, bool *pending)
{
    if (field == NULL || pending == NULL || shoulder_properties.reply_timeout_ms == 0U
        || shoulder_properties.quarantined) { return false; }
    *field = shoulder_properties.next; *pending = shoulder_properties.pending; return true;
}
