/* Synthetic S5 pair gateway and real L1 HAL-mock TX ownership. No hardware. */
#include "test_s5_gateway.h"
#include "output_gateway.h"
#include "output_can_transport.h"
#include "board_can_tx.h"
#include "can_tx_mock.h"
#include "can_monitor.h"
#include "s3_shoulder_service.h"
#include "s2_config.h"
#include <assert.h>
#include <math.h>
#include <string.h>

typedef struct {
    uint8_t axes;
    bool stop;
    BoardCanFrame frames[2];
} PairEvent;
typedef struct {
    PairEvent event[48];
    unsigned count, purges;
    bool hold_zero, block_zero, fail_normal;
} PairTransport;

static bool purge(void *context, uint8_t axes)
{
    PairTransport *t = context;
    assert(axes == SAFETY_ELBOW_AXES || axes == SAFETY_ALL_AXES);
    t->purges++;
    return true;
}
static bool submit(void *context, const BoardCanFrame frames[2], uint8_t axes, bool stop)
{
    PairTransport *t = context;
    assert(t->count < 48U && (axes == SAFETY_ELBOW_AXES || axes == SAFETY_ALL_AXES));
    if (t->fail_normal && !stop) { return false; }
    if (!stop && frames[1].id == 0x200U && frames[1].data[0] == 0U
        && frames[1].data[1] == 0U && (axes == SAFETY_ELBOW_AXES
            || (frames[0].id == 0x02EU && frames[0].data[0] == 0U
                && frames[0].data[1] == 0U && frames[0].data[2] == 0U
                && frames[0].data[3] == 0U))) { t->hold_zero = t->block_zero; }
    t->event[t->count++] = (PairEvent){axes, stop, {frames[0], frames[1]}};
    return true;
}
static bool drained(void *context, uint8_t axes)
{
    PairTransport *t = context;
    assert(axes == SAFETY_ELBOW_AXES || axes == SAFETY_ALL_AXES);
    return !t->hold_zero;
}
static bool failed(void *context, uint8_t axes)
{ (void)context; assert(axes == SAFETY_ELBOW_AXES || axes == SAFETY_ALL_AXES); return false; }

