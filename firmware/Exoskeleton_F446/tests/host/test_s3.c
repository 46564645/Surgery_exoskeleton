/* Decision-critical host scenarios. No physical transport or actuators. */
#include "test_s3.h"
#include "output_gateway.h"
#include "s3_bench.h"
#include "s3_site_mock.h"
#include "output_can_transport.h"
#include "can_tx_mock.h"
#include "gds_feedback.h"
#include "s3_feedback.h"
#include "elbow_session_angle.h"
#include "can_monitor.h"
#include "can_mock.h"
#include "board_can_tx.h"
#include <assert.h>
#include <math.h>
#include <string.h>

typedef struct {
    OutputGateway *gateway;
    BoardCanFrame latest[2];
    unsigned normals, stops, purges;
    bool fail_purge, fail_normal, fail_stop, pending_normal;
} TestTransport;

static bool purge(void *context, uint8_t axes)
{
    TestTransport *t = context;
    assert(axes == t->gateway->config.enabled_axes);
    t->purges++;
    if (t->fail_purge) { return false; }
    t->pending_normal = false;
    return true;
}

static bool submit(void *context, const BoardCanFrame frames[2], uint8_t axes, bool stop)
{
    TestTransport *t = context;
    assert(axes == t->gateway->config.enabled_axes);
    if (stop) {
        assert(t->gateway->state == OUTPUT_SAFE_OFF || t->gateway->state == OUTPUT_FAULT);
        t->stops++;
        if (t->fail_stop) { return false; }
    } else {
        assert((t->gateway->state == OUTPUT_ACTIVE || t->gateway->state == OUTPUT_PREPARING)
               && !t->pending_normal);
        t->normals++;
        if (t->fail_normal) { return false; }
        t->pending_normal = true;
    }
    memcpy(t->latest, frames, sizeof(t->latest));
    return true;
}

static SafetyConfig confirmed_config(void)
{
    /* Explicit synthetic TEST values; never production parameters. */
    SafetyConfig c = {0};
    c.enabled_axes = SAFETY_ALL_AXES; c.protection_confirmed = true;
    c.stop_method_confirmed = c.timing_confirmed = true;
    c.feedback_timeout_ms = 100U;
    c.service_limit_ms = 20U;
    c.max_request_ms = 100U;
    for (unsigned i = 0; i < 2U; i++) {
        c.joint[i].mapping = (JointMapping){8.0f, 1.0f, 1, true};
        c.joint[i].lower_rad = -1.0f;
        c.joint[i].upper_rad = 1.0f;
        c.joint[i].max_speed_rad_s = 2.0f;
        c.joint[i].max_motor_command = 1.0f;
        c.joint[i].motor_slew_per_s = 10.0f;
        c.joint[i].limits_confirmed = c.joint[i].feedback_confirmed = c.joint[i].protocol_confirmed = true;
    }
    c.joint[1].mapping.direction = -1;
    return c;
}

SafetyConfig TestS3_ConfirmedConfig(void) { return confirmed_config(); }

static SafetyInputs healthy(uint32_t now)
{
    SafetyInputs in = {0};
    in.transport_healthy = in.operator_stop_clear = in.drivers_ready = in.independently_stopped = true;
    for (unsigned i = 0; i < 2U; i++) { in.joint[i] = (JointState){0.0f, 0.0f, now, true}; }
    return in;
}

static void init(OutputGateway *g, TestTransport *t)
{
    SafetyConfig c = confirmed_config();
    memset(t, 0, sizeof(*t));
    t->gateway = g;
    OutputTransport port = {purge, submit, t};
    OutputGateway_Init(g, &c, &port, 1U, 1U);
}

static void arm(OutputGateway *g, uint32_t now)
{
    SafetyInputs in = healthy(now);
    assert(OutputGateway_Arm(g, &in, now));
}

static void assert_stopped(const OutputGateway *g, const TestTransport *t)
{
    assert(g->state == OUTPUT_FAULT || g->state == OUTPUT_SAFE_OFF);
    assert(!g->request_valid && g->applied == 0.0f);
    assert(g->stop_attempted && g->stop_accepted && g->purge_ok);
    assert(!t->pending_normal);
    assert(t->latest[0].id == 0x27U && t->latest[0].data[0] == 1U);
    for (unsigned i = 0; i < 8U; i++) { assert(t->latest[1].data[i] == 0U); }
}

static void test_codec(void)
{
    BoardCanFrame f;
    const uint8_t negative[8] = {0xE0,0,0,0,0,0,0,0};
    assert(MotorCommand_C620(1U, -10.0f, &f));
    assert(f.id == 0x200U && f.dlc == 8U && memcmp(f.data, negative, 8) == 0);
    assert(MotorCommand_C620(8U, 20.0f, &f));
    assert(f.id == 0x1FFU && f.data[6] == 0x40U && f.data[7] == 0U);
    for (unsigned i = 0; i < 6U; i++) { assert(f.data[i] == 0U); }
    assert(!MotorCommand_C620(0U, 0.0f, &f));
    assert(!MotorCommand_C620(1U, 20.01f, &f));
    assert(!MotorCommand_C620(1U, NAN, &f));
    assert(MotorCommand_GdsV22Torque(1U, -1.5f, &f));
    const uint8_t torque[8] = {0,0,0xC0,0xBF,0,0,0,0};
    assert(f.id == 0x2EU && f.dlc == 8U && memcmp(f.data, torque, 8) == 0);
    assert(!MotorCommand_GdsV22Torque(64U, 1.0f, &f));
    assert(!MotorCommand_GdsV22Torque(1U, INFINITY, &f));
    assert(MotorCommand_GdsV22Idle(1U, &f));
    const uint8_t idle[8] = {1,0,0,0,0,0,0,0};
    assert(f.id == 0x27U && memcmp(f.data, idle, 8) == 0);
    assert(MotorCommand_GdsV22TorqueMode(1U, &f));
    const uint8_t mode[8] = {1,0,0,0,1,0,0,0};
    assert(f.id == 0x2BU && memcmp(f.data, mode, 8) == 0);
    assert(MotorCommand_GdsV22ClosedLoop(1U, &f));
    const uint8_t closed[8] = {8,0,0,0,0,0,0,0};
    assert(f.id == 0x27U && memcmp(f.data, closed, 8) == 0);
}

static void test_mapping(void)
{
    JointMapping m = {8.0f, 2.0f, -1, true};
    JointState s;
    assert(JointState_FromRotor(&m, 6.0f, -8.0f, 42U, true, &s));
    assert(s.valid && s.position_rad == -0.5f && s.velocity_rad_s == 1.0f && s.received_ms == 42U);
    assert(!JointState_FromRotor(&m, 6.0f, 0.0f, 43U, false, &s) && !s.valid);
    m.confirmed = false;
    assert(!JointState_FromRotor(&m, 6.0f, 0.0f, 43U, true, &s));
    m.confirmed = true; m.rotor_per_joint = 0.0f;
    assert(!JointState_FromRotor(&m, 6.0f, 0.0f, 43U, true, &s));
}

