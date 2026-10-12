/* Synthetic S4 gateway boundary: no HAL, CAN peripheral or production limits. */
#include "test_s4_gateway.h"
#include "output_gateway.h"
#include "board_can_tx.h"
#include "can_tx_mock.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    float effort_nm;
    bool idle;
} TxEvent;
typedef struct {
    TxEvent event[64];
    unsigned count, purges, drain_polls;
    bool fail_normal, fail_stop, fail_purge, zero_pending, block_drain;
    bool block_output, async_failure;
} FollowTransport;

static bool purge(void *context, uint8_t axes)
{
    FollowTransport *t = context;
    assert(axes == SAFETY_SHOULDER_AXES);
    t->purges++;
    return !t->fail_purge;
}

static bool submit(void *context, const BoardCanFrame frames[2], uint8_t axes, bool stop)
{
    FollowTransport *t = context;
    assert(axes == SAFETY_SHOULDER_AXES && t->count < 64U);
    if (stop) {
        assert(frames[0].id == 0x027U && frames[0].data[0] == 1U);
        if (t->fail_stop) { return false; }
        t->event[t->count++] = (TxEvent){0.0f, true};
        return true;
    }
    if (t->fail_normal) { return false; }
    assert(frames[0].id == 0x02EU || frames[0].id == 0x02BU
        || frames[0].id == 0x027U); /* Preparation has three explicit steps. */
    float effort = 0.0f;
    if (frames[0].id == 0x02EU) { memcpy(&effort, &frames[0].data[0], sizeof(effort)); }
    t->zero_pending = frames[0].id == 0x02EU && effort == 0.0f;
    if (t->zero_pending) { t->drain_polls = 0U; }
    t->event[t->count++] = (TxEvent){effort, false};
    return true;
}

static bool drained(void *context, uint8_t axes)
{
    FollowTransport *t = context;
    assert(axes == SAFETY_SHOULDER_AXES);
    if (t->block_output || t->async_failure) { return false; }
    if (!t->zero_pending) { return true; }
    if (t->block_drain) { return false; }
    return ++t->drain_polls >= 2U; /* One pending poll before TX completion. */
}
static bool failed(void *context, uint8_t axes)
{
    FollowTransport *t = context;
    assert(axes == SAFETY_SHOULDER_AXES);
    return t->async_failure;
}

static SafetyInputs healthy(uint32_t now_ms)
{
    SafetyInputs in = {0};
    in.transport_healthy = in.drivers_ready = true;
    in.joint[0] = (JointState){0.0f, 0.0f, now_ms, true};
    return in;
}

static OutputDriverStatus driver(uint32_t now_ms, uint8_t state)
{
    OutputDriverStatus d = {0};
    d.valid = d.calibration_verified = true;
    d.node = 1U; d.axis_state = state; d.received_ms = now_ms;
    return d;
}

static void prepare_and_arm(OutputGateway *g, FollowTransport *t, uint32_t start_ms,
                            uint32_t sample_ttl_ms)
{
    SafetyConfig c = {0};
    c.enabled_axes = SAFETY_SHOULDER_AXES;
    c.stop_policy = SAFETY_STOP_SUPERVISED_SHOULDER_FOLLOW;
    c.protection_confirmed = c.stop_method_confirmed = c.timing_confirmed = true;
    c.feedback_timeout_ms = 100U; c.service_limit_ms = 25U;
    c.max_request_ms = sample_ttl_ms;
    c.joint[0].mapping = (JointMapping){1.0f, 0.0f, 1, true};
    c.joint[0].lower_rad = -1.0f; c.joint[0].upper_rad = 1.0f;
    c.joint[0].max_speed_rad_s = 2.0f;
    c.joint[0].max_motor_command = 0.05f; c.joint[0].motor_slew_per_s = 1.0f;
    c.joint[0].limits_confirmed = c.joint[0].feedback_confirmed
        = c.joint[0].protocol_confirmed = true;
    memset(t, 0, sizeof(*t));
    OutputTransport port = {purge, submit, t, drained, failed};
    OutputGateway_Init(g, &c, &port, 1U, 1U);
    assert(SafetySupervisor_ConfigReady(&g->config));
    assert(OutputGateway_ConfigureFollow(g, 10U, 200U, 80U, 1.0f));
    SafetyInputs in = healthy(start_ms);
    OutputDriverStatus d[2] = {driver(start_ms, 1U), {0}};
    assert(!OutputGateway_Arm(g, &in, start_ms));
    assert(OutputGateway_Prepare(g, &in, d, 100U, start_ms));
    in = healthy(start_ms + 1U); d[0].received_ms = start_ms + 1U;
    d[0].zero_verified = true; d[0].zero_verified_ms = start_ms + 1U;
    OutputGateway_PollPreparation(g, &in, d, start_ms + 1U);
    assert(g->driver_phase == DRIVER_WAIT_MODE);
    in = healthy(start_ms + 2U); d[0].received_ms = start_ms + 2U;
    d[0].mode_verified = true; d[0].mode_verified_ms = start_ms + 2U;
    OutputGateway_PollPreparation(g, &in, d, start_ms + 2U);
    assert(g->driver_phase == DRIVER_WAIT_CLOSED_LOOP);
    in = healthy(start_ms + 3U); d[0].received_ms = start_ms + 3U;
    d[0].axis_state = 8U;
    OutputGateway_PollPreparation(g, &in, d, start_ms + 3U);
    assert(g->state == OUTPUT_READY);
    in = healthy(start_ms + 4U);
    assert(OutputGateway_Arm(g, &in, start_ms + 4U));
    assert(g->follow_session_used && g->session_deadline_ms == start_ms + 204U);
    t->count = 0U; /* Test only commands accepted after manual Arm. */
}

