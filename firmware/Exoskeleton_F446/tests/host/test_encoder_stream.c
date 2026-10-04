/* Bounded bench observation scenarios. All transport is the host HAL seam. */
#include "test_encoder_stream.h"
#include "encoder_stream.h"
#include "shoulder_read_diagnostic.h"
#include "app_main.h"
#include "can_monitor.h"
#include "can_mock.h"
#include "can_tx_mock.h"
#include "s3_site_mock.h"
#include "s3_bench.h"
#include "test_s3.h"
#include <assert.h>
#include <math.h>
#include <string.h>

static bool booted;
static uint8_t life;
static EncoderStreamStatus snapshot(uint32_t now)
{
    EncoderStreamStatus s;
    EncoderStream_GetStatus(now, &s);
    return s;
}
static void reset(uint32_t now)
{
    TestClock_Set(now); TestS3Site_Reset();
    if (!booted) { App_Init(); booted = true; }
    else { CanMonitor_Init(); S3Bench_Init(); }
    ShoulderRead_Init(); EncoderStream_Init(); TestCanTx_Reset(); life = 0U;
}
static void heartbeat(uint32_t now, uint8_t state, uint8_t error, uint8_t fault)
{
    uint8_t data[8] = {0};
    data[0] = error; data[4] = state; data[5] = fault; data[7] = ++life;
    TestClock_Set(now);
    TestCan_InjectData(BOARD_CAN_SHOULDER, 0x021U, 8U, CAN_ID_STD, CAN_RTR_DATA, data);
    TestCan_Interrupt(BOARD_CAN_SHOULDER); CanMonitor_Poll();
}
static BoardCanFrame response(GdsReadField field, uint32_t now, float value)
{
    BoardCanFrame f;
    assert(GdsSdoRead_Request(field, &f));
    f.id = 0x025U; f.received_ms = now;
    uint32_t bits; memcpy(&bits, &value, sizeof(bits));
    for (unsigned i = 0U; i < 4U; i++) { f.data[4U + i] = (uint8_t)(bits >> (8U * i)); }
    return f;
}
static void complete_tx(void)
{
    /* The seam has no hardware completion IRQ. Reset only its TX registers;
     * stream counters/state remain intact and no production abort is called. */
    assert(TestCanTx_Sent(BOARD_CAN_ELBOW) == 0U && TestCanTx_Aborts(BOARD_CAN_ELBOW) == 0U);
    TestCanTx_Reset();
}
static void receive(GdsReadField field, uint32_t now, float value)
{
    BoardCanFrame f = response(field, now, value);
    TestClock_Set(now); EncoderStream_Receive(BOARD_CAN_SHOULDER, &f, now);
    complete_tx();
}
static void pair(uint32_t now, float position, float velocity)
{
    EncoderStreamStatus s = snapshot(now);
    assert(s.active && s.pending && TestCanTx_Last(BOARD_CAN_SHOULDER)->data[1] == 0x74U);
    receive(GDS_READ_POSITION, now, position);
    EncoderStream_Poll(now);
    assert(snapshot(now).pending && TestCanTx_Last(BOARD_CAN_SHOULDER)->data[1] == 0x7AU);
    receive(GDS_READ_VELOCITY, now + 1U, velocity);
    assert(snapshot(now + 1U).has_pair && snapshot(now + 1U).valid);
}
static void assert_finished(uint32_t now, EncoderStreamReason reason)
{
    EncoderStreamStatus s = snapshot(now);
    assert(s.used && !s.active && !s.pending && !s.valid && s.reason == reason);
    uint32_t requests = s.requests;
    EncoderStream_Poll(now + 1000U);
    assert(snapshot(now + 1000U).requests == requests);
    assert(!EncoderStream_Start(now + 1000U));
    assert(!ShoulderRead_Start(GDS_READ_POSITION, now + 1000U));
}
static void lifecycle(void)
{
    reset(100U); heartbeat(100U, 1U, 0U, 0U); EncoderStream_Poll(100U);
    assert(!snapshot(100U).used && !snapshot(100U).active);
    assert(TestCanTx_Sent(BOARD_CAN_SHOULDER) == 0U); /* No automatic start. */
    TestCanTx_Busy(BOARD_CAN_ELBOW, false);
    hcan2.State = HAL_CAN_STATE_ERROR; hcan2.Instance->ESR = CAN_ESR_BOFF;
    assert(EncoderStream_Start(100U));
    assert(snapshot(100U).requests == 1U && !snapshot(100U).has_pair);
    assert(TestCanTx_Last(BOARD_CAN_SHOULDER)->id == 0x024U);
    assert(!ShoulderRead_Start(GDS_READ_POSITION, 100U));
    receive(GDS_READ_POSITION, 100U, 2.0f);
    assert(!snapshot(100U).has_pair); /* Position alone is never published. */
    EncoderStream_Poll(100U);
    receive(GDS_READ_VELOCITY, 101U, -0.5f);
    EncoderStreamStatus first = snapshot(101U);
    assert(first.valid && first.pairs == 1U && first.requests == 2U);
    assert(first.position_rev == 2.0f && first.velocity_rev_s == -0.5f);
    assert(fabsf(first.position_rad - 12.5663706f) < 0.00001f);
    assert(fabsf(first.velocity_rad_s + 3.1415927f) < 0.00001f);
    EncoderStream_Poll(149U);
    assert(snapshot(149U).requests == 2U); /* One pair cycle per >=50ms. */
    EncoderStream_Poll(150U);
    assert(snapshot(150U).requests == 3U);
    receive(GDS_READ_POSITION, 150U, 3.0f);
    EncoderStreamStatus partial = snapshot(150U);
    assert(partial.pairs == 1U && partial.position_rev == 2.0f && partial.velocity_rev_s == -0.5f);
    assert(memcmp(&partial.position_frame, &first.position_frame, sizeof(BoardCanFrame)) == 0);
    EncoderStream_Poll(150U); receive(GDS_READ_VELOCITY, 151U, 0.25f);
    assert(snapshot(151U).pairs == 2U && snapshot(151U).position_rev == 3.0f);
    /* A 100ms heartbeat with 10ms jitter must not terminate fresh pairs.
     * A missed slot produces one fresh cycle, with no catch-up replay. */
    EncoderStream_Poll(209U);
    assert(snapshot(209U).active && snapshot(209U).requests == 5U);
    pair(209U, 4.0f, 0.0f);
    heartbeat(210U, 1U, 0U, 0U);
    EncoderStream_Poll(258U); assert(snapshot(258U).requests == 6U);
    EncoderStream_Stop(258U); assert_finished(258U, ENCODER_STREAM_USER);
    BoardCanFrame late = response(GDS_READ_POSITION, 259U, 9.0f);
    EncoderStream_Receive(BOARD_CAN_SHOULDER, &late, 259U);
    assert(snapshot(259U).pairs == 3U && snapshot(259U).position_rev == 4.0f);

    reset(100U); heartbeat(100U, 1U, 0U, 0U);
    assert(ShoulderRead_Start(GDS_READ_POSITION, 100U));
    assert(!EncoderStream_Start(100U) && !snapshot(100U).used);
    reset(100U); heartbeat(100U, 1U, 0U, 0U); assert(EncoderStream_Start(100U));
    /* Exercise the entire 600-pair/1200-request session at its fastest rate. */
    for (unsigned cycle = 0U; cycle < 600U; cycle++) {
        uint32_t now = 100U + cycle * ENCODER_STREAM_PERIOD_MS;
        if (cycle != 0U) { heartbeat(now, 1U, 0U, 0U); EncoderStream_Poll(now); }
        pair(now, 1.0f, 0.0f);
        assert(snapshot(now + 1U).requests == 2U * (cycle + 1U));
        assert(snapshot(now + 1U).requests <= ENCODER_STREAM_MAX_REQUESTS);
    }
    assert(snapshot(30051U).pairs == 600U && snapshot(30051U).requests == 1200U);
    EncoderStream_Poll(30100U); assert_finished(30100U, ENCODER_STREAM_LIMIT);

    reset(UINT32_MAX - 10U); heartbeat(UINT32_MAX - 10U, 1U, 0U, 0U);
    assert(EncoderStream_Start(UINT32_MAX - 10U));
    receive(GDS_READ_POSITION, UINT32_MAX - 10U, 1.0f);
    EncoderStream_Poll(UINT32_MAX - 10U); receive(GDS_READ_VELOCITY, 3U, 0.0f);
    assert(snapshot(3U).valid && snapshot(3U).age_ms == 14U);
    EncoderStream_Stop(4U); assert_finished(4U, ENCODER_STREAM_USER);
}

