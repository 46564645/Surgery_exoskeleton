/* Decision-critical S2 read scenarios; only host CAN HAL is exercised. */
#include "test_sdo.h"
#include "test_s3.h"
#include "gds_sdo_read.h"
#include "shoulder_read_diagnostic.h"
#include "app_main.h"
#include "can_monitor.h"
#include "can_mock.h"
#include "can_tx_mock.h"
#include "s3_site_mock.h"
#include "s3_bench.h"
#include <assert.h>
#include <math.h>
#include <string.h>

static bool booted;
static uint8_t heartbeat_life;

static ShoulderReadStatus snapshot(void)
{
    ShoulderReadStatus s;
    ShoulderRead_GetStatus(&s);
    return s;
}

static void reset(uint32_t now)
{
    TestClock_Set(now);
    TestS3Site_Reset();
    if (!booted) { App_Init(); booted = true; }
    else { CanMonitor_Init(); S3Bench_Init(); }
    ShoulderRead_Init();
    TestCanTx_Reset();
    heartbeat_life = 0U;
}

static void inject(const BoardCanFrame *f, uint32_t now)
{
    TestClock_Set(now);
    TestCan_InjectData(BOARD_CAN_SHOULDER, f->id, f->dlc,
                       CAN_ID_STD, CAN_RTR_DATA, f->data);
    TestCan_Interrupt(BOARD_CAN_SHOULDER);
    CanMonitor_Poll();
}

static void heartbeat(uint32_t now, uint8_t state, uint8_t error, uint8_t fault)
{
    BoardCanFrame f = {0};
    f.id = 0x021U; f.dlc = 8U;
    f.data[0] = error; f.data[4] = state; f.data[5] = fault;
    f.data[7] = ++heartbeat_life;
    inject(&f, now);
}

static BoardCanFrame response(GdsReadField field, uint32_t now, float value)
{
    BoardCanFrame f;
    assert(GdsSdoRead_Request(field, &f));
    f.id = 0x025U; f.received_ms = now;
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    for (unsigned i = 0U; i < 4U; i++) {
        f.data[4U + i] = (uint8_t)(bits >> (8U * i));
    }
    return f;
}

static void codec(void)
{
    const uint8_t requests[2][8] = {
        {0,0x74,1,0,0,0,0,0}, {0,0x7A,1,0,0,0,0,0}
    };
    for (unsigned i = 0U; i < 2U; i++) {
        GdsReadField field = (GdsReadField)i;
        BoardCanFrame f;
        assert(GdsSdoRead_Request(field, &f));
        assert(f.id == 0x024U && f.dlc == 8U && f.received_ms == 0U);
        assert(memcmp(f.data, requests[i], sizeof(f.data)) == 0);
        f = response(field, 10U, -1.25f);
        const uint8_t negative[4] = {0,0,0xA0,0xBF};
        assert(memcmp(f.data + 4, negative, sizeof(negative)) == 0);
        float value = 99.0f;
        assert(GdsSdoRead_Decode(field, BOARD_CAN_SHOULDER, &f, &value));
        assert(value == -1.25f);
        for (unsigned bad = 0U; bad < 5U; bad++) {
            BoardCanFrame malformed = f;
            if (bad == 0U) { malformed.id = 0x045U; }
            else if (bad == 1U) { malformed.dlc = 7U; }
            else if (bad == 2U) { malformed.data[0] = 1U; }
            else if (bad == 3U) { malformed.data[3] = 1U; }
            else { malformed.data[1] ^= 1U; }
            value = 99.0f;
            assert(!GdsSdoRead_Decode(field, BOARD_CAN_SHOULDER, &malformed, &value));
            assert(value == 99.0f);
        }
        assert(!GdsSdoRead_Decode(field, BOARD_CAN_ELBOW, &f, &value));
        f = response(field, 10U, INFINITY);
        assert(!GdsSdoRead_Decode(field, BOARD_CAN_SHOULDER, &f, &value));
        f = response(field, 10U, NAN);
        assert(!GdsSdoRead_Decode(field, BOARD_CAN_SHOULDER, &f, &value));
    }
    BoardCanFrame f;
    assert(!GdsSdoRead_Request((GdsReadField)2, &f));
}

