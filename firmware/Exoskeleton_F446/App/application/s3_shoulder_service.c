/* One main-loop SDO owner. The gateway alone owns enable/output/stop writes. */
#include "s3_shoulder_service.h"
#include "can_monitor.h"
#include "shoulder_read_diagnostic.h"
#include "board_can_tx.h"
#include "board_console.h"
#include "s2_config.h"
#include <stddef.h>
static const S3SiteProfile *profile;
static ShoulderReaderMode reader_mode;
static S3ReaderStatus status;
static ShoulderStandstill standstill;
static uint32_t started_ms, cycle_ms, read_submitted_ms, cancel_started_ms;
static bool have_cycle, cancellation_failed;
static uint8_t read_arbitration_retries;
static bool duration(uint32_t t) { return t > 0U && t < UINT32_C(0x80000000); }
static bool bench(void)
{ return profile != NULL && (profile->safety.stop_policy == SAFETY_STOP_SUPERVISED_SHOULDER_BENCH
    || profile->safety.stop_policy == SAFETY_STOP_SUPERVISED_SHOULDER_FOLLOW
    || profile->safety.stop_policy == SAFETY_STOP_SUPERVISED_DUAL_FOLLOW); }
bool S3ShoulderService_ReadProfileReady(const S3SiteProfile *p)
{
    return p != NULL && p->shoulder_source == S3_SHOULDER_ENCODER_PROPERTIES
        && p->shoulder_node == S2_GDS_NODE
        && duration(p->safety.feedback_timeout_ms)
        && duration(p->shoulder_property_reply_ms)
        && duration(p->shoulder_property_period_ms)
        && duration(p->shoulder_property_window_ms)
        && p->shoulder_property_reply_ms <= p->safety.feedback_timeout_ms
        && p->shoulder_property_period_ms < p->safety.feedback_timeout_ms
        && p->shoulder_property_window_ms > p->shoulder_property_period_ms
        && (uint64_t)p->shoulder_property_window_ms
            > (uint64_t)GDS_PROPERTY_COUNT * p->shoulder_property_reply_ms;
}
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
void S3ShoulderService_Init(const S3SiteProfile *p, ShoulderReaderMode mode)
{
    reader_mode = mode;
    profile = p; have_cycle = cancellation_failed = false;
    started_ms = cycle_ms = read_submitted_ms = cancel_started_ms = 0U;
    read_arbitration_retries = 0U;
    status = (S3ReaderStatus){0};
    bool ready = (mode == SHOULDER_READER_S4 || mode == SHOULDER_READER_S5)
        ? S3ShoulderService_ReadProfileReady(p)
        : mode == SHOULDER_READER_S3 && S3ShoulderService_ProfileReady(p);
    status.state = ready ? S3_READER_IDLE : S3_READER_DISABLED;
    ShoulderStandstill_Init(&standstill, p == NULL ? NULL : &p->shoulder_standstill);
}
S3ReaderStatus S3ShoulderService_Get(void) { return status; }
bool S3ShoulderService_S5FastPath(void)
{ return reader_mode == SHOULDER_READER_S5; }
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
        || status.state == S3_READER_STOPPING || status.state == S3_READER_OBSERVING
        || status.state == S3_READER_CANCELLING) {
        status.state = S3_READER_STOPPED;
    }
    CanMonitor_InvalidateShoulderProperties();
    bool stopped = BoardCanTx_CancelShoulderRead();
    if (!stopped) { cancellation_failed = true; }
    if (cancellation_failed) { status.state = S3_READER_FAULT; status.reason = S3_READ_TX; }
    return !cancellation_failed;
}
bool S3ShoulderService_BeginReadOnlyStop(uint32_t now_ms)
{
    if ((reader_mode != SHOULDER_READER_S4 && reader_mode != SHOULDER_READER_S5)
        || status.state != S3_READER_ACTIVE) { return false; }
    CanMonitor_InvalidateShoulderProperties();
    status.state = S3_READER_CANCELLING;
    cancel_started_ms = now_ms;
    return true;
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
    /* S5 can recover from historical RX losses once a live heartbeat is valid.
     * Older S3/S4 paths keep their existing qualification rules. */
    if (profile == NULL || BoardConsole_HasFault()
        || !BoardCan_GetStats(BOARD_CAN_SHOULDER, can) || !can->started
        || can->start_error != BOARD_CAN_START_OK || can->bus_off
        || (reader_mode != SHOULDER_READER_S5
            && (can->dropped != 0U || can->invalid != 0U
                || can->fifo_overruns != 0U))) { return false; }
    GdsFeedback hb; bool wire;
    if (!CanMonitor_GetShoulderObservation(&hb, &wire) || !hb.has_heartbeat || !hb.life_contiguous
        || hb.axis_error != 0U || hb.fault_flags != 0U || (hb.axis_state != 1U && hb.axis_state != 8U)
        || (uint32_t)(now_ms - hb.heartbeat_ms) > profile->safety.feedback_timeout_ms) { return false; }
    if (reader_mode == SHOULDER_READER_S4 || reader_mode == SHOULDER_READER_S5 || bench()) { return true; }
    /* S4 read-only mode has no installed stop GPIO; output stays separate. */
    S3StopObservation stop = S3StopInput_Read(&profile->operator_stop_input, &profile->independent_stop_input);
    return stop.available && stop.operator_stop_clear;
}
bool S3ShoulderService_Start(uint32_t now_ms)
{
    BoardCanStats can; GdsFeedback hb; bool wire;
    if (status.state != S3_READER_IDLE || !healthy(now_ms, &can)
        || (reader_mode != SHOULDER_READER_S5 && can.queued != 0U)
        || !CanMonitor_GetShoulderObservation(&hb, &wire) || hb.axis_state != 1U
        || !(reader_mode == SHOULDER_READER_S4 || reader_mode == SHOULDER_READER_S5
             ? ShoulderRead_ClaimS4() : ShoulderRead_ClaimS3())) { return false; }
    status.state = S3_READER_ACTIVE; started_ms = cycle_ms = now_ms;
    have_cycle = false; read_arbitration_retries = 0U; return true;
}
bool S3ShoulderService_RequestZeroProof(void)
{
    return reader_mode == SHOULDER_READER_S5 && status.state == S3_READER_ACTIVE
        && CanMonitor_RequestShoulderZeroProof();
}
bool S3ShoulderService_RequestModeProof(void)
{
    return reader_mode == SHOULDER_READER_S5 && status.state == S3_READER_ACTIVE
        && CanMonitor_RequestShoulderModeProof();
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
    if (status.state == S3_READER_CANCELLING) {
        BoardCanReadCancelResult cancel = BoardCanTx_CancelShoulderReadStep();
        if (cancel == BOARD_CAN_READ_CANCEL_DONE) {
            status.state = S3_READER_STOPPED;
        } else if (cancel == BOARD_CAN_READ_CANCEL_FAILED
            || (uint32_t)(now_ms - cancel_started_ms) >= profile->shoulder_property_reply_ms) {
            fail(S3_READ_TX);
        }
        return;
    }
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
        bool still = ShoulderStandstill_Read(&standstill, now_ms).stationary;
#if defined(S5_R5_DUAL_FOLLOW)
        /* R5: once torque is withdrawn a supported arm may still settle under
         * gravity. A new IDLE heartbeat plus a fresh pair ends the stop; the
         * standstill result is kept only as stop_observed. */
        bool done = reader_mode == SHOULDER_READER_S5 ? idle_after && fresh : still;
#else
        bool done = still;
#endif
        if (done && !pending) {
            if (!S3ShoulderService_Stop()) { return; }
            status.stop_observed = still; status.observed_ms = now_ms; status.state = S3_READER_COMPLETE;
            return;
        }
    }
    if (pending) {
        BoardCanReadTxResult tx = BoardCanTx_ShoulderReadResult();
        if (tx == BOARD_CAN_READ_TX_ALST) {
            /* Hardware auto-retransmission normally retries lost arbitration;
             * this path stays defensive. A lost read is known not to have
             * reached the motor, so one same-field retry is safe while the
             * original reply deadline is still open. */
            if (read_arbitration_retries != 0U
                || (uint32_t)(now_ms - read_submitted_ms) >= profile->shoulder_property_reply_ms) {
                fail(S3_READ_TX); return;
            }
            if (!BoardCanTx_ShoulderReadReady()) { return; }
            BoardCanFrame retry;
            if (!GdsSdoRead_PropertyRequest(next, &retry)
                || !BoardCanTx_SubmitShoulderRead(&retry)) { fail(S3_READ_TX); return; }
            read_arbitration_retries = 1U;
            status.requests++;
        } else if (tx == BOARD_CAN_READ_TX_FAILED || tx == BOARD_CAN_READ_TX_NONE) {
            fail(S3_READ_TX);
        }
        return;
    }
    if (next == GDS_PROPERTY_POSITION && have_cycle
        && (uint32_t)(now_ms - cycle_ms) < profile->shoulder_property_period_ms) { return; }
    /* New reads cannot precede the stop output leaving its hardware mailbox. */
    if (can.queued != 0U || !BoardCanTx_ShoulderReadReady()) { return; }
    BoardCanFrame read;
    if (!GdsSdoRead_PropertyRequest(next, &read) || !BoardCanTx_SubmitShoulderRead(&read)
        || !CanMonitor_ExpectShoulderProperty(next, now_ms)) { fail(S3_READ_TX); return; }
    read_submitted_ms = now_ms;
    read_arbitration_retries = 0U;
    if (next == GDS_PROPERTY_POSITION) { cycle_ms = now_ms; have_cycle = true; }
    status.requests++;
}
