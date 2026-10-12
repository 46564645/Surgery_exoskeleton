/* L4 bounded bench diagnostic. Reuses L1 SubmitAxes(mask1); never owns RX. */
#include "shoulder_read_diagnostic.h"
#include "board_can_tx.h"
#include "board_console.h"
#include "can_monitor.h"
#include "s2_config.h"
#include "s3_bench.h"
#include <stddef.h>
#include <string.h>
static ShoulderReadStatus status;
void ShoulderRead_Init(void) { memset(&status, 0, sizeof(status)); }
void ShoulderRead_GetStatus(ShoulderReadStatus *out)
{ if (out != NULL) { *out = status; } }
static bool claim(ShoulderReadPhase owner)
{
    if (status.phase != SHOULDER_READ_IDLE || status.sequence != 0U
        || status.used_fields != 0U || status.has_raw || status.quarantined) { return false; }
    status.phase = owner; status.used_fields = 3U;
    return true;
}
bool ShoulderRead_ClaimStream(void) { return claim(SHOULDER_READ_STREAM_OWNED); }
bool ShoulderRead_ClaimS3(void) { return claim(SHOULDER_READ_S3_OWNED); }
bool ShoulderRead_ClaimS4(void) { return claim(SHOULDER_READ_S4_OWNED); }
bool ShoulderRead_ClaimQualification(void) { return claim(SHOULDER_READ_QUALIFICATION_OWNED); }
bool ShoulderRead_Start(GdsReadField field, uint32_t now_ms)
{
    BoardCanFrame frames[2] = {{0}, {0}};
    if (!GdsSdoRead_Request(field, &frames[0])) { return false; }
    uint8_t bit = (uint8_t)(1U << (unsigned)field);
    if (status.phase == SHOULDER_READ_PENDING || status.phase == SHOULDER_READ_STREAM_OWNED
        || status.phase == SHOULDER_READ_S3_OWNED || status.phase == SHOULDER_READ_S4_OWNED
        || status.phase == SHOULDER_READ_QUALIFICATION_OWNED
        || status.quarantined
        || (status.used_fields & bit) != 0U || BoardConsole_HasFault()) { return false; }
    S3BenchStatus gate; S3Bench_GetStatus(&gate);
    if (gate.state != OUTPUT_SAFE_OFF || gate.configured || gate.transport_bound
        || gate.output_allowed || gate.request_valid || gate.arm_token != 0U) { return false; }
    GdsFeedback heartbeat; bool wire;
    if (!CanMonitor_GetShoulderObservation(&heartbeat, &wire)
        || !heartbeat.has_heartbeat || !heartbeat.life_contiguous
        || heartbeat.axis_state != 1U || heartbeat.axis_error != 0U
        || heartbeat.fault_flags != 0U
        || (uint32_t)(now_ms - heartbeat.heartbeat_ms) > S2_GDS_OBSERVATION_TIMEOUT_MS) {
        return false;
    }
    /* Wire estimate validity deliberately isn't a prerequisite: 029 is the
     * source under diagnosis. Historical cumulative error counters are retained
     * by canstatus; current bus-off/start failure still blocks this read. */
    BoardCanStats can;
    if (!BoardCan_GetStats(BOARD_CAN_SHOULDER, &can) || !can.started
        || can.start_error != BOARD_CAN_START_OK || can.bus_off || can.queued != 0U) { return false; }
    uint8_t used = (uint8_t)(status.used_fields | bit);
    uint32_t sequence = status.sequence + 1U;
    memset(&status, 0, sizeof(status));
    status.field = field; status.sequence = sequence; status.used_fields = used;
    status.sent_ms = now_ms; status.phase = SHOULDER_READ_PENDING;
    status.accepted = BoardCanTx_SubmitAxes(frames, 1U, false);
    if (!status.accepted) {
        status.phase = SHOULDER_READ_TX_FAILED; status.quarantined = true;
    }
    return status.accepted;
}
void ShoulderRead_Receive(BoardCanBus bus, const BoardCanFrame *f, uint32_t now_ms)
{
    if (bus != BOARD_CAN_SHOULDER || f == NULL || (f->id & 0x1FU) != 0x005U) { return; }
    status.raw = *f; status.has_raw = true;
    if (status.phase != SHOULDER_READ_PENDING) { return; }
    float value;
    if ((uint32_t)(now_ms - status.sent_ms) > SHOULDER_READ_TIMEOUT_MS
        || (uint32_t)(f->received_ms - status.sent_ms) > SHOULDER_READ_TIMEOUT_MS
        || (uint32_t)(now_ms - f->received_ms) > SHOULDER_READ_TIMEOUT_MS
        || !GdsSdoRead_Decode(status.field, bus, f, &value)) {
        status.rejected++; return;
    }
    /* Same-ms roundtrips are legitimate. Empty queue at submission and the
     * once-per-endpoint contract prevent reuse of an earlier local request. */
    status.value = value; status.has_value = true; status.matched = *f;
    status.received_ms = f->received_ms; status.phase = SHOULDER_READ_RECEIVED;
}
void ShoulderRead_Poll(uint32_t now_ms)
{
    if (status.phase == SHOULDER_READ_PENDING
        && (uint32_t)(now_ms - status.sent_ms) > SHOULDER_READ_TIMEOUT_MS) {
        status.phase = SHOULDER_READ_TIMEOUT; status.quarantined = true;
    }
}