static void step_until(OutputGateway *g, uint32_t first, uint32_t last)
{
    for (uint32_t at = first; at <= last; at++) {
        SafetyInputs in = healthy(at);
        OutputGateway_Step(g, &in, at);
    }
}

static void assert_zero_then_idle(const FollowTransport *t, unsigned from)
{
    assert(t->count > from + 1U);
    assert(!t->event[t->count - 2U].idle && t->event[t->count - 2U].effort_nm == 0.0f);
    assert(t->event[t->count - 1U].idle);
}

static void normal_cancel(void)
{
    OutputGateway g; FollowTransport t;
    prepare_and_arm(&g, &t, 1000U, 100U);
    assert(OutputGateway_RefreshFollow(&g, 0.04f, 1005U, 1U, 1005U));
    step_until(&g, 1005U, 1035U);
    assert(g.state == OUTPUT_ACTIVE && g.applied > 0.0f);
    unsigned before = t.count;
    assert(OutputGateway_BeginNormalStop(&g, OUTPUT_REASON_CANCEL, 1035U));
    assert(g.state == OUTPUT_RAMPING_DOWN && !g.request_valid);
    assert(!OutputGateway_RefreshFollow(&g, 0.02f, 1036U, 2U, 1036U));
    step_until(&g, 1036U, 1065U);
    assert(g.state == OUTPUT_RAMPING_DOWN && g.follow_zero_pending);
    assert(t.event[t.count - 1U].effort_nm == 0.0f && !t.event[t.count - 1U].idle);
    step_until(&g, 1066U, 1066U);
    assert(g.state == OUTPUT_RAMPING_DOWN && !t.event[t.count - 1U].idle);
    step_until(&g, 1067U, 1080U);
    assert(g.state == OUTPUT_SAFE_OFF && g.reason == OUTPUT_REASON_CANCEL);
    assert(g.stop_attempted && g.stop_accepted && !g.stop_delivery_failed);
    float previous = INFINITY;
    for (unsigned i = before; i + 1U < t.count; i++) {
        assert(!t.event[i].idle);
        float magnitude = fabsf(t.event[i].effort_nm);
        assert(magnitude <= previous + 0.000001f);
        previous = magnitude;
    }
    assert_zero_then_idle(&t, before);
    SafetyInputs in = healthy(1081U);
    OutputDriverStatus d[2] = {driver(1081U, 1U), {0}};
    assert(!OutputGateway_Prepare(&g, &in, d, 100U, 1081U));
    assert(!OutputGateway_Arm(&g, &in, 1081U));
    assert(!OutputGateway_AcknowledgeFault(&g, &in, 1081U, true));
}

