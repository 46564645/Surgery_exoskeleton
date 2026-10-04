/* L4: bounded continuous use of the two schema-bound, hardware-proven L2 reads.
 * The legacy 029 observation and the S3 production permit provider are separate.
 * No allocation, software TX backlog, ISR decoding, CAN2 TX or mode writes. */
#include "encoder_stream.h"
#include "shoulder_read_diagnostic.h"
#include "board_can_tx.h"
#include "board_console.h"
#include "can_monitor.h"
#include "s2_config.h"
#include "s3_bench.h"
#include <math.h>
#include <stddef.h>
#include <string.h>
typedef enum { NEED_POSITION, WAIT_POSITION, NEED_VELOCITY, WAIT_VELOCITY, WAIT_PERIOD } Phase;
static EncoderStreamStatus status;
static Phase phase;
static BoardCanStats baseline;
static uint32_t request_ms, cycle_ms, step_ms;
static BoardCanFrame working_position;
static float working_value;
_Static_assert(ENCODER_STREAM_MAX_REQUESTS == 2U * ENCODER_STREAM_DURATION_MS / ENCODER_STREAM_PERIOD_MS,
               "stream request bound");
static EncoderStreamReason health(uint32_t now_ms, BoardCanStats *can)
{
    if (BoardConsole_HasFault()) { return ENCODER_STREAM_CONSOLE; }
    S3BenchStatus gate; S3Bench_GetStatus(&gate);
    if (gate.state != OUTPUT_SAFE_OFF || gate.configured || gate.transport_bound
        || gate.output_allowed || gate.request_valid || gate.arm_token != 0U) { return ENCODER_STREAM_OWNER; }
    GdsFeedback heartbeat; bool wire;
    if (!CanMonitor_GetShoulderObservation(&heartbeat, &wire)
        || !heartbeat.has_heartbeat || !heartbeat.life_contiguous || heartbeat.axis_state != 1U
        || heartbeat.axis_error != 0U || heartbeat.fault_flags != 0U
        || (uint32_t)(now_ms - heartbeat.heartbeat_ms) > ENCODER_STREAM_HEARTBEAT_MS) {
        return ENCODER_STREAM_HEARTBEAT;
    }
    if (!BoardCan_GetStats(BOARD_CAN_SHOULDER, can) || !can->started
        || can->start_error != BOARD_CAN_START_OK || can->bus_off) { return ENCODER_STREAM_CAN; }
    if (status.active && (can->errors != baseline.errors || can->dropped != baseline.dropped
        || can->invalid != baseline.invalid || can->fifo_overruns != baseline.fifo_overruns)) {
        return ENCODER_STREAM_CAN;
    }
    return ENCODER_STREAM_NONE;
}
static void finish(EncoderStreamReason reason, uint32_t now_ms)
{
    status.active = status.pending = status.valid = false;
    status.reason = reason; status.stopped_ms = now_ms;
    /* A previously accepted read can still arrive. Do not abort shared HAL
     * mailboxes or turn late traffic into a new session/sample. */
}
void EncoderStream_Init(void)
{
    memset(&status, 0, sizeof(status)); memset(&baseline, 0, sizeof(baseline));
    memset(&working_position, 0, sizeof(working_position));
    working_value = 0.0f; request_ms = cycle_ms = step_ms = 0U; phase = NEED_POSITION;
}
bool EncoderStream_Start(uint32_t now_ms)
{
    BoardCanStats can;
    if (status.used || health(now_ms, &can) != ENCODER_STREAM_NONE || can.queued != 0U
        || !ShoulderRead_ClaimStream()) { return false; }
    baseline = can; status.used = status.active = true;
    status.started_ms = cycle_ms = step_ms = now_ms; phase = NEED_POSITION;
    EncoderStream_Poll(now_ms);
    return status.active;
}
void EncoderStream_Stop(uint32_t now_ms)
{ if (status.active) { finish(ENCODER_STREAM_USER, now_ms); } }
void EncoderStream_GetStatus(uint32_t now_ms, EncoderStreamStatus *out)
{
    if (out == NULL) { return; }
    *out = status;
    out->elapsed_ms = status.used ? (status.active ? now_ms : status.stopped_ms) - status.started_ms : 0U;
    if (out->has_pair) {
        uint32_t position_age = now_ms - out->position_ms, velocity_age = now_ms - out->velocity_ms;
        out->age_ms = position_age > velocity_age ? position_age : velocity_age;
    }
    BoardCanStats can;
    out->valid = out->active && out->has_pair && out->elapsed_ms < ENCODER_STREAM_DURATION_MS
        && out->age_ms <= ENCODER_STREAM_FRESH_MS && health(now_ms, &can) == ENCODER_STREAM_NONE;
}
void EncoderStream_Receive(BoardCanBus bus, const BoardCanFrame *f, uint32_t now_ms)
{
    if (!status.active || bus != BOARD_CAN_SHOULDER || f == NULL || (f->id & 0x1FU) != 0x005U) { return; }
    status.last_sdo = *f;
    if ((uint32_t)(now_ms - status.started_ms) >= ENCODER_STREAM_DURATION_MS) {
        finish(ENCODER_STREAM_LIMIT, now_ms); return;
    }
    BoardCanStats can; EncoderStreamReason reason = health(now_ms, &can);
    if (reason != ENCODER_STREAM_NONE) { finish(reason, now_ms); return; }
    if (status.pending && (uint32_t)(now_ms - request_ms) > ENCODER_STREAM_REPLY_MS) {
        finish(ENCODER_STREAM_TIMEOUT, now_ms); return;
    }
    GdsReadField field = phase == WAIT_POSITION ? GDS_READ_POSITION : GDS_READ_VELOCITY;
    float value;
    if (!status.pending || (phase != WAIT_POSITION && phase != WAIT_VELOCITY)
        || (uint32_t)(f->received_ms - request_ms) > ENCODER_STREAM_REPLY_MS
        || (uint32_t)(now_ms - f->received_ms) > ENCODER_STREAM_REPLY_MS
        || !GdsSdoRead_Decode(field, bus, f, &value) || !isfinite(value * 6.2831853071795864769f)) {
        status.rejected++; finish(ENCODER_STREAM_REPLY, now_ms); return;
    }
    status.pending = false;
    if (field == GDS_READ_POSITION) {
        working_position = *f; working_value = value; phase = NEED_VELOCITY; step_ms = now_ms;
    } else {
        if ((uint32_t)(f->received_ms - working_position.received_ms) > ENCODER_STREAM_FRESH_MS) {
            finish(ENCODER_STREAM_STALE, now_ms); return;
        }
        /* Publish an entire same-cycle pair. Never mix a new position with the
         * previous cycle's velocity if the second response is lost. */
        status.position_rev = working_value; status.velocity_rev_s = value;
        status.position_rad = working_value * 6.2831853071795864769f;
        status.velocity_rad_s = value * 6.2831853071795864769f;
        status.position_ms = working_position.received_ms; status.velocity_ms = f->received_ms;
        status.position_frame = working_position; status.velocity_frame = *f;
        status.has_pair = true; status.pairs++; phase = WAIT_PERIOD;
    }
}
void EncoderStream_Poll(uint32_t now_ms)
{
    if (!status.active) { return; }
    if ((uint32_t)(now_ms - status.started_ms) >= ENCODER_STREAM_DURATION_MS) {
        finish(ENCODER_STREAM_LIMIT, now_ms); return;
    }
    BoardCanStats can; EncoderStreamReason reason = health(now_ms, &can);
    if (reason != ENCODER_STREAM_NONE) { finish(reason, now_ms); return; }
    if (status.pending) {
        if ((uint32_t)(now_ms - request_ms) > ENCODER_STREAM_REPLY_MS) { finish(ENCODER_STREAM_TIMEOUT, now_ms); }
        return;
    }
    if (status.has_pair && ((uint32_t)(now_ms - status.position_ms) > ENCODER_STREAM_FRESH_MS
        || (uint32_t)(now_ms - status.velocity_ms) > ENCODER_STREAM_FRESH_MS)) {
        finish(ENCODER_STREAM_STALE, now_ms); return;
    }
    if (phase == WAIT_PERIOD) {
        if ((uint32_t)(now_ms - cycle_ms) < ENCODER_STREAM_PERIOD_MS) { return; }
        phase = NEED_POSITION; step_ms = now_ms;
    }
    if (can.queued != 0U) {
        if ((uint32_t)(now_ms - step_ms) > ENCODER_STREAM_REPLY_MS) { finish(ENCODER_STREAM_CAN, now_ms); }
        return; /* Empty input queue before each request; never catch up a backlog. */
    }
    if (status.requests >= ENCODER_STREAM_MAX_REQUESTS) { finish(ENCODER_STREAM_LIMIT, now_ms); return; }
    GdsReadField field = phase == NEED_POSITION ? GDS_READ_POSITION : GDS_READ_VELOCITY;
    BoardCanFrame frames[2] = {{0}, {0}};
    if (!GdsSdoRead_Request(field, &frames[0])) { finish(ENCODER_STREAM_REPLY, now_ms); return; }
    if (field == GDS_READ_POSITION) { cycle_ms = now_ms; }
    phase = field == GDS_READ_POSITION ? WAIT_POSITION : WAIT_VELOCITY;
    request_ms = now_ms; status.pending = true; status.requests++;
    if (!BoardCanTx_SubmitAxes(frames, 1U, false)) { finish(ENCODER_STREAM_TX, now_ms); }
}
