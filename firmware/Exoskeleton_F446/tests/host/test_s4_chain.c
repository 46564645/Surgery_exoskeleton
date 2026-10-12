/* Full S4 read/model path with synthetic CAN only; output remains disabled. */
#include "test_s4_chain.h"
#include "s4_follow.h"
#include "s3_bench.h"
#include "s3_site_mock.h"
#include "can_monitor.h"
#include "can_mock.h"
#include "can_tx_mock.h"
#include "s2_config.h"
#include <assert.h>
#include <math.h>
#include <string.h>

static const uint16_t endpoint[GDS_PROPERTY_COUNT] =
    {372U, 378U, 195U, 366U, 272U, 287U, 288U};
static const uint32_t value_bits[GDS_PROPERTY_COUNT] =
    {0x3E800000U, 0U, 1U, 1U, 0U, 1U, 1U};

static S4SiteProfile read_site(void)
{
    S4SiteProfile p = {0};
    p.s3.shoulder_source = S3_SHOULDER_ENCODER_PROPERTIES;
    p.s3.shoulder_node = S2_GDS_NODE;
    p.s3.safety.feedback_timeout_ms = 100U;
    p.s3.shoulder_property_reply_ms = 10U;
    p.s3.shoulder_property_period_ms = 50U;
    p.s3.shoulder_property_window_ms = 500U;
    p.s3.coordinates[0] = S3_ASSEMBLED_JOINT;
    p.s3.rotor_reference_confirmed[0] = true;
    p.s3.trusted_feedback_axes = SAFETY_SHOULDER_AXES;
    p.s3.safety.joint[0].feedback_confirmed = true;
    p.s3.safety.joint[0].protocol_confirmed = true;
    p.s3.safety.joint[0].mapping = (JointMapping){1.0f, 0.0f, 1, true};
    p.gravity = (GravityCompensationConfig){2.0f, 0.0f, 1, true};
    return p; /* Synthetic TEST-only mapping and gravity values. */
}

static void heartbeat(uint32_t now_ms, uint32_t axis_error, uint8_t life)
{
    uint8_t data[8] = {0};
    for (unsigned i = 0U; i < 4U; i++) { data[i] = (uint8_t)(axis_error >> (8U*i)); }
    data[4] = 1U; data[7] = life; /* IDLE, no drive activation. */
    TestClock_Set(now_ms);
    TestCan_InjectData(BOARD_CAN_SHOULDER, 0x021U, 8U, 0U, 0U, data);
    TestCan_Interrupt(BOARD_CAN_SHOULDER);
    CanMonitor_Poll();
}

static void check_no_output(unsigned read_count)
{
    S3BenchStatus bench;
    S3Bench_GetStatus(&bench);
    assert(bench.state == OUTPUT_SAFE_OFF && !bench.output_allowed);
    assert(TestCanTx_Sent(BOARD_CAN_SHOULDER) == read_count);
    assert(TestCanTx_Sent(BOARD_CAN_ELBOW) == 0U);
}

static void begin(S4SiteProfile *p)
{
    *p = read_site();
    assert(S4Site_ReadReady(p) && S4Site_ObservationReady(p));
    assert(!p->output_enabled && p->s3.safety.enabled_axes == 0U);
    assert(!S4Site_ProfileReady(p));
    TestS3Site_Reset(); /* The unrelated S3 gateway starts SAFE_OFF. */
    TestClock_Set(100U);
    CanMonitor_Init(&p->s3, SHOULDER_READER_S4);
    S3Bench_Init();
    TestCanTx_Reset();
    S4Follow_Init(p);
    heartbeat(100U, 0U, 1U);
    assert(S4Follow_Observe(100U));
    check_no_output(0U);
}

static void reply_to_pending(unsigned field, uint32_t now_ms)
{
    const BoardCanFrame *pending = TestCanTx_Pending(BOARD_CAN_SHOULDER, 0U);
    assert(pending != NULL && pending->id == 0x024U && pending->dlc == 8U);
    assert(pending->data[0] == 0U);
    assert(((uint16_t)pending->data[1] | ((uint16_t)pending->data[2] << 8)) == endpoint[field]);
    uint8_t data[8] = {0};
    data[1] = pending->data[1]; data[2] = pending->data[2];
    for (unsigned i = 0U; i < 4U; i++) {
        data[i + 4U] = (uint8_t)(value_bits[field] >> (8U*i));
    }
    TestCanTx_Complete(BOARD_CAN_SHOULDER);
    TestClock_Set(now_ms);
    TestCan_InjectData(BOARD_CAN_SHOULDER, 0x025U, 8U, 0U, 0U, data);
    TestCan_Interrupt(BOARD_CAN_SHOULDER);
    CanMonitor_Poll();
}