static void test_default(void)
{
    OutputGateway g;
    TestTransport t;
    init(&g, &t);
    g.config.joint[0].feedback_confirmed = false;
    SafetyInputs in = healthy(1U);
    assert(!OutputGateway_Arm(&g, &in, 1U));
    assert(!OutputGateway_Replace(&g, 0U, 0.5f, 10U, 1U));
    OutputGateway_Step(&g, &in, 2U);
    OutputGateway_Cancel(&g);
    assert(t.normals == 0U && t.stops == 0U && t.purges == 0U);
    g.config.joint[0].feedback_confirmed = true; g.config.joint[0].protocol_confirmed = false;
    assert(!OutputGateway_Arm(&g, &in, 1U));
    g.config.joint[0].protocol_confirmed = true; g.config.joint[1].mapping.confirmed = false;
    assert(!OutputGateway_Arm(&g, &in, 1U));
    S3Bench_Init();
    S3Bench_Poll(UINT32_MAX, 1U);
    S3BenchStatus status;
    S3Bench_GetStatus(&status);
    assert(status.state == OUTPUT_SAFE_OFF && !status.configured && !status.transport_bound);
    assert(!status.output_allowed && !status.request_valid && !status.stop_attempted);
}

static void test_lifecycle(void)
{
    OutputGateway g; TestTransport t; init(&g, &t);
    arm(&g, 100U);
    assert(OutputGateway_Replace(&g, 1U, 0.8f, 30U, 100U));
    SafetyInputs in = healthy(110U);
    OutputGateway_Step(&g, &in, 110U);
    assert(fabsf(g.applied - 0.1f) < 0.00001f && t.normals == 1U);
    assert(t.latest[1].data[0] == 0xFFU); /* direction -1, negative current */
    assert(OutputGateway_Replace(&g, 1U, -0.5f, 30U, 110U));
    assert(!t.pending_normal);
    in = healthy(120U); OutputGateway_Step(&g, &in, 120U);
    assert(fabsf(g.applied) < 0.00001f); /* bounded reversal from prior applied */
    OutputGateway_Cancel(&g); assert_stopped(&g, &t);
    assert(g.state == OUTPUT_SAFE_OFF && !OutputGateway_Replace(&g, 1U, 0.1f, 10U, 120U));
    arm(&g, UINT32_MAX - 5U);
    assert(OutputGateway_Replace(&g, 0U, 0.2f, 10U, UINT32_MAX - 5U));
    in = healthy(3U); OutputGateway_Step(&g, &in, 3U); /* elapsed9, still valid */
    assert(g.state == OUTPUT_ACTIVE);
    in = healthy(4U); OutputGateway_Step(&g, &in, 4U); /* elapsed10, expired */
    assert(g.reason == OUTPUT_REASON_EXPIRED); assert_stopped(&g, &t);
    unsigned normals = t.normals;
    OutputGateway_Step(&g, &in, 5U);
    assert(!OutputGateway_Arm(&g, &in, 5U) && t.normals == normals);
    assert(!OutputGateway_AcknowledgeFault(&g, &in, 5U, false));
    assert(OutputGateway_AcknowledgeFault(&g, &in, 5U, true));
    assert(g.state == OUTPUT_SAFE_OFF && !g.request_valid);
    /* Reboot cannot retain any permit, request or old callback context. */
    OutputGateway_Init(&g, NULL, NULL, 1U, 1U);
    assert(g.state == OUTPUT_SAFE_OFF && !g.request_valid && g.transport.submit == NULL);
}

static void test_interlocks(void)
{
    OutputGateway g; TestTransport t; SafetyInputs in;
    init(&g, &t); arm(&g, 200U);
    assert(OutputGateway_Replace(&g, 1U, 0.2f, 50U, 200U));
    in = healthy(201U); in.joint[0].valid = false; /* unused joint still critical */
    OutputGateway_Step(&g, &in, 201U); assert_stopped(&g, &t);
    assert(g.safety_reason == SAFETY_FEEDBACK && t.normals == 0U);
    OutputGateway_Cancel(&g); assert(g.state == OUTPUT_FAULT);
    init(&g, &t); arm(&g, 200U);
    in = healthy(201U); in.joint[1].position_rad = 1.01f;
    OutputGateway_Step(&g, &in, 201U); assert_stopped(&g, &t);
    assert(g.safety_reason == SAFETY_RANGE);
    init(&g, &t); arm(&g, 200U);
    in = healthy(201U); in.transport_healthy = false;
    OutputGateway_Step(&g, &in, 201U); assert_stopped(&g, &t);
    init(&g, &t); arm(&g, 200U);
    in = healthy(201U); in.operator_stop_clear = false;
    OutputGateway_Step(&g, &in, 201U); assert_stopped(&g, &t);
    init(&g, &t); arm(&g, 200U);
    in = healthy(201U); in.joint[0].received_ms = 100U;
    OutputGateway_Step(&g, &in, 201U); assert_stopped(&g, &t);
    init(&g, &t); arm(&g, 200U);
    assert(!OutputGateway_Replace(&g, 1U, NAN, 10U, 200U)); assert_stopped(&g, &t);
    init(&g, &t); arm(&g, 200U);
    assert(!OutputGateway_Replace(&g, 1U, 1.1f, 10U, 200U)); assert_stopped(&g, &t);
    init(&g, &t); arm(&g, 200U);
    assert(OutputGateway_Replace(&g, 0U, 0.1f, 10U, 200U));
    assert(!OutputGateway_Replace(&g, 1U, 0.1f, 10U, 200U)); assert_stopped(&g, &t);
    init(&g, &t); arm(&g, 200U);
    assert(OutputGateway_Replace(&g, 1U, 0.1f, 10U, 200U));
    assert(!OutputGateway_Replace(&g, 1U, 0.2f, 10U, 210U)); assert_stopped(&g, &t);
    init(&g, &t); arm(&g, 200U); in = healthy(221U);
    OutputGateway_Step(&g, &in, 221U);
    assert(g.reason == OUTPUT_REASON_SERVICE); assert_stopped(&g, &t);
}

static void test_transport(void)
{
    OutputGateway g; TestTransport t; init(&g, &t); arm(&g, 100U);
    assert(OutputGateway_Replace(&g, 0U, 0.2f, 50U, 100U));
    t.fail_normal = true;
    SafetyInputs in = healthy(101U); OutputGateway_Step(&g, &in, 101U);
    assert(g.reason == OUTPUT_REASON_TRANSPORT); assert_stopped(&g, &t);
    init(&g, &t); arm(&g, 100U);
    t.fail_stop = true; OutputGateway_Cancel(&g);
    assert(g.state == OUTPUT_FAULT && g.stop_attempted && !g.stop_accepted);
    assert(!OutputGateway_Arm(&g, &in, 101U));
    init(&g, &t); arm(&g, 100U);
    assert(OutputGateway_Replace(&g, 1U, 0.2f, 50U, 100U));
    OutputGateway_Step(&g, &in, 101U); assert(t.pending_normal);
    t.fail_purge = true; OutputGateway_Cancel(&g);
    assert(g.state == OUTPUT_FAULT && !g.purge_ok && g.stop_attempted);
    assert(t.pending_normal); /* adapter failure evidence must not be hidden */
    assert(!OutputGateway_AcknowledgeFault(&g, &in, 101U, true));
}

