/* One main-loop SDO owner. The gateway alone owns enable/output/stop writes. */
#include "s3_shoulder_service.h"
#include "can_monitor.h"
#include "shoulder_read_diagnostic.h"
#include "board_can_tx.h"
#include "board_console.h"
#include "s2_config.h"
#include <stddef.h>
static const S3SiteProfile *profile;
static S3ReaderStatus status;
static ShoulderStandstill standstill;
static uint32_t started_ms, cycle_ms;
static bool have_cycle, cancellation_failed;
static bool duration(uint32_t t) { return t > 0U && t < UINT32_C(0x80000000); }
static bool bench(void)
{ return profile != NULL && profile->safety.stop_policy == SAFETY_STOP_SUPERVISED_SHOULDER_BENCH; }
bool S3ShoulderService_ProfileReady(const S3SiteProfile *p)
{
    if (p == NULL) { return false; }
    bool stop_ready = p->safety.stop_policy == SAFETY_STOP_EXTERNAL_INPUT
        && p->stop_input_confirmed
        && S3StopInput_ConfigReady(&p->operator_stop_input, &p->independent_stop_input);
    if (p->safety.stop_policy == SAFETY_STOP_SUPERVISED_SHOULDER_BENCH) {
        stop_ready = ShoulderStandstill_ConfigValid(&p->shoulder_standstill)
            && duration(p->shoulder_stop_window_ms)
            && p->shoulder_stop_window_ms > p->shoulder_standstill.dwell_ms
            && p->shoulder_standstill.freshness_ms <= p->safety.feedback_timeout_ms
            && p->shoulder_property_period_ms < p->shoulder_standstill.freshness_ms;
    }
    return p->shoulder_source == S3_SHOULDER_ENCODER_PROPERTIES
        && p->shoulder_node == S2_GDS_NODE && p->safety.enabled_axes == SAFETY_SHOULDER_AXES
        && SafetySupervisor_ConfigReady(&p->safety)
        && (p->trusted_feedback_axes & SAFETY_SHOULDER_AXES) != 0U
        && (p->coordinates[0] == S3_BENCH_RELATIVE_ROTOR
            ? p->safety.joint[0].relative_native_envelope && !p->rotor_reference_confirmed[0]
            : !p->safety.joint[0].relative_native_envelope && p->rotor_reference_confirmed[0]
                && p->coordinates[0] != S3_COORDINATE_UNSET)
        && stop_ready
        && duration(p->shoulder_property_reply_ms) && duration(p->shoulder_property_period_ms)
        && duration(p->shoulder_property_window_ms) && duration(p->prepare_timeout_ms)
        && p->shoulder_property_reply_ms <= p->safety.feedback_timeout_ms
        && p->shoulder_property_period_ms < p->safety.feedback_timeout_ms
        && p->shoulder_property_window_ms > p->prepare_timeout_ms;
}
void S3ShoulderService_Init(const S3SiteProfile *p)
{
    profile = p; have_cycle = cancellation_failed = false; started_ms = cycle_ms = 0U;
    status = (S3ReaderStatus){0};
    status.state = S3ShoulderService_ProfileReady(p) ? S3_READER_IDLE : S3_READER_DISABLED;
    ShoulderStandstill_Init(&standstill, p == NULL ? NULL : &p->shoulder_standstill);
}
S3ReaderStatus S3ShoulderService_Get(void) { return status; }
uint32_t S3ShoulderService_WindowRemaining(uint32_t now_ms)
{
    if (profile == NULL || (status.state != S3_READER_ACTIVE && status.state != S3_READER_OBSERVING)) { return 0U; }
    bool observing = status.state == S3_READER_OBSERVING;
    uint32_t age = now_ms - (observing ? status.stop_ms : started_ms);
    uint32_t limit = observing ? profile->shoulder_stop_window_ms : profile->shoulder_property_window_ms;
    return age < limit ? limit - age : 0U;
}
ShoulderStandstillStatus S3ShoulderService_GetStandstill(uint32_t now_ms)
{ return ShoulderStandstill_Read(&standstill, now_ms); }
bool S3ShoulderService_Stop(void)
{
    if (status.state == S3_READER_ACTIVE || status.state == S3_READER_IDLE
        || status.state == S3_READER_STOPPING || status.state == S3_READER_OBSERVING) {
        status.state = S3_READER_STOPPED;
    }
    CanMonitor_InvalidateShoulderProperties();
    bool stopped = BoardCanTx_CancelShoulderRead();
    if (!stopped) { cancellation_failed = true; }
    if (cancellation_failed) { status.state = S3_READER_FAULT; status.reason = S3_READ_TX; }
    return !cancellation_failed;
}
static void fail(S3ReadReason reason)
{
    status.state = S3_READER_FAULT; status.reason = reason;
    ShoulderStandstill_Observe(&standstill, NULL, 0U);
    (void)S3ShoulderService_Stop();
}
bool S3ShoulderService_BeforeOutputStop(void)
{
    if (bench() && status.state == S3_READER_ACTIVE && !cancellation_failed) {
        /* Keep an already submitted read paired with its response. No restart,
         * fresh reader, or new association can swallow an old in-flight reply. */
        status.state = S3_READER_STOPPING;
        return true;
    }
    return S3ShoulderService_Stop();
}
void S3ShoulderService_AfterOutputStop(bool accepted, uint32_t now_ms)
{
    if (status.state != S3_READER_STOPPING) { return; }
    if (!accepted) { fail(S3_READ_TX); return; }
    status.state = S3_READER_OBSERVING; status.stop_ms = now_ms;
    ShoulderStandstill_After(&standstill, now_ms);
}
static bool healthy(uint32_t now_ms, BoardCanStats *can)
{
    if (profile == NULL || BoardConsole_HasFault()
        || !BoardCan_GetStats(BOARD_CAN_SHOULDER, can) || !can->started
        || can->start_error != BOARD_CAN_START_OK || can->bus_off || can->errors != 0U
        || can->dropped != 0U || can->invalid != 0U || can->fifo_overruns != 0U) { return false; }
    GdsFeedback hb; bool wire;
    if (!CanMonitor_GetShoulderObservation(&hb, &wire) || !hb.has_heartbeat || !hb.life_contiguous
        || hb.axis_error != 0U || hb.fault_flags != 0U || (hb.axis_state != 1U && hb.axis_state != 8U)
        || (uint32_t)(now_ms - hb.heartbeat_ms) > profile->safety.feedback_timeout_ms) { return false; }
    if (bench()) { return true; } /* Explicit supervised policy; no invented GPIO. */
    S3StopObservation stop = S3StopInput_Read(&profile->operator_stop_input, &profile->independent_stop_input);
    return stop.available && stop.operator_stop_clear;
}
bool S3ShoulderService_Start(uint32_t now_ms)
{
    BoardCanStats can; GdsFeedback hb; bool wire;
    if (status.state != S3_READER_IDLE || !healthy(now_ms, &can) || can.queued != 0U
        || !CanMonitor_GetShoulderObservation(&hb, &wire) || hb.axis_state != 1U
        || !ShoulderRead_ClaimS3()) { return false; }
    status.state = S3_READER_ACTIVE; started_ms = cycle_ms = now_ms;
    have_cycle = false; return true;
}
void S3ShoulderService_Observe(const BoardCanFrame *f, uint32_t now_ms)
{
    if ((status.state != S3_READER_ACTIVE && status.state != S3_READER_OBSERVING)
        || f == NULL || f->id != 0x021U) { return; }
    BoardCanStats can;
    if (f->dlc != 8U || (uint32_t)(now_ms - f->received_ms) > profile->safety.feedback_timeout_ms
        || !healthy(now_ms, &can)) { fail(S3_READ_HEALTH); }
}
void S3ShoulderService_Poll(uint32_t now_ms)
{
    bool observing = status.state == S3_READER_OBSERVING;
    if (status.state != S3_READER_ACTIVE && !observing) { return; }
    if ((observing && (uint32_t)(now_ms - status.stop_ms) >= profile->shoulder_stop_window_ms)
        || (!observing && (uint32_t)(now_ms - started_ms) >= profile->shoulder_property_window_ms)) {
        fail(S3_READ_WINDOW); return;
    }
    BoardCanStats can;
    if (!healthy(now_ms, &can)) { fail(S3_READ_HEALTH); return; }
    GdsPropertyField next; bool pending;
    if (!CanMonitor_NextShoulderProperty(&next, &pending)) { fail(S3_READ_REPLY); return; }
    if (observing) {
        GdsFeedback hb; bool wire; ShoulderPropertySnapshot snapshot;
        (void)CanMonitor_GetShoulderObservation(&hb, &wire);
        uint32_t after = hb.heartbeat_ms - status.stop_ms;
        bool idle_after = hb.axis_state == 1U && after != 0U && after < UINT32_C(0x80000000);
        bool fresh = CanMonitor_GetShoulderProperties(now_ms, profile->shoulder_standstill.freshness_ms, &snapshot);
        ShoulderStandstill_Observe(&standstill, idle_after && fresh ? &snapshot : NULL, now_ms);
        if (ShoulderStandstill_Read(&standstill, now_ms).stationary && !pending) {
            if (!S3ShoulderService_Stop()) { return; }
            status.stop_observed = true; status.observed_ms = now_ms; status.state = S3_READER_COMPLETE;
            return;
        }
    }
    if (pending) { return; }
    if (next == GDS_PROPERTY_POSITION && have_cycle
        && (uint32_t)(now_ms - cycle_ms) < profile->shoulder_property_period_ms) { return; }
    /* New reads cannot precede the stop output leaving its hardware mailbox. */
    if (can.queued != 0U || !BoardCanTx_ShoulderReadReady()) { return; }
    BoardCanFrame read;
    if (!GdsSdoRead_PropertyRequest(next, &read) || !BoardCanTx_SubmitShoulderRead(&read)
        || !CanMonitor_ExpectShoulderProperty(next, now_ms)) { fail(S3_READ_TX); return; }
    if (next == GDS_PROPERTY_POSITION) { cycle_ms = now_ms; have_cycle = true; }
    status.requests++;
}
