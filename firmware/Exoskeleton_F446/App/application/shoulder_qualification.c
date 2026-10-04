/* Fixed seven-property read, once per cold boot, with no retries or output TX. */
#include "shoulder_qualification.h"
#include "shoulder_read_diagnostic.h"
#include "board_can_tx.h"
#include "board_console.h"
#include "can_monitor.h"
#include "s3_bench.h"
#include "s2_config.h"
#include <stddef.h>
#include <string.h>
static ShoulderQualificationStatus status;

static ShoulderQualificationReason health(uint32_t now_ms, BoardCanStats *can, GdsFeedback *hb)
{
    S3BenchStatus gate; S3Bench_GetStatus(&gate);
    if (gate.state != OUTPUT_SAFE_OFF || gate.configured || gate.transport_bound
        || gate.output_allowed || gate.request_valid || gate.arm_token != 0U) {
        return SHOULDER_QUAL_GATE;
    }
    bool wire;
    if (BoardConsole_HasFault()
        || !BoardCan_GetStats(BOARD_CAN_SHOULDER, can) || !can->started
        || can->start_error != BOARD_CAN_START_OK || can->bus_off || can->errors != 0U
        || can->dropped != 0U || can->invalid != 0U || can->fifo_overruns != 0U
        || !CanMonitor_GetShoulderObservation(hb, &wire) || !hb->has_heartbeat
        || !hb->life_contiguous || hb->axis_state != 1U || hb->axis_error != 0U
        || hb->fault_flags != 0U
        || (uint32_t)(now_ms - hb->heartbeat_ms) > S2_GDS_OBSERVATION_TIMEOUT_MS) {
        return SHOULDER_QUAL_HEALTH;
    }
    return SHOULDER_QUAL_NONE; /* 029 estimate/wire validity is not a prerequisite. */
}
static void finish(ShoulderQualificationState state, ShoulderQualificationReason reason, uint32_t now_ms)
{
    status.state = state; status.reason = reason; status.finished_ms = now_ms;
    status.pending = false;
    /* No IDLE/enable/torque frame, no shared mailbox purge, and no owner release. */
    status.cancel_accepted = BoardCanTx_CancelShoulderRead();
    if (!status.cancel_accepted) {
        status.state = SHOULDER_QUAL_FAILED;
        if (reason == SHOULDER_QUAL_NONE || reason == SHOULDER_QUAL_CANCEL) {
            status.reason = SHOULDER_QUAL_CANCEL_TX;
        }
    }
}
static bool running(uint32_t now_ms, BoardCanStats *can, GdsFeedback *hb)
{
    if (status.state != SHOULDER_QUAL_ACTIVE) { return false; }
    if ((uint32_t)(now_ms - status.started_ms) >= SHOULDER_QUAL_WINDOW_MS) {
        finish(SHOULDER_QUAL_FAILED, SHOULDER_QUAL_WINDOW, now_ms); return false;
    }
    ShoulderQualificationReason reason = health(now_ms, can, hb);
    if (reason != SHOULDER_QUAL_NONE) {
        finish(SHOULDER_QUAL_FAILED, reason, now_ms); return false;
    }
    if (status.pending
        && (uint32_t)(now_ms - status.sample[status.completed].request_ms) >= SHOULDER_QUAL_REPLY_MS) {
        finish(SHOULDER_QUAL_FAILED, SHOULDER_QUAL_TIMEOUT, now_ms); return false;
    }
    return true;
}
static bool submit(uint32_t now_ms)
{
    ShoulderQualificationSample *s = &status.sample[status.completed];
    s->request_ms = now_ms;
    if (!GdsSdoRead_PropertyRequest(s->field, &s->request)) {
        finish(SHOULDER_QUAL_FAILED, SHOULDER_QUAL_TX, now_ms); return false;
    }
    status.attempts++; s->requested = BoardCanTx_SubmitShoulderRead(&s->request);
    if (!s->requested) {
        finish(SHOULDER_QUAL_FAILED, SHOULDER_QUAL_TX, now_ms); return false;
    }
    status.pending = true;
    return true;
}
void ShoulderQualification_Init(void)
{
    memset(&status, 0, sizeof(status));
    for (unsigned i = 0U; i < GDS_PROPERTY_COUNT; i++) {
        status.sample[i].field = (GdsPropertyField)i;
        status.sample[i].endpoint = GdsSdoRead_PropertyEndpoint((GdsPropertyField)i);
    }
}
void ShoulderQualification_GetStatus(ShoulderQualificationStatus *out)
{ if (out != NULL) { *out = status; } }
bool ShoulderQualification_Start(uint32_t now_ms)
{
    if (status.state != SHOULDER_QUAL_IDLE || status.claimed) { return false; }
    BoardCanStats can; GdsFeedback hb;
    status.reason = health(now_ms, &can, &hb);
    if (status.reason != SHOULDER_QUAL_NONE) { return false; }
    if (can.queued != 0U || !BoardCanTx_ShoulderReadReady()) {
        status.reason = SHOULDER_QUAL_TX; return false;
    }
    if (!ShoulderRead_ClaimQualification()) { status.reason = SHOULDER_QUAL_OWNED; return false; }
    status.claimed = status.first_idle = true; status.first_heartbeat = hb;
    status.started_ms = now_ms; status.state = SHOULDER_QUAL_ACTIVE;
    return submit(now_ms);
}
void ShoulderQualification_Receive(BoardCanBus bus, const BoardCanFrame *f, uint32_t now_ms)
{
    if (status.state != SHOULDER_QUAL_ACTIVE || bus != BOARD_CAN_SHOULDER || f == NULL) { return; }
    bool sdo = (f->id & 0x1FU) == 0x005U;
    /* Preserve the first observed candidate even when deadline/health rejects it. */
    if (sdo && status.pending) {
        ShoulderQualificationSample *s = &status.sample[status.completed];
        s->reply = *f; s->replied = true; s->reply_ms = f->received_ms;
        s->raw_value_bits = (uint32_t)f->data[4] | (uint32_t)f->data[5] << 8
            | (uint32_t)f->data[6] << 16 | (uint32_t)f->data[7] << 24;
    }
    BoardCanStats can; GdsFeedback hb;
    if (!running(now_ms, &can, &hb)) {
        status.has_unexpected = true; status.unexpected_frame = *f; return;
    }
    if (f->id == 0x021U && (f->dlc != 8U
        || (uint32_t)(now_ms - f->received_ms) > S2_GDS_OBSERVATION_TIMEOUT_MS)) {
        status.has_unexpected = true; status.unexpected_frame = *f;
        finish(SHOULDER_QUAL_FAILED, SHOULDER_QUAL_HEALTH, now_ms); return;
    }
    if (!sdo) { return; }
    if (!status.pending) {
        status.has_unexpected = true; status.unexpected_frame = *f;
        finish(SHOULDER_QUAL_FAILED, SHOULDER_QUAL_REPLY, now_ms); return;
    }
    ShoulderQualificationSample *s = &status.sample[status.completed];
    if ((uint32_t)(f->received_ms - s->request_ms) >= SHOULDER_QUAL_REPLY_MS
        || (uint32_t)(now_ms - f->received_ms) >= SHOULDER_QUAL_REPLY_MS
        || !GdsSdoRead_PropertyDecode(s->field, bus, f, &s->value)) {
        status.has_unexpected = true; status.unexpected_frame = *f;
        finish(SHOULDER_QUAL_FAILED, SHOULDER_QUAL_REPLY, now_ms); return;
    }
    s->decoded = true; status.completed_mask |= (uint8_t)(1U << status.completed);
    status.completed++; status.pending = false;
    /* The next read is submitted only by Poll after the RX batch is consumed.
     * Any duplicate/unsolicited SDO in that batch sees no pending transaction. */
}
void ShoulderQualification_Poll(uint32_t now_ms)
{
    BoardCanStats can; GdsFeedback hb;
    if (!running(now_ms, &can, &hb) || status.pending || can.queued != 0U) { return; }
    if (status.completed == GDS_PROPERTY_COUNT) {
        uint32_t after = hb.heartbeat_ms - status.sample[GDS_PROPERTY_COUNT - 1U].reply_ms;
        if (after == 0U || after >= UINT32_C(0x80000000)) { return; }
        status.last_idle = true; status.last_heartbeat = hb;
        finish(SHOULDER_QUAL_COMPLETE, SHOULDER_QUAL_NONE, now_ms); return;
    }
    if (!BoardCanTx_ShoulderReadReady()) {
        finish(SHOULDER_QUAL_FAILED, SHOULDER_QUAL_TX, now_ms); return;
    }
    (void)submit(now_ms);
}
bool ShoulderQualification_Cancel(uint32_t now_ms)
{
    if (status.state != SHOULDER_QUAL_ACTIVE) { return false; }
    finish(SHOULDER_QUAL_CANCELLED, SHOULDER_QUAL_CANCEL, now_ms);
    return status.cancel_accepted;
}