static void test_bxcan(void)
{
    /* Actual gateway -> L4 binder -> actual L1 adapter -> simulated ST HAL. */
    TestCanTx_Reset();
    OutputTransport port = OutputCanTransport_Create();
    SafetyConfig config = confirmed_config();
    OutputGateway g;
    OutputGateway_Init(&g, &config, &port, 1U, 1U);
    arm(&g, 100U);
    assert(OutputGateway_Replace(&g, 0U, 0.2f, 20U, 100U));
    SafetyInputs in = healthy(101U);
    OutputGateway_Step(&g, &in, 101U);
    assert(TestCanTx_Sent(BOARD_CAN_SHOULDER) == 1U && TestCanTx_Sent(BOARD_CAN_ELBOW) == 1U);
    assert(TestCanTx_Last(BOARD_CAN_SHOULDER)->id == 0x2EU);
    OutputGateway_Cancel(&g);
    assert(g.state == OUTPUT_SAFE_OFF && g.stop_accepted && g.purge_ok);
    assert(TestCanTx_Aborts(BOARD_CAN_SHOULDER) == 1U && TestCanTx_Aborts(BOARD_CAN_ELBOW) == 1U);
    assert(TestCanTx_Last(BOARD_CAN_SHOULDER)->id == 0x27U);
    for (unsigned i = 0U; i < 8U; i++) { assert(TestCanTx_Last(BOARD_CAN_ELBOW)->data[i] == 0U); }

    /* A real bxCAN abort can stay pending after HAL accepted ABRQ. Cancel
     * withdraws permission immediately, then sends zero only after the old
     * elbow-current mailbox is gone. HAL enqueue alone does not finish stop. */
    TestCanTx_Reset();
    config.enabled_axes = SAFETY_ELBOW_AXES;
    config.stop_policy = SAFETY_STOP_SUPERVISED_C620_BENCH;
    OutputGateway_Init(&g, &config, &port, 1U, 1U);
    /* Preparation is covered separately; enter READY here to isolate TX. */
    g.state = OUTPUT_READY; g.driver_phase = DRIVER_READY;
    g.prepare_ms = 99U; g.prepare_timeout_ms = 1000U;
    in = healthy(100U); in.feedback_stationary = true;
    assert(OutputGateway_Arm(&g, &in, 100U));
    assert(OutputGateway_Replace(&g, 1U, 0.2f, 50U, 100U));
    OutputGateway_Step(&g, &in, 101U);
    assert(TestCanTx_Sent(BOARD_CAN_ELBOW) == 1U);
    const BoardCanFrame *old = TestCanTx_Pending(BOARD_CAN_ELBOW, 0U);
    assert(old != NULL && (old->data[0] != 0U || old->data[1] != 0U));
    TestCanTx_AbortCompletes(BOARD_CAN_ELBOW, false);
    OutputGateway_Cancel(&g);
    assert(g.state != OUTPUT_ARMED && g.state != OUTPUT_ACTIVE && !g.request_valid);
    assert(g.stop_pending_purge && !g.stop_accepted);
    assert(TestCanTx_Aborts(BOARD_CAN_ELBOW) >= 1U);
    assert(TestCanTx_Sent(BOARD_CAN_ELBOW) == 1U);
    assert(TestCanTx_Pending(BOARD_CAN_ELBOW, 0U) != NULL);
    assert(TestCanTx_Pending(BOARD_CAN_ELBOW, 1U) == NULL);
    TestCanTx_CompleteAbort(BOARD_CAN_ELBOW);
    in = healthy(102U);
    OutputGateway_Step(&g, &in, 102U);
    assert(TestCanTx_Sent(BOARD_CAN_ELBOW) == 2U);
    for (unsigned i = 0U; i < 8U; i++) {
        assert(TestCanTx_Last(BOARD_CAN_ELBOW)->data[i] == 0U);
    }
    assert(TestCanTx_Pending(BOARD_CAN_ELBOW, 0U) != NULL);
    assert(TestCanTx_Pending(BOARD_CAN_ELBOW, 1U) == NULL);
    assert(g.stop_pending_tx && !g.stop_delivery_failed);
    TestCanTx_Complete(BOARD_CAN_ELBOW);
    in = healthy(125U); /* A late poll must see TXOK before judging deadline. */
    OutputGateway_Step(&g, &in, 125U);
    assert(g.state == OUTPUT_SAFE_OFF && g.stop_accepted && !g.stop_delivery_failed
        && !g.stop_pending_tx);

    TestCanTx_Reset();
    OutputGateway_Init(&g, &config, &port, 1U, 1U);
    g.state = OUTPUT_READY; g.driver_phase = DRIVER_READY;
    g.prepare_ms = 99U; g.prepare_timeout_ms = 1000U;
    in = healthy(100U); in.feedback_stationary = true;
    assert(OutputGateway_Arm(&g, &in, 100U));
    assert(OutputGateway_Replace(&g, 1U, 0.2f, 50U, 100U));
    in = healthy(101U); OutputGateway_Step(&g, &in, 101U);
    OutputGateway_CancelAt(&g, 101U);
    assert(g.stop_pending_tx && g.stop_accepted);
    TestCanTx_CompleteFailed(BOARD_CAN_ELBOW);
    in = healthy(102U); OutputGateway_Step(&g, &in, 102U);
    assert(g.state == OUTPUT_FAULT && g.stop_delivery_failed && !g.stop_pending_tx);

    config.enabled_axes = SAFETY_ALL_AXES;
    config.stop_policy = SAFETY_STOP_EXTERNAL_INPUT;
    in = healthy(101U);
    TestCanTx_Reset(); OutputGateway_Init(&g, &config, &port, 1U, 1U);
    arm(&g, 100U); assert(OutputGateway_Replace(&g, 0U, 0.2f, 20U, 100U));
    TestCanTx_Busy(BOARD_CAN_SHOULDER, false); /* ABRQ accepted but NOT completed */
    OutputGateway_Step(&g, &in, 101U);
    assert(g.state == OUTPUT_FAULT && !g.purge_ok && !g.stop_accepted);
    assert(TestCanTx_Sent(BOARD_CAN_SHOULDER) == 0U);
    assert(TestCanTx_Sent(BOARD_CAN_ELBOW) == 1U); /* still attempted its STOP */
    assert(TestCanTx_Aborts(BOARD_CAN_SHOULDER) == 1U); /* do not reissue a pending ABRQ */

    TestCanTx_Reset(); OutputGateway_Init(&g, &config, &port, 1U, 1U);
    arm(&g, 100U); assert(OutputGateway_Replace(&g, 0U, 0.2f, 20U, 100U));
    TestCanTx_Fail(BOARD_CAN_ELBOW, true, false); /* second bus partial failure */
    OutputGateway_Step(&g, &in, 101U);
    assert(g.state == OUTPUT_FAULT && !g.stop_accepted);
    assert(TestCanTx_Last(BOARD_CAN_SHOULDER)->id == 0x27U);
    assert(TestCanTx_Sent(BOARD_CAN_ELBOW) == 0U);

    TestCanTx_Reset();
    S3Bench_Init(); S3Bench_Poll(100U, 1U); /* unconfirmed production stays unbound */
    assert(TestCanTx_Sent(BOARD_CAN_SHOULDER) == 0U && TestCanTx_Sent(BOARD_CAN_ELBOW) == 0U);
    assert(TestCanTx_Aborts(BOARD_CAN_SHOULDER) == 0U && TestCanTx_Aborts(BOARD_CAN_ELBOW) == 0U);
}