static void full_cycle(void)
{
    S4SiteProfile p;
    begin(&p);
    TestClock_Set(101U);
    S4Follow_Poll(101U, 1U);
    for (unsigned field = 0U; field < GDS_PROPERTY_COUNT; field++) {
        assert(TestCanTx_Sent(BOARD_CAN_SHOULDER) == field + 1U);
        reply_to_pending(field, 102U + field);
        S4Follow_Poll(102U + field, 1U);
        S4FollowStatus partial;
        S4Follow_GetStatus(&partial);
        assert(partial.phase == S4_FOLLOW_OBSERVING);
        if (field + 1U < GDS_PROPERTY_COUNT) {
            assert(!partial.new_model_sample && partial.model_sequence == 0U);
        }
    }
    S4FollowStatus s;
    S4Follow_GetStatus(&s);
    assert(s.reader.requests == GDS_PROPERTY_COUNT);
    assert(s.observation.properties_fresh && s.observation.joint.valid);
    assert(s.observation.properties.sequence == 1U);
    assert(s.observation.properties.requested_ms[GDS_PROPERTY_POSITION] == 101U);
    assert(s.observation.properties.received_ms[GDS_PROPERTY_INPUT_MODE] == 108U);
    assert(s.model_valid && s.new_model_sample);
    assert(s.model_sequence == 1U && s.model_sample_ms == 101U);
    assert(fabsf(s.model_joint_torque_nm - 2.0f) < 0.00001f);
    check_no_output(GDS_PROPERTY_COUNT);

    /* Polling the published cycle again must not create a new model sample. */
    TestClock_Set(108U);
    S4Follow_Poll(108U, 1U);
    S4Follow_GetStatus(&s);
    assert(s.model_valid && !s.new_model_sample);
    assert(s.model_sequence == 1U && s.model_sample_ms == 101U);
    assert(s.reader.requests == GDS_PROPERTY_COUNT);
    check_no_output(GDS_PROPERTY_COUNT);

    /* A fault retires the reader without any fallback output or auto-retry. */
    heartbeat(109U, 1U, 2U);
    S4Follow_Poll(109U, 1U);
    S4Follow_GetStatus(&s);
    assert(s.phase == S4_FOLLOW_FAULT && s.reader.state == S3_READER_FAULT);
    assert(!s.model_valid && !s.new_model_sample && !S4Follow_Observe(110U));
    S4Follow_Cancel();
    S4Follow_GetStatus(&s);
    assert(s.phase == S4_FOLLOW_FAULT);
    TestClock_Set(110U);
    S4Follow_Poll(110U, 1U);
    check_no_output(GDS_PROPERTY_COUNT);
}

static void cancel_pending(void)
{
    S4SiteProfile p;
    begin(&p);
    TestClock_Set(101U);
    S4Follow_Poll(101U, 1U);
    assert(TestCanTx_Pending(BOARD_CAN_SHOULDER, 0U) != NULL);
    S4Follow_Cancel();
    S4FollowStatus s;
    S4Follow_GetStatus(&s);
    assert(s.phase == S4_FOLLOW_STOPPING && s.reader.state == S3_READER_CANCELLING);
    assert(!s.model_valid && !s.new_model_sample && s.model_sequence == 0U);
    assert(TestCanTx_Pending(BOARD_CAN_SHOULDER, 0U) != NULL);
    assert(!S4Follow_Observe(102U));
    TestClock_Set(102U);
    S4Follow_Poll(102U, 1U);
    S4Follow_GetStatus(&s);
    assert(s.phase == S4_FOLLOW_STOPPED && s.reader.state == S3_READER_STOPPED);
    assert(TestCanTx_Pending(BOARD_CAN_SHOULDER, 0U) == NULL);
    assert(TestCanTx_Aborts(BOARD_CAN_SHOULDER) == 1U);
    check_no_output(1U);
}