static SafetyInputs inputs(uint32_t ms, uint8_t axes)
{
    SafetyInputs in = {0};
    in.transport_healthy = in.drivers_ready = true;
    in.stationary_axes = axes;
    in.joint[0] = (JointState){0.0f, 0.0f, ms, true};
    in.joint[1] = (JointState){0.0f, 0.0f, ms, true};
    return in;
}
static SafetyConfig config(uint8_t axes)
{
    SafetyConfig c = {0};
    c.enabled_axes = axes;
    c.stop_policy = axes == SAFETY_ALL_AXES
        ? SAFETY_STOP_SUPERVISED_DUAL_FOLLOW : SAFETY_STOP_SUPERVISED_ELBOW_FOLLOW;
    c.protection_confirmed = c.stop_method_confirmed = c.timing_confirmed = true;
    c.feedback_timeout_ms = 50U; c.service_limit_ms = 25U; c.max_request_ms = 60U;
    for (unsigned axis = 0U; axis < SAFETY_JOINT_COUNT; axis++) {
        SafetyJointConfig *j = &c.joint[axis];
        j->mapping = (JointMapping){1.0f, 0.0f, 1, true};
        j->lower_rad = -1.0f; j->upper_rad = 1.0f; j->max_speed_rad_s = 2.0f;
        j->max_motor_command = axis == 0U ? 0.1f : 0.4f;
        j->motor_slew_per_s = axis == 0U ? 2.0f : 8.0f;
        j->limits_confirmed = j->feedback_confirmed = j->protocol_confirmed = true;
    }
    return c;
}
static OutputDriverStatus driver(uint8_t node, uint32_t ms, uint8_t state)
{
    OutputDriverStatus d = {0};
    d.node = node; d.received_ms = ms; d.axis_state = state;
    d.valid = d.calibration_verified = true;
    return d;
}
static void ready_and_arm(OutputGateway *g, PairTransport *t, uint8_t axes, uint32_t start)
{
    memset(t, 0, sizeof(*t));
    SafetyConfig c = config(axes);
    OutputTransport port = {purge, submit, t, drained, failed};
    OutputGateway_Init(g, &c, &port, 1U, 1U);
    if (axes == SAFETY_ALL_AXES) { assert(OutputGateway_SetFollowReference(g, 0.5f)); }
    assert(!OutputGateway_SetElbowFollowReference(g, -0.4f));
    assert(OutputGateway_SetElbowFollowReference(g, 0.0f));
    assert(g->config.joint[1].mapping.rotor_zero_rad == 0.0f);
    assert(OutputGateway_ConfigureAxesFollow(g, 10U, 200U, 80U,
        (S5FollowEffort){axes == SAFETY_ALL_AXES ? 2.0f : 0.0f, 8.0f}));
    SafetyInputs in = inputs(start, axes);
    OutputDriverStatus d[2] = {driver(1U, start, 1U), driver(1U, start, 0U)};
    assert(OutputGateway_Prepare(g, &in, d, 100U, start));
    in = inputs(start + 1U, axes);
    d[0].received_ms = d[1].received_ms = start + 1U;
    d[0].zero_verified = true; d[0].zero_verified_ms = start + 1U;
    OutputGateway_PollPreparation(g, &in, d, start + 1U);
    if (axes == SAFETY_ALL_AXES) {
        assert(g->driver_phase == DRIVER_WAIT_MODE);
        in = inputs(start + 2U, axes); d[0].received_ms = d[1].received_ms = start + 2U;
        d[0].mode_verified = true; d[0].mode_verified_ms = start + 2U;
        OutputGateway_PollPreparation(g, &in, d, start + 2U);
        in = inputs(start + 3U, axes); d[0].received_ms = d[1].received_ms = start + 3U;
        d[0].axis_state = 8U;
        OutputGateway_PollPreparation(g, &in, d, start + 3U);
    }
    assert(g->state == OUTPUT_READY);
    in = inputs(start + 4U, axes);
    assert(OutputGateway_Arm(g, &in, start + 4U));
    t->count = 0U;
}
static S5FollowSample sample(uint8_t axes, uint32_t ms, uint32_t shoulder_seq,
                             uint32_t elbow_seq)
{
    S5FollowSample s = {0};
    if ((axes & SAFETY_SHOULDER_AXES) != 0U) {
        s.sample_ms[0] = ms; s.sequence[0] = shoulder_seq;
    }
    s.sample_ms[1] = ms; s.sequence[1] = elbow_seq;
    return s;
}
static void step_range(OutputGateway *g, uint8_t axes, uint32_t from, uint32_t to)
{
    for (uint32_t ms = from; ms <= to; ms++) {
        SafetyInputs in = inputs(ms, axes);
        OutputGateway_Step(g, &in, ms);
    }
}
static void pair_request(void)
{
    OutputGateway g; PairTransport t;
    SafetyConfig dual = config(SAFETY_ALL_AXES);
    SafetyInputs elbow_only_still = inputs(1000U, SAFETY_ELBOW_AXES);
    assert(!SafetySupervisor_StopObserved(&dual, &elbow_only_still));
    ready_and_arm(&g, &t, SAFETY_ALL_AXES, 1000U);
    assert(!OutputGateway_Replace(&g, 1U, 0.1f, 20U, 1005U));
    assert(OutputGateway_RefreshAxesFollow(&g, (S5FollowEffort){0.08f, 0.32f},
        sample(SAFETY_ALL_AXES, 1005U, 1U, 1U), 1005U));
    step_range(&g, SAFETY_ALL_AXES, 1005U, 1014U);
    assert(t.count == 1U && t.event[0].axes == SAFETY_ALL_AXES && !t.event[0].stop);
    assert(t.event[0].frames[0].id == 0x02EU && t.event[0].frames[1].id == 0x200U);
    assert(g.axes_applied.shoulder_rotor_nm > 0.0f && g.axes_applied.elbow_current_a > 0.0f);
    /* Independent new sequence permits both frames in the same millisecond. */
    assert(OutputGateway_RefreshAxesFollow(&g, (S5FollowEffort){0.08f, 0.32f},
        sample(SAFETY_ALL_AXES, 1005U, 2U, 2U), 1015U));
    assert(!OutputGateway_RefreshAxesFollow(&g, (S5FollowEffort){0.08f, 0.32f},
        sample(SAFETY_ALL_AXES, 1016U, 3U, 2U), 1016U));
    assert(g.state == OUTPUT_FAULT && g.reason == OUTPUT_REASON_REQUEST);
    assert(t.event[t.count - 1U].stop && t.event[t.count - 1U].axes == SAFETY_ALL_AXES);
    assert(t.event[t.count - 1U].frames[0].id == 0x027U
        && t.event[t.count - 1U].frames[1].id == 0x200U);

    ready_and_arm(&g, &t, SAFETY_ELBOW_AXES, 2000U);
    assert(OutputGateway_RefreshAxesFollow(&g, (S5FollowEffort){0.0f, 0.2f},
        sample(SAFETY_ELBOW_AXES, 2005U, 0U, 1U), 2005U));
    step_range(&g, SAFETY_ELBOW_AXES, 2005U, 2014U);
    assert(t.count == 1U && t.event[0].axes == SAFETY_ELBOW_AXES
        && t.event[0].frames[1].id == 0x200U);
    assert(!OutputGateway_RefreshAxesFollow(&g, (S5FollowEffort){0.01f, 0.2f},
        sample(SAFETY_ELBOW_AXES, 2015U, 0U, 2U), 2015U));
    assert(g.state == OUTPUT_FAULT && t.event[t.count - 1U].stop);
}
static void pair_normal_stop(void)
{
    OutputGateway g; PairTransport t;
    ready_and_arm(&g, &t, SAFETY_ALL_AXES, 3000U);
    assert(OutputGateway_RefreshAxesFollow(&g, (S5FollowEffort){0.08f, 0.32f},
        sample(SAFETY_ALL_AXES, 3005U, 1U, 1U), 3005U));
    step_range(&g, SAFETY_ALL_AXES, 3005U, 3034U);
    assert(g.state == OUTPUT_ACTIVE && t.count >= 2U);
    assert(OutputGateway_BeginNormalStop(&g, OUTPUT_REASON_CANCEL, 3034U));
    t.block_zero = true;
    step_range(&g, SAFETY_ALL_AXES, 3035U, 3074U);
    assert(g.state == OUTPUT_RAMPING_DOWN && g.follow_zero_pending && t.hold_zero);
    unsigned before = t.count;
    step_range(&g, SAFETY_ALL_AXES, 3075U, 3075U);
    assert(g.state == OUTPUT_RAMPING_DOWN && t.count == before);
    t.hold_zero = false;
    step_range(&g, SAFETY_ALL_AXES, 3076U, 3076U);
    assert(g.state == OUTPUT_SAFE_OFF && g.stop_accepted && t.count == before + 1U);
    assert(t.event[before - 1U].frames[0].id == 0x02EU
        && t.event[before - 1U].frames[1].id == 0x200U);
    assert(t.event[before].stop && t.event[before].axes == SAFETY_ALL_AXES);

    /* The L1 owner requires positive TXOK for both normal output frames. */
    TestCanTx_Reset();
    BoardCanFrame frames[2] = {0};
    assert(MotorCommand_GdsV22Torque(1U, 0.0f, &frames[0]));
    assert(MotorCommand_C620(1U, 0.0f, &frames[1]));
    assert(BoardCanTx_SubmitOutputAxes(frames, SAFETY_ALL_AXES, false));
    assert(!BoardCanTx_OutputAxesDrained(SAFETY_ALL_AXES));
    TestCanTx_Complete(BOARD_CAN_SHOULDER);
    assert(!BoardCanTx_OutputAxesDrained(SAFETY_ALL_AXES));
    TestCanTx_Complete(BOARD_CAN_ELBOW);
    assert(BoardCanTx_OutputAxesDrained(SAFETY_ALL_AXES));
    TestCanTx_Reset();
    assert(BoardCanTx_SubmitOutputAxes(frames, SAFETY_ALL_AXES, false));
    TestCanTx_Complete(BOARD_CAN_SHOULDER);
    TestCanTx_CompleteFailed(BOARD_CAN_ELBOW);
    assert(BoardCanTx_OutputAxesFailed(SAFETY_ALL_AXES));
    assert(!BoardCanTx_OutputAxesDrained(SAFETY_ALL_AXES));
    TestCanTx_Reset();
    TestCanTx_FailNext(BOARD_CAN_SHOULDER);
    assert(!BoardCanTx_SubmitOutputAxes(frames, SAFETY_ALL_AXES, true));
    assert(TestCanTx_Sent(BOARD_CAN_SHOULDER) == 0U
        && TestCanTx_Sent(BOARD_CAN_ELBOW) == 1U);
}
static void dual_read_inflight_stop(void)
{
    S3SiteProfile p = {0};
    p.safety.stop_policy = SAFETY_STOP_SUPERVISED_DUAL_FOLLOW;
    p.safety.feedback_timeout_ms = 100U;
    p.shoulder_source = S3_SHOULDER_ENCODER_PROPERTIES;
    p.shoulder_node = S2_GDS_NODE;
    p.shoulder_property_reply_ms = 10U;
    p.shoulder_property_period_ms = 50U;
    p.shoulder_property_window_ms = 500U;
    p.shoulder_stop_window_ms = 100U;
    assert(S3ShoulderService_ReadProfileReady(&p));
    TestClock_Set(100U);
    hcan1.State = hcan2.State = HAL_CAN_STATE_READY;
    CanMonitor_Init(&p, SHOULDER_READER_S4);
    assert(S3ShoulderService_Get().state == S3_READER_IDLE);
    TestCanTx_Reset();
    uint8_t hb[8] = {0};
    hb[4] = 1U; hb[7] = 1U;
    TestCan_InjectData(BOARD_CAN_SHOULDER, 0x021U, 8U, 0U, 0U, hb);
    TestCan_Interrupt(BOARD_CAN_SHOULDER);
    CanMonitor_Poll();
    GdsFeedback observed; bool wire;
    BoardCanStats stats;
    assert(CanMonitor_GetShoulderObservation(&observed, &wire));
    assert(BoardCan_GetStats(BOARD_CAN_SHOULDER, &stats));
    assert(stats.started && stats.errors == 0U && stats.queued == 0U);
    assert(observed.has_heartbeat);
    assert(observed.life_contiguous && observed.axis_state == 1U);
    assert(S3ShoulderService_Start(100U));
    TestClock_Set(101U);
    S3ShoulderService_Poll(101U);
    assert(S3ShoulderService_Get().state == S3_READER_ACTIVE);
    assert(TestCanTx_Pending(BOARD_CAN_SHOULDER, 0U) != NULL);
    TestCanTx_AbortCompletes(BOARD_CAN_SHOULDER, false);
    OutputTransport port = OutputCanTransport_Create();
    BoardCanFrame frames[2] = {0};
    assert(MotorCommand_GdsV22Idle(S2_GDS_NODE, &frames[0]));
    assert(MotorCommand_C620(1U, 0.0f, &frames[1]));
    assert(port.submit(port.context, frames, SAFETY_ALL_AXES, true));
    assert(S3ShoulderService_Get().state == S3_READER_OBSERVING);
    assert(TestCanTx_Aborts(BOARD_CAN_SHOULDER) == 0U);
    assert(TestCanTx_Pending(BOARD_CAN_SHOULDER, 0U) != NULL);
    assert(TestCanTx_Sent(BOARD_CAN_SHOULDER) == 2U
        && TestCanTx_Sent(BOARD_CAN_ELBOW) == 1U);
}
void TestS5Gateway_Run(const char *scenario)
{
    if (strcmp(scenario, "s5_gateway_pair") == 0) { pair_request(); }
    else if (strcmp(scenario, "s5_gateway_stop") == 0) {
        pair_normal_stop(); dual_read_inflight_stop();
    }
    else { assert(!"Unknown S5 gateway scenario"); }
}