static bool test_prepare_pair(OutputGateway *g, const SafetyInputs *in, const OutputDriverStatus *d,
                              uint32_t timeout, uint32_t now)
{ OutputDriverStatus pair[2] = {*d, *d}; return OutputGateway_Prepare(g, in, pair, timeout, now); }
static void test_poll_pair(OutputGateway *g, const SafetyInputs *in, const OutputDriverStatus *d, uint32_t now)
{ OutputDriverStatus pair[2] = {*d, *d}; OutputGateway_PollPreparation(g, in, pair, now); }
static bool test_ready_pair(const OutputGateway *g, const OutputDriverStatus *d, uint32_t now)
{ OutputDriverStatus pair[2] = {*d, *d}; return OutputGateway_DriverReady(g, pair, now); }
static OutputDriverStatus idle_driver(uint32_t now)
{
    OutputDriverStatus d = {0}; d.node = 1U; d.axis_state = 1U; d.received_ms = now;
    d.valid = d.calibration_verified = true; return d;
}
static void test_prepare(void)
{
    OutputGateway g; TestTransport t; init(&g, &t);
    SafetyInputs in = healthy(100U); in.drivers_ready = false;
    OutputDriverStatus d = idle_driver(100U);
    d.calibration_verified = false;
    assert(!test_prepare_pair(&g, &in, &d, 100U, 100U) && t.normals == 0U);
    d.calibration_verified = true; in.operator_stop_clear = false;
    assert(!test_prepare_pair(&g, &in, &d, 100U, 100U) && t.normals == 0U);
    in.operator_stop_clear = true;
    assert(test_prepare_pair(&g, &in, &d, 100U, 100U));
    assert(g.state == OUTPUT_PREPARING && !g.request_valid && t.normals == 1U);
    for (unsigned j = 0U; j < 2U; j++) { for (unsigned i = 0U; i < 8U; i++) { assert(t.latest[j].data[i] == 0U); } }
    assert(!OutputGateway_Arm(&g, &in, 100U));
    d.zero_verified = true; d.zero_verified_ms = 99U; d.received_ms = 101U;
    in = healthy(101U); in.drivers_ready = false;
    test_poll_pair(&g, &in, &d, 101U);
    assert(g.driver_phase == DRIVER_WAIT_ZERO && t.normals == 1U);
    d.zero_verified_ms = d.received_ms = 102U; in = healthy(102U);
    test_poll_pair(&g, &in, &d, 102U);
    assert(g.driver_phase == DRIVER_WAIT_MODE && t.latest[0].id == 0x2BU);
    d.mode_verified = true; d.mode_verified_ms = 102U; d.received_ms = 103U; in = healthy(103U);
    test_poll_pair(&g, &in, &d, 103U);
    assert(g.driver_phase == DRIVER_WAIT_MODE && t.normals == 2U);
    d.mode_verified_ms = d.received_ms = 104U; in = healthy(104U);
    test_poll_pair(&g, &in, &d, 104U);
    assert(g.driver_phase == DRIVER_WAIT_CLOSED_LOOP && t.latest[0].id == 0x27U && t.latest[0].data[0] == 8U);
    d.axis_state = 8U;
    test_poll_pair(&g, &in, &d, 104U);
    assert(g.state == OUTPUT_PREPARING); /* Cached/same-time heartbeat is not proof. */
    d.received_ms = 105U; in = healthy(105U);
    test_poll_pair(&g, &in, &d, 105U);
    assert(g.state == OUTPUT_READY && test_ready_pair(&g, &d, 105U));
    assert(!OutputGateway_Replace(&g, 1U, 0.2f, 10U, 105U));
    assert(OutputGateway_Arm(&g, &in, 105U));
    assert(OutputGateway_Replace(&g, 1U, 0.2f, 10U, 105U));
    d.fault_flags = 1U; in.drivers_ready = test_ready_pair(&g, &d, 106U);
    OutputGateway_Step(&g, &in, 106U); assert_stopped(&g, &t);
    assert(g.driver_phase == DRIVER_NOT_PREPARED);
    in = healthy(107U); in.drivers_ready = false;
    assert(!OutputGateway_AcknowledgeFault(&g, &in, 107U, false));
    assert(OutputGateway_AcknowledgeFault(&g, &in, 107U, true));
    assert(g.state == OUTPUT_SAFE_OFF && g.driver_phase == DRIVER_NOT_PREPARED);
    init(&g, &t); in = healthy(200U); in.drivers_ready = false; d = idle_driver(200U);
    assert(test_prepare_pair(&g, &in, &d, 20U, 200U));
    in = healthy(220U); d.received_ms = 220U;
    test_poll_pair(&g, &in, &d, 220U);
    assert(g.reason == OUTPUT_REASON_EXPIRED); assert_stopped(&g, &t);
    init(&g, &t); in = healthy(300U); d = idle_driver(300U);
    assert(test_prepare_pair(&g, &in, &d, 100U, 300U));
    t.fail_stop = true; OutputGateway_Cancel(&g);
    assert(g.state == OUTPUT_FAULT && !g.stop_accepted && !g.request_valid);
}
static void test_gds(void)
{
    GdsDevice d; GdsFeedback s;
    assert(!GdsFeedback_Init(&d, 1U, GDS_PROFILE_UNSELECTED, 100U));
    assert(GdsFeedback_Init(&d, 1U, GDS_PROFILE_V22_FW_GE_0513, 100U));
    BoardCanFrame f = {0}; f.id = 0x29U; f.dlc = 8U; f.received_ms = 50U;
    const uint8_t estimate[8] = {0,0,0xA0,0x3F,0,0,0,0xBF}; /* +1.25rev, -0.5rev/s. */
    memcpy(f.data, estimate, 8U);
    assert(!GdsFeedback_Receive(&d, BOARD_CAN_ELBOW, &f, 50U));
    assert(GdsFeedback_Receive(&d, BOARD_CAN_SHOULDER, &f, 50U));
    assert(!GdsFeedback_Get(&d, 50U, &s)); /* No heartbeat. */
    assert(fabsf(s.rotor_position_rad - 7.8539816f) < 0.00001f);
    assert(fabsf(s.rotor_velocity_rad_s + 3.1415927f) < 0.00001f);
    f.id = 0x21U; memset(f.data, 0, 8U); f.data[4] = 1U; f.data[5] = 0x80U; f.data[7] = 255U;
    assert(GdsFeedback_Receive(&d, BOARD_CAN_SHOULDER, &f, 50U));
    assert(GdsFeedback_Get(&d, 50U, &s) && s.axis_state == 1U && s.fault_flags == 0U);
    f.data[7] = 0U; f.received_ms = 51U;
    assert(GdsFeedback_Receive(&d, BOARD_CAN_SHOULDER, &f, 51U));
    assert(GdsFeedback_Get(&d, 51U, &s) && s.life_contiguous);
    f.data[7] = 3U;
    assert(GdsFeedback_Receive(&d, BOARD_CAN_SHOULDER, &f, 51U));
    assert(!GdsFeedback_Get(&d, 51U, &s));
    f.data[7] = 4U; f.data[5] = 8U;
    assert(GdsFeedback_Receive(&d, BOARD_CAN_SHOULDER, &f, 51U));
    assert(!GdsFeedback_Get(&d, 51U, &s) && s.fault_flags == 8U);
    f.id = 0x298U; /* Observed raw ID is not assumed to be node1 estimates. */
    GdsFeedback before = d.sample;
    assert(!GdsFeedback_Receive(&d, BOARD_CAN_SHOULDER, &f, 51U));
    assert(memcmp(&before, &d.sample, sizeof(before)) == 0);
    f.id = 0x29U; memcpy(f.data, estimate, 8U); f.data[2] = 0xC0U; f.data[3] = 0x7FU;
    assert(!GdsFeedback_Receive(&d, BOARD_CAN_SHOULDER, &f, 51U)); /* NaN. */
    f.dlc = 7U;
    assert(!GdsFeedback_Receive(&d, BOARD_CAN_SHOULDER, &f, 51U));
    assert(!GdsFeedback_Get(&d, 151U, &s)); /* Old estimate cannot become fresh. */
}