static void cancel_delayed_abort(void)
{
    S4SiteProfile p;
    begin(&p);
    S4Follow_Poll(101U, 1U);
    TestCanTx_AbortCompletes(BOARD_CAN_SHOULDER, false);
    TestClock_Set(102U);
    S4Follow_Cancel();
    S4FollowStatus s;
    S4Follow_GetStatus(&s);
    assert(s.phase == S4_FOLLOW_STOPPING && s.reader.state == S3_READER_CANCELLING);
    S4Follow_Poll(102U, 1U);
    S4Follow_GetStatus(&s);
    assert(s.phase == S4_FOLLOW_STOPPING && s.reader.state == S3_READER_CANCELLING);
    assert(TestCanTx_Aborts(BOARD_CAN_SHOULDER) == 1U);
    assert(TestCanTx_Pending(BOARD_CAN_SHOULDER, 0U) != NULL);
    TestCanTx_CompleteAbort(BOARD_CAN_SHOULDER);
    TestClock_Set(103U);
    S4Follow_Poll(103U, 1U);
    S4Follow_GetStatus(&s);
    assert(s.phase == S4_FOLLOW_STOPPED && s.reader.state == S3_READER_STOPPED);
    assert(TestCanTx_Sent(BOARD_CAN_SHOULDER) == 1U);
    check_no_output(1U);

    begin(&p);
    S4Follow_Poll(101U, 1U);
    TestCanTx_AbortCompletes(BOARD_CAN_SHOULDER, false);
    TestClock_Set(102U);
    S4Follow_Cancel();
    S4Follow_Poll(102U, 1U);
    TestClock_Set(112U); /* Synthetic 10 ms cancellation budget expires. */
    S4Follow_Poll(112U, 1U);
    S4Follow_GetStatus(&s);
    assert(s.phase == S4_FOLLOW_FAULT && s.reader.reason == S3_READ_TX);
    check_no_output(1U);

    begin(&p);
    S4Follow_Poll(101U, 1U);
    TestCanTx_Fail(BOARD_CAN_SHOULDER, false, true);
    TestClock_Set(102U);
    S4Follow_Cancel();
    S4Follow_Poll(102U, 1U);
    S4Follow_GetStatus(&s);
    assert(s.phase == S4_FOLLOW_FAULT && s.reader.reason == S3_READ_TX);
    check_no_output(1U);
}

static void arbitration_retry(void)
{
    S4SiteProfile p;
    begin(&p);
    TestClock_Set(101U);
    S4Follow_Poll(101U, 1U);
    assert(TestCanTx_Sent(BOARD_CAN_SHOULDER) == 1U);
    TestCanTx_LoseArbitration(BOARD_CAN_SHOULDER);
    TestClock_Set(102U);
    CanMonitor_Poll();
    S4Follow_Poll(102U, 1U);
    assert(TestCanTx_Sent(BOARD_CAN_SHOULDER) == 2U);
    assert(TestCanTx_Last(BOARD_CAN_SHOULDER)->id == 0x024U);
    assert(TestCanTx_Last(BOARD_CAN_SHOULDER)->data[1] == (uint8_t)endpoint[0]);
    reply_to_pending(0U, 103U);
    S4Follow_Poll(103U, 1U);
    S4FollowStatus s;
    S4Follow_GetStatus(&s);
    assert(s.phase == S4_FOLLOW_OBSERVING && s.reader.requests == 3U);
    assert(TestCanTx_Sent(BOARD_CAN_SHOULDER) == 3U);
    assert(TestCanTx_Last(BOARD_CAN_SHOULDER)->data[1] == (uint8_t)endpoint[1]);
    for (unsigned field = 1U; field < GDS_PROPERTY_COUNT; field++) {
        reply_to_pending(field, 103U + field);
        S4Follow_Poll(103U + field, 1U);
    }
    S4Follow_GetStatus(&s);
    assert(s.phase == S4_FOLLOW_OBSERVING && s.observation.properties.sequence == 1U);
    assert(s.observation.properties.requested_ms[0] == 101U);
    assert(s.reader.requests == GDS_PROPERTY_COUNT + 1U);
    check_no_output(GDS_PROPERTY_COUNT + 1U);
}