static void faults(void)
{
    reset(100U); assert(!EncoderStream_Start(100U)); /* No fresh IDLE heartbeat. */
    heartbeat(100U, 8U, 0U, 0U); assert(!EncoderStream_Start(100U));
    reset(100U); heartbeat(100U, 1U, 0U, 0U); assert(EncoderStream_Start(100U));
    EncoderStream_Poll(150U); assert(snapshot(150U).active && snapshot(150U).pending);
    EncoderStream_Poll(151U); assert_finished(151U, ENCODER_STREAM_TIMEOUT);
    BoardCanFrame f = response(GDS_READ_POSITION, 152U, 1.0f);
    EncoderStream_Receive(BOARD_CAN_SHOULDER, &f, 152U);
    assert(!snapshot(152U).has_pair);
    for (unsigned bad = 0U; bad < 3U; bad++) {
        reset(100U); heartbeat(100U, 1U, 0U, 0U); assert(EncoderStream_Start(100U));
        f = response(bad == 0U ? GDS_READ_VELOCITY : GDS_READ_POSITION, 100U,
                     bad == 2U ? NAN : 1.0f);
        if (bad == 1U) { f.received_ms = 99U; }
        EncoderStream_Receive(BOARD_CAN_SHOULDER, &f, 100U);
        assert_finished(100U, ENCODER_STREAM_REPLY);
        assert(snapshot(100U).rejected == 1U && !snapshot(100U).has_pair);
    }
    reset(100U); heartbeat(100U, 1U, 0U, 0U); assert(EncoderStream_Start(100U));
    pair(100U, 2.0f, 0.0f);
    heartbeat(200U, 1U, 0U, 0U); EncoderStream_Poll(200U);
    assert(snapshot(200U).valid && snapshot(200U).age_ms == 100U);
    assert(!snapshot(201U).valid && snapshot(201U).age_ms == 101U);
    EncoderStream_Poll(251U); assert_finished(251U, ENCODER_STREAM_TIMEOUT);

    for (unsigned bad = 0U; bad < 3U; bad++) {
        reset(100U); heartbeat(100U, 1U, 0U, 0U); assert(EncoderStream_Start(100U));
        if (bad == 0U) { heartbeat(110U, 8U, 0U, 0U); }
        else if (bad == 1U) { heartbeat(110U, 1U, 1U, 0U); }
        else { heartbeat(110U, 1U, 0U, 1U); }
        EncoderStream_Poll(110U); assert_finished(110U, ENCODER_STREAM_HEARTBEAT);
    }
    reset(100U); heartbeat(100U, 1U, 0U, 0U);
    uint32_t heartbeat_boundary = 100U + ENCODER_STREAM_HEARTBEAT_MS;
    TestClock_Set(heartbeat_boundary);
    assert(EncoderStream_Start(heartbeat_boundary));
    assert(snapshot(heartbeat_boundary).active); /* Heartbeat age 250ms allowed. */
    EncoderStream_Poll(heartbeat_boundary + 1U);
    assert_finished(heartbeat_boundary + 1U, ENCODER_STREAM_HEARTBEAT);
    reset(100U); heartbeat(100U, 1U, 0U, 0U);
    TestClock_Set(heartbeat_boundary + 1U);
    assert(!EncoderStream_Start(heartbeat_boundary + 1U)); /* Age 251ms denied. */
    assert(!snapshot(heartbeat_boundary + 1U).used);
    reset(100U); heartbeat(100U, 1U, 0U, 0U); assert(EncoderStream_Start(100U));
    SafetyConfig c = TestS3_ConfirmedConfig(); TestS3Site_Configure(&c); S3Bench_Init();
    EncoderStream_Poll(110U); assert_finished(110U, ENCODER_STREAM_OWNER);

    reset(100U); heartbeat(100U, 1U, 0U, 0U);
    hcan1.ErrorCode = HAL_CAN_ERROR_ACK; HAL_CAN_ErrorCallback(&hcan1);
    assert(EncoderStream_Start(100U)); /* Historical counters are the baseline. */
    hcan1.ErrorCode = HAL_CAN_ERROR_ACK; HAL_CAN_ErrorCallback(&hcan1);
    EncoderStream_Poll(101U); assert_finished(101U, ENCODER_STREAM_CAN);
    reset(100U); heartbeat(100U, 1U, 0U, 0U); assert(EncoderStream_Start(100U));
    TestCan_Inject(BOARD_CAN_SHOULDER, 0x029U, 8U, CAN_ID_EXT, CAN_RTR_DATA, 0U);
    TestCan_Interrupt(BOARD_CAN_SHOULDER); EncoderStream_Poll(101U);
    assert_finished(101U, ENCODER_STREAM_CAN);
    reset(100U); heartbeat(100U, 1U, 0U, 0U); TestCanTx_FailNext(BOARD_CAN_SHOULDER);
    assert(!EncoderStream_Start(100U)); assert_finished(100U, ENCODER_STREAM_TX);
}
void TestEncoderStream_Run(const char *name)
{
    if (strcmp(name, "s2_stream_lifecycle") == 0) { lifecycle(); }
    else if (strcmp(name, "s2_stream_faults") == 0) { faults(); }
    else { assert(!"Unknown encoder stream scenario"); }
}