static void test_axes(void)
{
    SafetyConfig c = confirmed_config(); c.enabled_axes = SAFETY_SHOULDER_AXES;
    memset(&c.joint[1], 0, sizeof(c.joint[1]));
    assert(SafetySupervisor_ConfigReady(&c));
    SafetyInputs in = healthy(100U); in.joint[1].valid = false; in.drivers_ready = false;
    assert(SafetySupervisor_CheckBeforePrepare(&c, &in, 100U) == SAFETY_OK);
    OutputGateway g; TestTransport t = {0}; t.gateway = &g;
    OutputTransport port = {purge, submit, &t};
    OutputGateway_Init(&g, &c, &port, 1U, 1U);
    OutputDriverStatus d[2] = {idle_driver(100U), {0}};
    assert(OutputGateway_Prepare(&g, &in, d, 100U, 100U));
    for (unsigned i = 0U; i < 8U; i++) { assert(t.latest[1].data[i] == 0U); }
    d[0].zero_verified = true; d[0].zero_verified_ms = d[0].received_ms = 101U;
    in.joint[0].received_ms = 101U; OutputGateway_PollPreparation(&g, &in, d, 101U);
    d[0].mode_verified = true; d[0].mode_verified_ms = d[0].received_ms = 102U;
    in.joint[0].received_ms = 102U; OutputGateway_PollPreparation(&g, &in, d, 102U);
    d[0].axis_state = 8U; d[0].received_ms = in.joint[0].received_ms = 103U;
    OutputGateway_PollPreparation(&g, &in, d, 103U);
    assert(g.state == OUTPUT_READY && OutputGateway_DriverReady(&g, d, 103U));
    in.drivers_ready = true; assert(OutputGateway_Arm(&g, &in, 103U));
    assert(!OutputGateway_Replace(&g, 1U, 0.1f, 10U, 103U));
    assert(g.state == OUTPUT_FAULT); /* Disabled axis can never become a request. */
    c.enabled_axes = SAFETY_ELBOW_AXES; c.joint[1] = confirmed_config().joint[1];
    memset(&c.joint[0], 0, sizeof(c.joint[0])); assert(SafetySupervisor_ConfigReady(&c));
    OutputGateway_Init(&g, &c, &port, 1U, 1U); in = healthy(200U);
    in.joint[0].valid = false; in.drivers_ready = false;
    memset(d, 0, sizeof(d)); d[1].valid = true; d[1].node = 1U; d[1].received_ms = 200U;
    assert(OutputGateway_Prepare(&g, &in, d, 100U, 200U));
    OutputGateway_PollPreparation(&g, &in, d, 200U); assert(g.state == OUTPUT_PREPARING);
    d[1].received_ms = in.joint[1].received_ms = 201U;
    OutputGateway_PollPreparation(&g, &in, d, 201U);
    assert(g.state == OUTPUT_READY && !d[1].zero_verified && OutputGateway_DriverReady(&g, d, 201U));
    d[1].valid = false;
    assert(!OutputGateway_DriverReady(&g, d, 201U));
    OutputGateway_PollPreparation(&g, &in, d, 201U);
    assert(g.state == OUTPUT_FAULT && g.stop_attempted);
    d[1].valid = true; OutputGateway_Init(&g, &c, &port, 1U, 1U);
    in.independently_stopped = false;
    assert(!OutputGateway_Prepare(&g, &in, d, 100U, 202U));
    in.independently_stopped = true;
    assert(OutputGateway_Prepare(&g, &in, d, 100U, 202U));
    d[1].received_ms = in.joint[1].received_ms = 203U;
    OutputGateway_PollPreparation(&g, &in, d, 203U); assert(g.state == OUTPUT_READY);
    in.drivers_ready = true; assert(OutputGateway_Arm(&g, &in, 203U));
    assert(OutputGateway_Replace(&g, 1U, 0.1f, 10U, 203U));
    in.operator_stop_clear = false; OutputGateway_Step(&g, &in, 204U);
    assert(g.state == OUTPUT_FAULT && !g.request_valid && g.stop_attempted);
    c.enabled_axes = 0U; assert(!SafetySupervisor_ConfigReady(&c));
    c.enabled_axes = 4U; assert(!SafetySupervisor_ConfigReady(&c));
    TestCanTx_Reset(); BoardCanFrame f[2] = {{0}, {0}};
    assert(MotorCommand_GdsV22Torque(1U, 0.0f, &f[0])); f[1].id = 0xFFFFU;
    assert(BoardCanTx_SubmitAxes(f, SAFETY_SHOULDER_AXES, false));
    assert(TestCanTx_Sent(BOARD_CAN_SHOULDER) == 1U && TestCanTx_Sent(BOARD_CAN_ELBOW) == 0U);
    assert(BoardCanTx_PurgeAxes(SAFETY_SHOULDER_AXES));
    assert(TestCanTx_Aborts(BOARD_CAN_ELBOW) == 0U);
    assert(!BoardCanTx_SubmitAxes(f, 0U, false));
    TestCanTx_Reset(); assert(MotorCommand_C620(1U, 0.0f, &f[1]));
    f[0].id = 0xFFFFU;
    assert(!BoardCanTx_SubmitAxes(f, SAFETY_ALL_AXES, true) && TestCanTx_Sent(BOARD_CAN_ELBOW) == 1U);
}
static void test_observations(void)
{
    TestClock_Set(0U); CanMonitor_Init(S3Site_GetProfile(), SHOULDER_READER_S3);
    S3SiteProfile p = {0}; p.shoulder_node = p.elbow_id = 1U;
    S3SiteSample s; assert(!S3Feedback_Read(&p, 0U, &s));
    assert(s.observed_axes == 0U && s.wire_valid_axes == 0U);
    const uint8_t estimate[8] = {0,0,0xA0,0x3F,0,0,0,0xBF};
    uint8_t heartbeat[8] = {0,0,0,0,1,0,0,255};
    const uint8_t elbow[8] = {0x0C,0x13,0,0,0xFF,0xFC,0x1E,0};
    TestClock_Set(10U);
    TestCan_InjectData(BOARD_CAN_SHOULDER, 0x29U, 8U, 0U, 0U, estimate);
    TestCan_InjectData(BOARD_CAN_SHOULDER, 0x21U, 8U, 0U, 0U, heartbeat);
    TestCan_Interrupt(BOARD_CAN_SHOULDER);
    TestCan_InjectData(BOARD_CAN_ELBOW, 0x201U, 8U, 0U, 0U, elbow);
    TestCan_Interrupt(BOARD_CAN_ELBOW); CanMonitor_Poll();
    assert(!S3Feedback_Read(&p, 10U, &s));
    assert(s.observed_axes == SAFETY_ALL_AXES && s.wire_valid_axes == SAFETY_ALL_AXES);
    assert(s.shoulder_rotor.axis_state == 1U && s.shoulder_rotor.heartbeat_ms == 10U);
    assert(s.elbow_rotor.rotor_angle_ticks == 3091U);
    assert(!s.joint[0].valid && !s.joint[1].valid && !s.driver[0].calibration_verified
        && !s.driver[0].zero_verified && !s.driver[0].mode_verified && !s.driver[1].valid);
    assert(!s.operator_stop_clear && !s.independently_stopped);
    p.safety = confirmed_config(); p.safety.enabled_axes = SAFETY_ELBOW_AXES;
    p.coordinates[1] = S3_BENCH_ROTOR; p.bench_sector_confirmed[1] = true;
    p.trusted_feedback_axes = SAFETY_ELBOW_AXES;
    p.safety.joint[1].mapping = (JointMapping){1.0f, 0.0f, 1, true};
    p.safety.joint[1].lower_rad = 1.0f; p.safety.joint[1].upper_rad = 3.0f;
    assert(S3Feedback_Read(&p, 10U, &s) && s.joint[1].valid);
    assert(s.joint[1].received_ms == 10U && s.joint[1].position_rad > 2.37f);
    assert(!p.rotor_reference_confirmed[1] && !s.driver[1].valid && !s.operator_stop_clear);
    p.operational_confirmed[1] = true;
    assert(S3Feedback_Read(&p, 10U, &s) && s.driver[1].valid && !s.driver[1].zero_verified);
    assert(!s.operator_stop_clear && !s.independently_stopped); /* Not a permit or stop proof. */
    p.elbow_id = 2U;
    assert(!S3Feedback_Read(&p, 10U, &s) && !s.joint[1].valid && !s.driver[1].valid);
    p.elbow_id = 1U;
    p.coordinates[1] = S3_ASSEMBLED_JOINT;
    assert(!S3Feedback_Read(&p, 10U, &s) && !s.joint[1].valid);
    p.coordinates[1] = S3_BENCH_ROTOR; p.safety.joint[1].lower_rad = -0.1f;
    assert(!S3Feedback_Read(&p, 10U, &s) && !s.joint[1].valid); /* Crosses modulo wrap. */
    p.safety.joint[1].lower_rad = 1.0f; p.safety.joint[1].mapping.rotor_zero_rad = 0.5f;
    assert(!S3Feedback_Read(&p, 10U, &s) && !s.joint[1].valid);
    p = (S3SiteProfile){0}; p.shoulder_node = p.elbow_id = 1U;
    TestClock_Set(50U); heartbeat[7] = 0U;
    TestCan_InjectData(BOARD_CAN_SHOULDER, 0x21U, 8U, 0U, 0U, heartbeat);
    TestCan_Interrupt(BOARD_CAN_SHOULDER); CanMonitor_Poll();
    assert(!S3Feedback_Read(&p, 50U, &s));
    assert(s.shoulder_rotor.heartbeat_ms == 50U && s.shoulder_rotor.estimate_ms == 10U);
    TestClock_Set(111U); CanMonitor_Poll(); assert(!S3Feedback_Read(&p, 111U, &s));
    assert(s.wire_valid_axes == 0U && s.observed_axes == SAFETY_ALL_AXES);
}
static void observe(C620Standstill *s, uint32_t stamp, uint16_t ticks, int16_t rpm)
{
    C620Feedback f = {0}; f.has_sample = f.valid = true;
    f.received_ms = stamp; f.rotor_angle_ticks = ticks; f.rotor_speed_rpm = rpm;
    C620Standstill_Observe(s, &f, stamp);
}
static void test_standstill(void)
{
    C620Standstill s = {0};
    for (uint32_t t = 0U; t < 250U; t++) { observe(&s, t, 3437U, 0); }
    assert(!C620Standstill_Read(&s, 249U).stationary);
    for (unsigned i = 0; i < 20U; i++) { observe(&s, 249U, 3437U, 0); }
    assert(C620Standstill_Read(&s, 299U).dwell_ms == 249U);
    observe(&s, 250U, 3439U, 0);
    assert(C620Standstill_Read(&s, 250U).stationary); /* Exactly two ticks. */
    observe(&s, 251U, 3440U, 0); /* Slow accumulated drift, not per-frame difference. */
    assert(!C620Standstill_Read(&s, 251U).stationary);
    assert(C620Standstill_Read(&s, 251U).dwell_ms == 0U);
    for (uint32_t t = 252U; t <= 501U; t++) { observe(&s, t, 3440U, 0); }
    assert(C620Standstill_Read(&s, 551U).stationary);
    assert(!C620Standstill_Read(&s, 552U).stationary);
    observe(&s, 552U, 3440U, 0); assert(C620Standstill_Read(&s, 552U).dwell_ms == 0U);
    C620Standstill_Reset(&s);
    for (uint32_t t = 0; t <= 250U; t++) { observe(&s, t, 100U, 0); }
    observe(&s, 250U, 100U, 1); observe(&s, 250U, 100U, 0);
    assert(!C620Standstill_Read(&s, 250U).stationary); /* Same tick movement is not hidden. */
    observe(&s, 301U, 100U, 0); assert(C620Standstill_Read(&s, 301U).dwell_ms == 0U);
    observe(&s, 300U, 100U, 0); assert(!C620Standstill_Read(&s, 301U).seen);
    C620Standstill_Reset(&s);
    for (uint32_t t = 0; t <= 250U; t++) { observe(&s, UINT32_MAX - 100U + t, 3437U, 0); }
    assert(C620Standstill_Read(&s, 149U).stationary); /* Wrap-safe receive interval. */
    observe(&s, 150U, 8192U, 0); assert(!C620Standstill_Read(&s, 150U).seen);
    C620Standstill_After(&s, 300U);
    C620Standstill_Observe(&s, NULL, 300U); /* Invalid target frame cannot erase the fence. */
    observe(&s, 299U, 3437U, 0); observe(&s, 300U, 3437U, 0);
    assert(!C620Standstill_Read(&s, 300U).seen);
    for (uint32_t t = 301U; t <= 550U; t++) { observe(&s, t, 3437U, 0); }
    assert(!C620Standstill_Read(&s, 550U).stationary);
    observe(&s, 551U, 3437U, 0); assert(C620Standstill_Read(&s, 551U).stationary);
    C620Standstill_After(&s, UINT32_MAX);
    observe(&s, UINT32_MAX, 3437U, 0); assert(!C620Standstill_Read(&s, UINT32_MAX).seen);
    observe(&s, 0U, 3437U, 0); assert(C620Standstill_Read(&s, 0U).seen);
    C620Standstill_Reset(&s);
    for (uint32_t t = 0U; t <= C620_STILL_WINDOW_MS; t++) {
        observe(&s, t, (uint16_t)(8191U + t % 3U) % 8192U, 0);
    }
    assert(C620Standstill_Read(&s, C620_STILL_WINDOW_MS).stationary);
    observe(&s, C620_STILL_WINDOW_MS + 1U, 2U, 0);
    assert(!C620Standstill_Read(&s, C620_STILL_WINDOW_MS + 1U).stationary);
}