static void lifecycle(void)
{
    reset(100U); heartbeat(100U, 1U, 0U, 0U);
    GdsFeedback observed; bool wire;
    assert(CanMonitor_GetShoulderObservation(&observed, &wire));
    assert(observed.has_heartbeat && !observed.has_estimate && !wire);
    /* CAN2 is deliberately unusable: selected shoulder TX must still work. */
    TestCanTx_Busy(BOARD_CAN_ELBOW, false);
    hcan2.State = HAL_CAN_STATE_ERROR; hcan2.Instance->ESR = CAN_ESR_BOFF;
    assert(ShoulderRead_Start(GDS_READ_POSITION, 100U));
    assert(TestCanTx_Sent(BOARD_CAN_SHOULDER) == 1U);
    assert(TestCanTx_Sent(BOARD_CAN_ELBOW) == 0U && TestCanTx_Aborts(BOARD_CAN_ELBOW) == 0U);
    assert(TestCanTx_Last(BOARD_CAN_SHOULDER)->id == 0x024U);
    assert(!ShoulderRead_Start(GDS_READ_VELOCITY, 100U));
    BoardCanFrame f = response(GDS_READ_POSITION, 100U, 2.5f);
    inject(&f, 100U); /* Same tick as submission is a legitimate roundtrip. */
    ShoulderReadStatus s = snapshot();
    assert(s.phase == SHOULDER_READ_RECEIVED && s.accepted && s.has_value && s.has_raw);
    assert(s.sequence == 1U && s.value == 2.5f && s.received_ms == 100U);
    BoardCanFrame matched = s.matched;
    f.id = 0x029U; memset(f.data, 0, sizeof(f.data)); inject(&f, 101U);
    f = response(GDS_READ_VELOCITY, 102U, 42.0f); inject(&f, 102U);
    s = snapshot();
    assert(s.phase == SHOULDER_READ_RECEIVED && s.value == 2.5f);
    assert(memcmp(&s.matched, &matched, sizeof(matched)) == 0);
    assert(s.raw.received_ms == 102U && s.raw.data[1] == 0x7AU);
    assert(!ShoulderRead_Start(GDS_READ_POSITION, 102U));
    ShoulderRead_Poll(500U);
    assert(TestCanTx_Sent(BOARD_CAN_SHOULDER) == 1U); /* No periodic sends. */
    /* The host TX seam has no completion callback; reset only its HAL state. */
    TestCanTx_Reset(); heartbeat(501U, 1U, 0U, 0U);
    assert(ShoulderRead_Start(GDS_READ_VELOCITY, 501U));
    f = response(GDS_READ_POSITION, 501U, 8.0f); inject(&f, 501U);
    assert(snapshot().phase == SHOULDER_READ_PENDING && snapshot().rejected == 1U);
    f = response(GDS_READ_VELOCITY, 502U, -0.5f); inject(&f, 502U);
    s = snapshot();
    assert(s.sequence == 2U && s.used_fields == 3U && s.value == -0.5f);
    assert(!ShoulderRead_Start(GDS_READ_POSITION, 502U));
    assert(!ShoulderRead_Start(GDS_READ_VELOCITY, 502U));

    reset(1000U); heartbeat(1000U, 1U, 0U, 0U);
    assert(ShoulderRead_Start(GDS_READ_POSITION, 1000U));
    f = response(GDS_READ_POSITION, 999U, 7.0f);
    ShoulderRead_Receive(BOARD_CAN_SHOULDER, &f, 1000U);
    assert(snapshot().phase == SHOULDER_READ_PENDING && !snapshot().has_value);
    ShoulderRead_Poll(1250U);
    assert(snapshot().phase == SHOULDER_READ_PENDING);
    ShoulderRead_Poll(1251U);
    s = snapshot(); assert(s.phase == SHOULDER_READ_TIMEOUT && s.quarantined);
    f = response(GDS_READ_POSITION, 1251U, 7.0f); inject(&f, 1251U);
    assert(snapshot().phase == SHOULDER_READ_TIMEOUT && !snapshot().has_value);
    assert(!ShoulderRead_Start(GDS_READ_POSITION, 1251U));
    assert(!ShoulderRead_Start(GDS_READ_VELOCITY, 1251U));
    ShoulderRead_Poll(2000U);
    assert(TestCanTx_Sent(BOARD_CAN_SHOULDER) == 1U);

    reset(UINT32_MAX - 10U); heartbeat(UINT32_MAX - 10U, 1U, 0U, 0U);
    assert(ShoulderRead_Start(GDS_READ_POSITION, UINT32_MAX - 10U));
    f = response(GDS_READ_POSITION, 3U, 1.0f); inject(&f, 3U);
    assert(snapshot().phase == SHOULDER_READ_RECEIVED && snapshot().received_ms == 3U);
    reset(UINT32_MAX - 10U); heartbeat(UINT32_MAX - 10U, 1U, 0U, 0U);
    assert(ShoulderRead_Start(GDS_READ_POSITION, UINT32_MAX - 10U));
    ShoulderRead_Poll(240U);
    assert(snapshot().phase == SHOULDER_READ_TIMEOUT && snapshot().quarantined);
}

