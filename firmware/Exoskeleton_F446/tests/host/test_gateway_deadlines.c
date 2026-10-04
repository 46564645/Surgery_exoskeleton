/* Synthetic gateway boundary tests. No HAL, hardware or production site values. */
#include "test_gateway_deadlines.h"
#include "output_gateway.h"
#include <assert.h>
#include <string.h>

typedef struct {
    unsigned normals, stops, purges;
    bool fail_purge, fail_stop;
} DeadlineTransport;

static bool purge(void *context, uint8_t axes)
{
    DeadlineTransport *t = context;
    assert(axes == SAFETY_SHOULDER_AXES);
    t->purges++;
    return !t->fail_purge;
}
static bool submit(void *context, const BoardCanFrame frames[2], uint8_t axes, bool stop)
{
    DeadlineTransport *t = context;
    assert(axes == SAFETY_SHOULDER_AXES);
    if (stop) {
        assert(frames[0].id == 0x027U && frames[0].data[0] == 1U);
        t->stops++;
        return !t->fail_stop;
    }
    t->normals++;
    return true;
}
static SafetyInputs healthy(uint32_t now_ms)
{
    SafetyInputs in = {0};
    in.transport_healthy = in.operator_stop_clear = in.drivers_ready = true;
    in.independently_stopped = true;
    in.joint[0] = (JointState){0.0f, 0.0f, now_ms, true};
    return in;
}
static OutputDriverStatus driver(uint32_t now_ms, uint8_t state)
{
    OutputDriverStatus d = {0};
    d.received_ms = now_ms; d.node = 1U; d.axis_state = state;
    d.valid = d.calibration_verified = true;
    return d;
}
static void init(OutputGateway *g, DeadlineTransport *t, SafetyStopPolicy policy)
{
    SafetyConfig c = {0};
    c.enabled_axes = SAFETY_SHOULDER_AXES; c.stop_policy = policy;
    c.protection_confirmed = c.stop_method_confirmed = c.timing_confirmed = true;
    c.feedback_timeout_ms = 1000U; c.service_limit_ms = 250U; c.max_request_ms = 20U;
    c.joint[0].mapping = (JointMapping){1.0f, 0.0f, 1, true};
    c.joint[0].lower_rad = -1.0f; c.joint[0].upper_rad = 1.0f;
    c.joint[0].max_speed_rad_s = 2.0f; c.joint[0].max_motor_command = 0.05f;
    c.joint[0].motor_slew_per_s = 1.0f;
    c.joint[0].limits_confirmed = c.joint[0].feedback_confirmed = c.joint[0].protocol_confirmed = true;
    memset(t, 0, sizeof(*t));
    OutputTransport port = {purge, submit, t};
    OutputGateway_Init(g, &c, &port, 1U, 1U);
}
static void ready(OutputGateway *g, DeadlineTransport *t, SafetyStopPolicy policy, uint32_t start_ms)
{
    init(g, t, policy);
    SafetyInputs in = healthy(start_ms);
    OutputDriverStatus d[2] = {driver(start_ms, 1U), {0}};
    assert(OutputGateway_Prepare(g, &in, d, 200U, start_ms));
    in = healthy(start_ms + 1U); d[0].received_ms = start_ms + 1U;
    d[0].zero_verified = true; d[0].zero_verified_ms = start_ms + 1U;
    OutputGateway_PollPreparation(g, &in, d, start_ms + 1U);
    assert(g->driver_phase == DRIVER_WAIT_MODE);
    in = healthy(start_ms + 2U); d[0].received_ms = start_ms + 2U;
    d[0].mode_verified = true; d[0].mode_verified_ms = start_ms + 2U;
    OutputGateway_PollPreparation(g, &in, d, start_ms + 2U);
    assert(g->driver_phase == DRIVER_WAIT_CLOSED_LOOP);
    in = healthy(start_ms + 3U); d[0].received_ms = start_ms + 3U; d[0].axis_state = 8U;
    OutputGateway_PollPreparation(g, &in, d, start_ms + 3U);
    assert(g->state == OUTPUT_READY && t->normals == 3U);
}
static void ready_deadline(uint32_t start_ms)
{
    OutputGateway g; DeadlineTransport t;
    ready(&g, &t, SAFETY_STOP_EXTERNAL_INPUT, start_ms);
    SafetyInputs in = healthy(start_ms + 199U);
    assert(OutputGateway_Arm(&g, &in, start_ms + 199U));
    for (uint32_t age = 200U; age <= 201U; age++) {
        ready(&g, &t, SAFETY_STOP_EXTERNAL_INPUT, start_ms);
        in = healthy(start_ms + age);
        assert(!OutputGateway_Arm(&g, &in, start_ms + age));
        assert(g.state == OUTPUT_FAULT && g.reason == OUTPUT_REASON_EXPIRED);
        assert(!g.request_valid && g.stop_attempted && g.stop_accepted && t.stops == 1U);
    }
}
static void first_deadline(uint32_t armed_ms)
{
    OutputGateway g; DeadlineTransport t;
    init(&g, &t, SAFETY_STOP_EXTERNAL_INPUT);
    SafetyInputs in = healthy(armed_ms);
    assert(OutputGateway_Arm(&g, &in, armed_ms));
    in = healthy(armed_ms + 19U); OutputGateway_Step(&g, &in, armed_ms + 19U);
    assert(OutputGateway_Replace(&g, 0U, 0.02f, 20U, armed_ms + 19U));
    for (uint32_t age = 20U; age <= 21U; age++) {
        init(&g, &t, SAFETY_STOP_EXTERNAL_INPUT); g.config.service_limit_ms = 5U;
        in = healthy(armed_ms); assert(OutputGateway_Arm(&g, &in, armed_ms));
        for (uint32_t step = 1U; step < 20U; step++) {
            in = healthy(armed_ms + step); OutputGateway_Step(&g, &in, armed_ms + step);
        }
        assert(g.state == OUTPUT_ARMED && !g.request_valid);
        assert(!OutputGateway_Replace(&g, 0U, 0.02f, 20U, armed_ms + age));
        assert(g.state == OUTPUT_FAULT && g.reason == OUTPUT_REASON_EXPIRED);
        assert(!g.request_valid && g.stop_attempted && g.stop_accepted && t.stops == 1U);
    }
}
#ifndef TEST_GATEWAY_DEADLINES_BASELINE
static void shoulder_once(void)
{
    OutputGateway g; DeadlineTransport t;
    init(&g, &t, SAFETY_STOP_SUPERVISED_SHOULDER_BENCH);
    SafetyInputs in = healthy(100U);
    assert(!OutputGateway_Arm(&g, &in, 100U) && t.normals == 0U);
    ready(&g, &t, SAFETY_STOP_SUPERVISED_SHOULDER_BENCH, 100U);
    in = healthy(104U); assert(OutputGateway_Arm(&g, &in, 104U));
    assert(OutputGateway_Replace(&g, 0U, 0.02f, 20U, 104U));
    assert(g.shoulder_attempted);
    in = healthy(105U); OutputGateway_Step(&g, &in, 105U);
    assert(g.state == OUTPUT_ACTIVE && t.normals == 4U);
    assert(!OutputGateway_Replace(&g, 0U, 0.02f, 20U, 106U));
    assert(g.state == OUTPUT_FAULT && t.stops == 1U);
    assert(!OutputGateway_AcknowledgeFault(&g, &in, 106U, true));

    ready(&g, &t, SAFETY_STOP_SUPERVISED_SHOULDER_BENCH, 200U);
    assert(!g.shoulder_attempted); /* Cold Init, never an online reset. */
    in = healthy(204U); assert(OutputGateway_Arm(&g, &in, 204U));
    assert(OutputGateway_Replace(&g, 0U, 0.02f, 20U, 204U));
    OutputGateway_Cancel(&g);
    assert(g.state == OUTPUT_SAFE_OFF && g.shoulder_attempted && t.stops == 1U);
    OutputDriverStatus d[2] = {driver(205U, 1U), {0}};
    in = healthy(205U); unsigned normals = t.normals;
    assert(!OutputGateway_Prepare(&g, &in, d, 200U, 205U));
    assert(!OutputGateway_Arm(&g, &in, 205U) && t.normals == normals);

    ready(&g, &t, SAFETY_STOP_SUPERVISED_SHOULDER_BENCH, 300U);
    in = healthy(304U); assert(OutputGateway_Arm(&g, &in, 304U));
    assert(OutputGateway_Replace(&g, 0U, 0.02f, 20U, 304U));
    in = healthy(324U); OutputGateway_Step(&g, &in, 324U);
    assert(g.state == OUTPUT_FAULT && g.shoulder_attempted && t.stops == 1U);
    assert(!OutputGateway_AcknowledgeFault(&g, &in, 324U, true));

    ready(&g, &t, SAFETY_STOP_SUPERVISED_SHOULDER_BENCH, 400U);
    in = healthy(404U); assert(OutputGateway_Arm(&g, &in, 404U));
    t.fail_stop = true; OutputGateway_Cancel(&g);
    assert(g.state == OUTPUT_FAULT && g.stop_delivery_failed && !g.shoulder_attempted);
    assert(!OutputGateway_AcknowledgeFault(&g, &in, 404U, true));
}
#endif
void TestGatewayDeadlines_Run(const char *name)
{
    if (!strcmp(name, "s3_gateway_ready_deadline")) {
        ready_deadline(100U); ready_deadline(UINT32_MAX - 100U);
    } else if (!strcmp(name, "s3_gateway_first_deadline")) {
        first_deadline(100U); first_deadline(UINT32_MAX - 10U);
    } else if (!strcmp(name, "s3_gateway_ready_deadline_wrap")) {
        ready_deadline(UINT32_MAX - 100U);
    } else if (!strcmp(name, "s3_gateway_first_deadline_wrap")) {
        first_deadline(UINT32_MAX - 10U);
#ifndef TEST_GATEWAY_DEADLINES_BASELINE
    } else if (!strcmp(name, "s3_gateway_shoulder_once")) {
        shoulder_once();
#endif
    } else { assert(false); }
}
#ifdef TEST_GATEWAY_DEADLINES_MAIN
#include <stdio.h>
#if defined(_MSC_VER)
#include <crtdbg.h>
#include <stdlib.h>
#endif
int main(int argc, char **argv)
{
#if defined(_MSC_VER)
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0U, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    assert(argc == 2);
    TestGatewayDeadlines_Run(argv[1]);
    printf("PASS %s\n", argv[1]);
    return 0;
}
#endif