static C620Feedback elbow_frame(uint32_t stamp, uint16_t ticks, float velocity)
{
    C620Feedback f = {0};
    f.has_sample = f.valid = true; f.received_ms = stamp;
    f.sequence = stamp;
    f.rotor_angle_ticks = ticks; f.rotor_velocity_rad_s = velocity;
    return f;
}
static void test_elbow_session_angle(void)
{
    ElbowSessionAngle s = {0}; JointState joint;
    assert(ElbowSessionAngle_Begin(&s, 100U));
    C620Feedback f = elbow_frame(100U, 8191U, 0.0f);
    ElbowSessionAngle_Observe(&s, &f, true, 100U, 10U, 5.0f, 1);
    assert(s.phase == ELBOW_SESSION_WAITING); /* Pre-command frame cannot set zero. */
    f = elbow_frame(101U, 8191U, 0.0f);
    ElbowSessionAngle_Observe(&s, &f, true, 101U, 10U, 5.0f, 1);
    assert(ElbowSessionAngle_Read(&s, 101U, 10U, &joint) && joint.position_rad == 0.0f);
    f = elbow_frame(102U, 0U, 0.0f);
    ElbowSessionAngle_Observe(&s, &f, false, 102U, 10U, 5.0f, 1);
    assert(ElbowSessionAngle_Read(&s, 102U, 10U, &joint)
        && joint.position_rad > 0.0f && joint.position_rad < 0.001f);
    uint32_t fresh_ms = joint.received_ms;
    ElbowSessionAngle_Observe(&s, &f, false, 103U, 10U, 5.0f, 1);
    assert(ElbowSessionAngle_Read(&s, 103U, 10U, &joint) && joint.received_ms == fresh_ms);
    f = elbow_frame(102U, 1U, 0.0f); f.sequence = 103U;
    ElbowSessionAngle_Observe(&s, &f, false, 103U, 10U, 5.0f, 1);
    assert(ElbowSessionAngle_Read(&s, 103U, 10U, &joint)
        && joint.position_rad > 0.001f && joint.received_ms == 102U);
    f = elbow_frame(104U, 2U, 0.0f);
    ElbowSessionAngle_Observe(&s, &f, false, 104U, 10U, 5.0f, -1);
    assert(ElbowSessionAngle_Read(&s, 104U, 10U, &joint) && joint.position_rad < 0.0f);
    f = elbow_frame(105U, 100U, 0.0f);
    ElbowSessionAngle_Observe(&s, &f, false, 105U, 10U, 5.0f, -1);
    assert(s.phase == ELBOW_SESSION_FAULT && !ElbowSessionAngle_Read(&s, 105U, 10U, &joint));
    ElbowSessionAngle_Reset(&s);
    assert(ElbowSessionAngle_Begin(&s, 200U));
    f = elbow_frame(201U, 50U, 0.0f);
    ElbowSessionAngle_Observe(&s, &f, true, 201U, 10U, 5.0f, 1);
    assert(ElbowSessionAngle_Read(&s, 211U, 10U, &joint));
    f = elbow_frame(201U, 50U, 0.0f);
    ElbowSessionAngle_Observe(&s, &f, false, 212U, 10U, 5.0f, 1);
    assert(s.phase == ELBOW_SESSION_FAULT && !ElbowSessionAngle_Read(&s, 212U, 10U, &joint));

    /* S5 dual waits for a still zero sample. Once zero exists, its session
     * angle uses fresh encoder positions; running speed is checked outside. */
    ElbowSessionAngle_Reset(&s);
    assert(ElbowSessionAngle_BeginForDual(&s, 300U));
    f = elbow_frame(301U, 50U, 10.0f);
    ElbowSessionAngle_Observe(&s, &f, true, 301U, 10U, 0.34906585f, 1);
    assert(s.phase == ELBOW_SESSION_WAITING);
    f = elbow_frame(302U, 50U, 0.0f);
    ElbowSessionAngle_Observe(&s, &f, true, 302U, 10U, 0.34906585f, 1);
    assert(ElbowSessionAngle_Read(&s, 302U, 10U, &joint));
    f = elbow_frame(303U, 200U, 0.0f);
    ElbowSessionAngle_Observe(&s, &f, false, 303U, 10U, 0.34906585f, 1);
    assert(s.phase == ELBOW_SESSION_ACTIVE && s.last_ms == 303U
        && ElbowSessionAngle_Read(&s, 303U, 10U, &joint)
        && joint.position_rad > 0.1f);
    f = elbow_frame(304U, 51U, 0.0f);
    ElbowSessionAngle_Observe(&s, &f, false, 304U, 10U, 0.34906585f, 1);
    assert(ElbowSessionAngle_Read(&s, 304U, 10U, &joint)
        && joint.position_rad > 0.0f && s.last_ms == 304U);
    f = elbow_frame(305U, 151U, 10.0f);
    ElbowSessionAngle_Observe(&s, &f, false, 305U, 10U, 0.34906585f, 1);
    assert(s.phase == ELBOW_SESSION_ACTIVE && s.last_ms == 305U
        && ElbowSessionAngle_Read(&s, 305U, 10U, &joint)
        && joint.velocity_rad_s == 10.0f);
    f = elbow_frame(306U, 151U, 0.0f); f.valid = false;
    ElbowSessionAngle_Observe(&s, &f, false, 306U, 10U, 0.34906585f, 1);
    assert(s.phase == ELBOW_SESSION_ACTIVE && s.last_ms == 305U);
    assert(!ElbowSessionAngle_Read(&s, 316U, 10U, &joint));
    f = elbow_frame(317U, 152U, 0.0f);
    ElbowSessionAngle_Observe(&s, &f, false, 317U, 10U, 0.34906585f, 1);
    assert(s.phase == ELBOW_SESSION_ACTIVE && s.last_ms == 317U
        && ElbowSessionAngle_Read(&s, 317U, 10U, &joint));
}
static void elbow_bus_sample(uint32_t now_ms, uint16_t ticks)
{
    uint8_t data[8] = {(uint8_t)(ticks >> 8), (uint8_t)ticks, 0U, 0U,
                       0U, 0U, 30U, 0U};
    TestClock_Set(now_ms);
    TestCan_InjectData(BOARD_CAN_ELBOW, 0x201U, 8U, 0U, 0U, data);
    TestCan_Interrupt(BOARD_CAN_ELBOW);
    CanMonitor_Poll();
    S3Bench_Poll(now_ms, 10U);
}
static void test_elbow_session_bench(void)
{
    S3SiteProfile p = {0};
    p.safety = confirmed_config();
    p.safety.enabled_axes = SAFETY_ELBOW_AXES;
    p.safety.stop_policy = SAFETY_STOP_SUPERVISED_C620_BENCH;
    p.safety.joint[1].mapping = (JointMapping){1.0f, 0.0f, 1, true};
    p.safety.joint[1].lower_rad = -0.1f;
    p.safety.joint[1].upper_rad = 0.1f;
    p.safety.joint[1].max_motor_command = 0.1f;
    p.prepare_timeout_ms = 1000U;
    p.shoulder_node = p.elbow_id = 1U;
    p.trusted_feedback_axes = SAFETY_ELBOW_AXES;
    p.coordinates[1] = S3_ASSEMBLED_JOINT;
    p.operational_confirmed[1] = true;
    TestS3Site_ConfigureShoulderChain(&p);
    TestClock_Set(0U);
    CanMonitor_Init(S3Site_GetProfile(), SHOULDER_READER_S3);
    TestCanTx_Reset();
    S3Bench_Init();
    S3BenchStatus gate;
    S3Bench_GetStatus(&gate);
    assert(!gate.configured && gate.transport_bound && !gate.output_allowed);
    assert(S3Bench_Prepare(0U));
    assert(!S3Bench_Prepare(0U));
    for (uint32_t t = 1U; t <= 251U; t += 10U) {
        elbow_bus_sample(t, (uint16_t)((8191U + (t / 10U) % 2U) % 8192U));
    }
    S3Bench_GetStatus(&gate);
    assert(gate.elbow_zero_valid && !gate.elbow_zero_pending);
    assert(gate.state == OUTPUT_PREPARING && !gate.output_allowed);
    assert(TestCanTx_Sent(BOARD_CAN_ELBOW) == 1U);
    for (unsigned i = 0U; i < 8U; i++) {
        assert(TestCanTx_Last(BOARD_CAN_ELBOW)->data[i] == 0U);
    }
    elbow_bus_sample(261U, 8191U);
    S3Bench_GetStatus(&gate);
    assert(gate.state == OUTPUT_READY && !gate.output_allowed);
    for (uint32_t t = 271U; t <= 511U; t += 10U) {
        elbow_bus_sample(t, (uint16_t)((8191U + (t / 10U) % 2U) % 8192U));
    }
    assert(S3Bench_Arm(511U));
    S3Bench_GetStatus(&gate);
    assert(gate.output_allowed && gate.arm_token != 0U);
    uint32_t token = gate.arm_token;
    assert(S3Bench_Request(token, 1U, 0.05f, 50U, 511U));
    TestClock_Set(512U); S3Bench_Poll(512U, 1U);
    assert(TestCanTx_Last(BOARD_CAN_ELBOW)->id == 0x200U);
    assert(TestCanTx_Last(BOARD_CAN_ELBOW)->data[0] != 0U
        || TestCanTx_Last(BOARD_CAN_ELBOW)->data[1] != 0U);
    S3Bench_Cancel();
    S3Bench_GetStatus(&gate);
    assert(gate.state == OUTPUT_SAFE_OFF && !gate.output_allowed && !gate.elbow_zero_valid);
    assert(gate.stop_accepted && gate.purge_ok && !gate.request_valid);
    for (unsigned i = 0U; i < 8U; i++) {
        assert(TestCanTx_Last(BOARD_CAN_ELBOW)->data[i] == 0U);
    }
    assert(!S3Bench_Request(token, 1U, 0.05f, 50U, 512U));
}
static void test_bench_stop_policy(void)
{
    SafetyConfig c = confirmed_config(); c.stop_policy = SAFETY_STOP_SUPERVISED_C620_BENCH;
    assert(!SafetySupervisor_ConfigReady(&c)); c.enabled_axes = SAFETY_ELBOW_AXES;
    assert(SafetySupervisor_ConfigReady(&c));
    SafetyInputs in = healthy(100U); in.operator_stop_clear = in.independently_stopped = false;
    in.feedback_stationary = false;
    assert(SafetySupervisor_Check(&c, &in, 100U) == SAFETY_OK);
    OutputGateway g; TestTransport t = {0}; t.gateway = &g; OutputTransport port = {purge, submit, &t};
    OutputGateway_Init(&g, &c, &port, 1U, 1U);
    OutputDriverStatus d[2] = {{0}, {0}}; d[1].valid = true; d[1].node = 1U; d[1].received_ms = 100U;
    assert(!OutputGateway_Arm(&g, &in, 100U));
    assert(!OutputGateway_Prepare(&g, &in, d, 1000U, 100U));
    in.feedback_stationary = true;
    assert(OutputGateway_Prepare(&g, &in, d, 1000U, 100U));
    d[1].received_ms = in.joint[1].received_ms = 101U;
    OutputGateway_PollPreparation(&g, &in, d, 101U); assert(g.state == OUTPUT_READY);
    in.feedback_stationary = false; assert(!OutputGateway_Arm(&g, &in, 101U));
    in.feedback_stationary = true; assert(OutputGateway_Arm(&g, &in, 101U));
    assert(OutputGateway_Replace(&g, 1U, 0.05f, 100U, 101U));
    assert(!OutputGateway_Replace(&g, 1U, 0.05f, 100U, 102U)); /* No renewable bench pulse. */
    assert(g.state == OUTPUT_FAULT && g.stop_accepted);
    in.feedback_stationary = false;
    assert(!OutputGateway_AcknowledgeFault(&g, &in, 102U, true));
    in.feedback_stationary = true;
    assert(OutputGateway_AcknowledgeFault(&g, &in, 102U, true));
    d[1].received_ms = in.joint[1].received_ms = 103U;
    assert(OutputGateway_Prepare(&g, &in, d, 1000U, 103U));
    t.fail_stop = true; OutputGateway_Cancel(&g);
    assert(g.state == OUTPUT_FAULT && g.stop_delivery_failed && !g.stop_accepted);
    t.fail_stop = false;
    assert(!OutputGateway_AcknowledgeFault(&g, &in, 104U, true)); /* New stationary cannot erase failed delivery. */
    OutputGateway_Init(&g, &c, &port, 1U, 1U); t.fail_purge = true;
    assert(!OutputGateway_Prepare(&g, &in, d, 1000U, 104U));
    assert(g.stop_delivery_failed && !OutputGateway_AcknowledgeFault(&g, &in, 104U, true));
    c.stop_policy = SAFETY_STOP_EXTERNAL_INPUT;
    assert(SafetySupervisor_Check(&c, &in, 104U) == SAFETY_STOP_INPUT);
    assert(!SafetySupervisor_StopObserved(&c, &in)); /* Bench observation cannot forge external proof. */
}