static void scheduled_expiry(void)
{
    OutputGateway g; FollowTransport t;
    prepare_and_arm(&g, &t, 1000U, 100U);
    assert(OutputGateway_RefreshFollow(&g, 0.05f, 1005U, 1U, 1005U));
    step_until(&g, 1005U, 1054U);
    assert(OutputGateway_RefreshFollow(&g, 0.05f, 1055U, 2U, 1055U));
    step_until(&g, 1055U, 1104U);
    assert(OutputGateway_RefreshFollow(&g, 0.05f, 1105U, 3U, 1105U));
    step_until(&g, 1105U, 1123U);
    assert(g.state == OUTPUT_ACTIVE && g.applied > 0.0f);
    unsigned before = t.count;
    step_until(&g, 1124U, 1124U);
    assert(g.state == OUTPUT_RAMPING_DOWN && g.reason == OUTPUT_REASON_EXPIRED);
    assert(g.ramp_start_ms == 1124U && g.ramp_deadline_ms == 1204U);
    step_until(&g, 1125U, 1204U);
    assert(g.state == OUTPUT_SAFE_OFF && g.stop_accepted);
    assert_zero_then_idle(&t, before);
}

static void hard_faults(void)
{
    OutputGateway g; FollowTransport t;
    prepare_and_arm(&g, &t, 1000U, 100U);
    assert(OutputGateway_RefreshFollow(&g, 0.04f, 1005U, 1U, 1005U));
    step_until(&g, 1005U, 1035U);
    assert(OutputGateway_BeginNormalStop(&g, OUTPUT_REASON_CANCEL, 1035U));
    step_until(&g, 1036U, 1045U);
    unsigned before = t.count;
    SafetyInputs bad = healthy(1046U); bad.transport_healthy = false;
    OutputGateway_Step(&g, &bad, 1046U);
    assert(g.state == OUTPUT_FAULT && g.reason == OUTPUT_REASON_HEALTH);
    assert(t.count == before + 1U && t.event[t.count - 1U].idle);
    assert(!OutputGateway_AcknowledgeFault(&g, &bad, 1047U, true));

    prepare_and_arm(&g, &t, 2000U, 100U);
    assert(OutputGateway_RefreshFollow(&g, 0.04f, 2005U, 1U, 2005U));
    assert(!OutputGateway_RefreshFollow(&g, 0.03f, 2006U, 1U, 2006U));
    assert(g.state == OUTPUT_FAULT && t.event[t.count - 1U].idle);

    prepare_and_arm(&g, &t, 3000U, 30U);
    assert(OutputGateway_RefreshFollow(&g, 0.04f, 3005U, 1U, 3005U));
    step_until(&g, 3005U, 3015U);
    assert(!OutputGateway_RefreshFollow(&g, 0.03f, 3005U, 2U, 3035U));
    assert(g.state == OUTPUT_FAULT && t.event[t.count - 1U].idle);

    prepare_and_arm(&g, &t, 4000U, 100U);
    assert(OutputGateway_RefreshFollow(&g, 0.04f, 4005U, 1U, 4005U));
    t.fail_normal = true;
    step_until(&g, 4005U, 4015U);
    assert(g.state == OUTPUT_FAULT && g.reason == OUTPUT_REASON_TRANSPORT);
    assert(t.count == 1U && t.event[0].idle);

    prepare_and_arm(&g, &t, 5000U, 100U);
    assert(OutputGateway_RefreshFollow(&g, 0.04f, 5005U, 1U, 5005U));
    step_until(&g, 5005U, 5035U);
    assert(OutputGateway_BeginNormalStop(&g, OUTPUT_REASON_CANCEL, 5035U));
    t.block_drain = true;
    step_until(&g, 5036U, 5114U);
    assert(g.state == OUTPUT_FAULT && g.reason == OUTPUT_REASON_TRANSPORT);
    assert(t.event[t.count - 1U].idle); /* Zero-TX timeout hard-cuts before D. */

    /* L1: mailbox acceptance is not transmission completion. A read may
     * reuse the completed output slot, but the positive TXOK is latched first. */
    TestCanTx_Reset();
    BoardCanFrame zero[2] = {{0}};
    assert(MotorCommand_GdsV22Torque(1U, 0.0f, &zero[0]));
    assert(BoardCanTx_SubmitOutputAxes(zero, SAFETY_SHOULDER_AXES, false));
    assert(!BoardCanTx_ShoulderOutputDrained());
    TestCanTx_Complete(BOARD_CAN_SHOULDER);
    BoardCanFrame read = {.id = 0x024U, .dlc = 8U};
    assert(BoardCanTx_SubmitShoulderRead(&read));
    assert(BoardCanTx_ShoulderOutputDrained());
    assert(BoardCanTx_CancelShoulderRead());
    TestCanTx_Reset();
    assert(BoardCanTx_SubmitOutputAxes(zero, SAFETY_SHOULDER_AXES, false));
    assert(BoardCanTx_PurgeOutputAxes(SAFETY_SHOULDER_AXES));
    assert(!BoardCanTx_ShoulderOutputDrained());
    TestCanTx_Reset();
    assert(BoardCanTx_SubmitOutputAxes(zero, SAFETY_SHOULDER_AXES, false));
    TestCanTx_CompleteFailed(BOARD_CAN_SHOULDER);
    assert(!BoardCanTx_ShoulderOutputDrained());
    assert(BoardCanTx_ShoulderOutputFailed());
    TestCanTx_Reset();
    assert(BoardCanTx_SubmitOutputAxes(zero, SAFETY_SHOULDER_AXES, false));
    assert(BoardCanTx_PurgeOutputAxes(SAFETY_SHOULDER_AXES));
    assert(!BoardCanTx_ShoulderOutputFailed()); /* Deliberate purge is not failure. */

    /* A normal Nm frame accepted by HAL can later lose arbitration in NART
     * mode. Its completed-without-TXOK status must hard-withdraw before any
     * further torque frame, even while feedback and CAN RX remain healthy. */
    prepare_and_arm(&g, &t, 6000U, 100U);
    assert(OutputGateway_RefreshFollow(&g, 0.04f, 6005U, 1U, 6005U));
    step_until(&g, 6005U, 6014U);
    assert(g.state == OUTPUT_ACTIVE && g.follow_output_pending && t.count == 1U);
    t.async_failure = true;
    step_until(&g, 6015U, 6015U);
    assert(g.state == OUTPUT_FAULT && g.reason == OUTPUT_REASON_TRANSPORT);
    assert(t.count == 2U && t.event[1].idle);

    /* A still-pending frame is never purged merely to refresh the target;
     * it receives a finite completion window, then the fault stop. */
    prepare_and_arm(&g, &t, 7000U, 100U);
    assert(OutputGateway_RefreshFollow(&g, 0.04f, 7005U, 1U, 7005U));
    t.block_output = true;
    step_until(&g, 7005U, 7038U);
    assert(g.state == OUTPUT_ACTIVE && t.count == 1U);
    step_until(&g, 7039U, 7039U);
    assert(g.state == OUTPUT_FAULT && g.reason == OUTPUT_REASON_TRANSPORT);
    assert(t.count == 2U && t.event[1].idle);

    /* A human cancel may enter the ramp while one prior Nm frame is still
     * pending; later positive completion permits monotone withdrawal. */
    prepare_and_arm(&g, &t, 8000U, 100U);
    assert(OutputGateway_RefreshFollow(&g, 0.04f, 8005U, 1U, 8005U));
    t.block_output = true;
    step_until(&g, 8005U, 8014U);
    assert(OutputGateway_BeginNormalStop(&g, OUTPUT_REASON_CANCEL, 8015U));
    assert(g.state == OUTPUT_RAMPING_DOWN && t.count == 1U);
    t.block_output = false;
    step_until(&g, 8015U, 8100U);
    assert(g.state == OUTPUT_SAFE_OFF && g.stop_accepted);
}