static void blocked(uint32_t now)
{
    assert(!ShoulderRead_Start(GDS_READ_POSITION, now));
    assert(TestCanTx_Sent(BOARD_CAN_SHOULDER) == 0U);
    assert(TestCanTx_Sent(BOARD_CAN_ELBOW) == 0U);
    assert(snapshot().sequence == 0U && snapshot().used_fields == 0U);
}

static void interlocks(void)
{
    reset(100U); blocked(100U); /* No heartbeat. */
    heartbeat(100U, 1U, 0U, 0U); blocked(201U); /* Stale heartbeat. */
    heartbeat(202U, 8U, 0U, 0U); blocked(202U);
    heartbeat(203U, 1U, 1U, 0U); blocked(203U);
    heartbeat(204U, 1U, 0U, 1U); blocked(204U);
    heartbeat(205U, 1U, 0U, 0U);
    TestCan_Inject(BOARD_CAN_SHOULDER, 0x029U, 8U, CAN_ID_STD, CAN_RTR_DATA, 0U);
    TestCan_Interrupt(BOARD_CAN_SHOULDER); blocked(205U); /* RX ownership/backlog. */
    CanMonitor_Poll();
    hcan1.Instance->ESR = CAN_ESR_BOFF; blocked(205U);
    hcan1.Instance->ESR = 0U;
    SafetyConfig c = TestS3_ConfirmedConfig();
    TestS3Site_Configure(&c); S3Bench_Init();
    S3BenchStatus gate; S3Bench_GetStatus(&gate);
    assert(gate.configured && gate.transport_bound && !gate.output_allowed);
    blocked(205U); /* Even SAFE_OFF cannot share a bound S3 transport. */
    TestS3Site_Reset(); S3Bench_Init();
    /* Healthy observation plus unconfigured, unbound S3 permits a read. */
    TestCanTx_FailNext(BOARD_CAN_SHOULDER);
    assert(!ShoulderRead_Start(GDS_READ_POSITION, 205U));
    ShoulderReadStatus s = snapshot();
    assert(s.phase == SHOULDER_READ_TX_FAILED && !s.accepted && s.quarantined);
    assert(s.sequence == 1U && s.used_fields == 1U && !s.has_value);
    assert(!ShoulderRead_Start(GDS_READ_VELOCITY, 205U));
    ShoulderRead_Poll(1000U);
    assert(TestCanTx_Sent(BOARD_CAN_SHOULDER) == 0U && TestCanTx_Sent(BOARD_CAN_ELBOW) == 0U);
}

void TestSdo_Run(const char *name)
{
    if (strcmp(name, "s2_sdo_codec") == 0) { codec(); }
    else if (strcmp(name, "s2_sdo_lifecycle") == 0) { lifecycle(); }
    else if (strcmp(name, "s2_sdo_interlocks") == 0) { interlocks(); }
    else { assert(!"Unknown SDO scenario"); }
}