void TestS3_Run(const char *scenario)
{
    if (!strcmp(scenario, "s3_standstill")) { test_standstill(); }
    else if (!strcmp(scenario, "s3_elbow_session_angle")) { test_elbow_session_angle(); }
    else if (!strcmp(scenario, "s3_elbow_session_bench")) { test_elbow_session_bench(); }
    else if (!strcmp(scenario, "s3_bench_stop_policy")) { test_bench_stop_policy(); }
    else if (!strcmp(scenario, "s3_axes")) { test_axes(); }
    else if (!strcmp(scenario, "s3_observations")) { test_observations(); }
    else if (!strcmp(scenario, "s3_codec")) { test_codec(); }
    else if (!strcmp(scenario, "s3_mapping")) { test_mapping(); }
    else if (!strcmp(scenario, "s3_default")) { test_default(); }
    else if (!strcmp(scenario, "s3_lifecycle")) { test_lifecycle(); }
    else if (!strcmp(scenario, "s3_interlocks")) { test_interlocks(); }
    else if (!strcmp(scenario, "s3_transport")) { test_transport(); }
    else if (!strcmp(scenario, "s3_bxcan")) { test_bxcan(); }
    else if (!strcmp(scenario, "s3_prepare")) { test_prepare(); }
    else if (!strcmp(scenario, "s3_gds")) { test_gds(); }
    else { assert(!"unknown S3 scenario"); }
}