static void arbitration_failures(void)
{
    S4SiteProfile p;
    S4FollowStatus s;
    begin(&p);
    S4Follow_Poll(101U, 1U);
    TestCanTx_LoseArbitration(BOARD_CAN_SHOULDER);
    TestClock_Set(102U); CanMonitor_Poll(); S4Follow_Poll(102U, 1U);
    assert(TestCanTx_Sent(BOARD_CAN_SHOULDER) == 2U);
    TestCanTx_LoseArbitration(BOARD_CAN_SHOULDER);
    TestClock_Set(103U); CanMonitor_Poll(); S4Follow_Poll(103U, 1U);
    S4Follow_GetStatus(&s);
    assert(s.phase == S4_FOLLOW_FAULT);
    assert(s.reader.reason == S3_READ_TX);
    check_no_output(2U);

    begin(&p);
    S4Follow_Poll(101U, 1U);
    TestCanTx_CompleteFailed(BOARD_CAN_SHOULDER); /* RQCP+TERR, not ALST. */
    TestClock_Set(102U); CanMonitor_Poll(); S4Follow_Poll(102U, 1U);
    S4Follow_GetStatus(&s);
    assert(s.phase == S4_FOLLOW_FAULT);
    assert(s.reader.reason == S3_READ_TX);
    check_no_output(1U);

    begin(&p);
    S4Follow_Poll(101U, 1U);
    TestCanTx_CompleteUnknown(BOARD_CAN_SHOULDER); /* No completion proof. */
    TestClock_Set(102U); CanMonitor_Poll(); S4Follow_Poll(102U, 1U);
    S4Follow_GetStatus(&s);
    assert(s.phase == S4_FOLLOW_FAULT);
    assert(s.reader.reason == S3_READ_TX);
    check_no_output(1U);

    begin(&p);
    S4Follow_Poll(101U, 1U);
    TestCanTx_LoseArbitration(BOARD_CAN_SHOULDER);
    TestClock_Set(111U); CanMonitor_Poll(); S4Follow_Poll(111U, 10U);
    S4Follow_GetStatus(&s);
    assert(s.phase == S4_FOLLOW_FAULT);
    assert(s.reader.reason == S3_READ_TX);
    check_no_output(1U);

    begin(&p);
    S4Follow_Poll(101U, 1U);
    TestCanTx_LoseArbitration(BOARD_CAN_SHOULDER);
    S4Follow_Cancel();
    TestClock_Set(102U); CanMonitor_Poll(); S4Follow_Poll(102U, 1U);
    S4Follow_GetStatus(&s);
    assert(s.phase == S4_FOLLOW_STOPPED);
    check_no_output(1U);
}

static void arbitration_stray_reply(void)
{
    S4SiteProfile p;
    begin(&p);
    S4Follow_Poll(101U, 1U);
    TestCanTx_LoseArbitration(BOARD_CAN_SHOULDER);
    uint8_t stale[8] = {0};
    stale[1] = (uint8_t)endpoint[0]; stale[2] = (uint8_t)(endpoint[0] >> 8);
    TestClock_Set(102U);
    TestCan_InjectData(BOARD_CAN_SHOULDER, 0x025U, 8U, 0U, 0U, stale);
    TestCan_Interrupt(BOARD_CAN_SHOULDER);
    CanMonitor_Poll(); S4Follow_Poll(102U, 1U);
    S4FollowStatus s;
    S4Follow_GetStatus(&s);
    assert(s.phase == S4_FOLLOW_FAULT && s.reader.requests == 1U);
    assert(s.observation.properties.sequence == 0U);
    check_no_output(1U);
}

void TestS4Chain_Run(const char *scenario)
{
    if (strcmp(scenario, "s4_chain_complete_fault") == 0) { full_cycle(); }
    else if (strcmp(scenario, "s4_chain_cancel_pending") == 0) { cancel_pending(); }
    else if (strcmp(scenario, "s4_chain_cancel_delayed_abort") == 0) { cancel_delayed_abort(); }
    else if (strcmp(scenario, "s4_chain_alst_retry") == 0) { arbitration_retry(); }
    else if (strcmp(scenario, "s4_chain_alst_failures") == 0) { arbitration_failures(); }
    else if (strcmp(scenario, "s4_chain_alst_stray") == 0) { arbitration_stray_reply(); }
    else { assert(!"Unknown S4 chain scenario"); }
}