static void session_reference_once(void)
{
    OutputGateway g; FollowTransport t;
    prepare_and_arm(&g, &t, 9000U, 100U);
    SafetyConfig c = g.config;
    OutputTransport port = {purge, submit, &t, drained, failed};
    OutputGateway_Init(&g, &c, &port, 1U, 1U);
    assert(!OutputGateway_SetFollowReference(&g, NAN));
    assert(OutputGateway_SetFollowReference(&g, 1.25f));
    assert(g.follow_reference_set && g.config.joint[0].mapping.rotor_zero_rad == 1.25f);
    assert(!OutputGateway_SetFollowReference(&g, 1.5f));
    assert(g.config.joint[0].mapping.rotor_zero_rad == 1.25f && t.count == 0U);
    OutputGateway_RejectRequest(&g);
    assert(g.state == OUTPUT_FAULT && !OutputGateway_SetFollowReference(&g, 1.5f));
    assert(!OutputGateway_AcknowledgeFault(&g, NULL, 9001U, true));
}

void TestS4Gateway_Run(const char *scenario)
{
    if (strcmp(scenario, "s4_gateway_ramp") == 0) { normal_cancel(); }
    else if (strcmp(scenario, "s4_gateway_expiry") == 0) { scheduled_expiry(); }
    else if (strcmp(scenario, "s4_gateway_faults") == 0) {
        hard_faults(); session_reference_once();
    }
    else { assert(!"Unknown S4 gateway scenario"); }
}
