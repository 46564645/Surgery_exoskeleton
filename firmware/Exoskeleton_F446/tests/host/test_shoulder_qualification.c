/* Synthetic real CAN/SDO/owner path; no physical qualification claim. */
#include "test_shoulder_qualification.h"
#include "shoulder_qualification.h"
#include "shoulder_read_diagnostic.h"
#include "encoder_stream.h"
#include "app_main.h"
#include "can_monitor.h"
#include "can_mock.h"
#include "can_tx_mock.h"
#include "board_console.h"
#include "s3_site_mock.h"
#include "s3_bench.h"
#include "test_s3.h"
#include <assert.h>
#include <string.h>
extern UART_HandleTypeDef huart2;
static bool booted;
static uint8_t life;
static ShoulderQualificationStatus snapshot(void)
{ ShoulderQualificationStatus s; ShoulderQualification_GetStatus(&s); return s; }
static void boot(uint32_t now_ms)
{
    /* Host fixtures emulate MCU boots. Runtime qualification never calls Init. */
    TestClock_Set(now_ms); TestS3Site_Reset();
    if (!booted) { App_Init(); booted = true; }
    else { CanMonitor_Init(S3Site_GetProfile(), SHOULDER_READER_S3); S3Bench_Init(); }
    TestCanTx_Reset(); life = 0U;
}
static void queue(const BoardCanFrame *f)
{
    TestCan_InjectData(BOARD_CAN_SHOULDER, f->id, f->dlc,
                       CAN_ID_STD, CAN_RTR_DATA, f->data);
}
static void inject(const BoardCanFrame *f, uint32_t now_ms)
{
    TestClock_Set(now_ms); queue(f); TestCan_Interrupt(BOARD_CAN_SHOULDER); CanMonitor_Poll();
}
static BoardCanFrame heartbeat_frame(uint8_t state, uint8_t error)
{
    BoardCanFrame f = {0}; f.id = 0x021U; f.dlc = 8U;
    f.data[0] = error; f.data[4] = state; f.data[7] = ++life; return f;
}
static void heartbeat(uint32_t now_ms)
{ BoardCanFrame f = heartbeat_frame(1U, 0U); inject(&f, now_ms); }
static void start(uint32_t now_ms)
{
    boot(now_ms); heartbeat(now_ms);
    assert(ShoulderQualification_Start(now_ms));
    ShoulderReadStatus owner; ShoulderRead_GetStatus(&owner);
    assert(owner.phase == SHOULDER_READ_QUALIFICATION_OWNED);
    assert(snapshot().claimed && snapshot().pending && snapshot().attempts == 1U);
}
static BoardCanFrame response(GdsPropertyField field, uint32_t now_ms)
{
    BoardCanFrame f; assert(GdsSdoRead_PropertyRequest(field, &f));
    f.id = 0x025U; f.received_ms = now_ms;
    uint32_t bits = 1U;
    if (field == GDS_PROPERTY_POSITION || field == GDS_PROPERTY_VELOCITY
        || field == GDS_PROPERTY_INPUT_TORQUE) {
        float value = field == GDS_PROPERTY_POSITION ? 3.25f
            : field == GDS_PROPERTY_VELOCITY ? -0.125f : 0.0f;
        memcpy(&bits, &value, sizeof(bits));
    }
    for (unsigned i = 0U; i < 4U; i++) { f.data[4U + i] = (uint8_t)(bits >> (8U * i)); }
    return f;
}
static void no_more_tx(unsigned sent, uint32_t now_ms)
{
    ShoulderQualification_Poll(now_ms);
    assert(!ShoulderQualification_Start(now_ms));
    assert(TestCanTx_Sent(BOARD_CAN_SHOULDER) == sent);
    assert(TestCanTx_Sent(BOARD_CAN_ELBOW) == 0U && TestCanTx_Aborts(BOARD_CAN_ELBOW) == 0U);
}
static void success(void)
{
    const uint16_t endpoints[GDS_PROPERTY_COUNT] = {372U,378U,195U,366U,272U,287U,288U};
    /* Wrap across the seven replies and the strictly later closing heartbeat. */
    uint32_t begin = UINT32_MAX - 3U; start(begin);
    S3BenchStatus gate; S3Bench_GetStatus(&gate);
    assert(!gate.configured && !gate.transport_bound && !gate.output_allowed);
    for (unsigned i = 0U; i < GDS_PROPERTY_COUNT; i++) {
        const BoardCanFrame *read = TestCanTx_Last(BOARD_CAN_SHOULDER);
        assert(read->id == 0x024U && read->dlc == 8U && read->data[0] == 0U
            && read->data[3] == 0U && read->data[4] == 0U && read->data[5] == 0U
            && read->data[6] == 0U && read->data[7] == 0U);
        assert(((uint16_t)read->data[1] | (uint16_t)((uint16_t)read->data[2] << 8)) == endpoints[i]);
        TestCanTx_Complete(BOARD_CAN_SHOULDER);
        BoardCanFrame f = response((GdsPropertyField)i, begin + i + 1U);
        inject(&f, begin + i + 1U);
        ShoulderQualificationStatus s = snapshot();
        assert(s.completed == i + 1U && s.sample[i].requested && s.sample[i].replied
            && s.sample[i].decoded && s.sample[i].endpoint == endpoints[i]);
        assert(s.sample[i].reply_ms == begin + i + 1U);
        assert(memcmp(s.sample[i].reply.data, f.data, sizeof(f.data)) == 0);
    }
    ShoulderQualificationStatus s = snapshot();
    assert(s.state == SHOULDER_QUAL_ACTIVE && !s.pending && s.attempts == 7U
        && s.completed_mask == 0x7FU && s.first_idle && !s.last_idle);
    assert(s.sample[0].value.real == 3.25f && s.sample[1].value.real == -0.125f
        && s.sample[4].value.real == 0.0f && s.sample[2].value.byte == 1U);
    heartbeat(begin + 7U); /* Same timestamp as final SDO cannot certify the end. */
    assert(snapshot().state == SHOULDER_QUAL_ACTIVE && TestCanTx_Sent(BOARD_CAN_SHOULDER) == 7U);
    heartbeat(begin + 8U); s = snapshot();
    assert(s.state == SHOULDER_QUAL_COMPLETE && s.reason == SHOULDER_QUAL_NONE
        && s.first_idle && s.last_idle && s.cancel_accepted && s.finished_ms == begin + 8U);
    assert(s.last_heartbeat.heartbeat_ms - s.sample[6].reply_ms == 1U);
    assert(!ShoulderRead_ClaimStream() && !ShoulderRead_ClaimS3());
    assert(!ShoulderRead_Start(GDS_READ_POSITION, begin + 8U));
    no_more_tx(7U, begin + 50U);
}
static void faults(void)
{
    start(100U); TestCanTx_Complete(BOARD_CAN_SHOULDER);
    BoardCanFrame f = response(GDS_PROPERTY_VELOCITY, 101U); inject(&f, 101U);
    assert(snapshot().state == SHOULDER_QUAL_FAILED && snapshot().reason == SHOULDER_QUAL_REPLY
        && snapshot().sample[0].replied && !snapshot().sample[0].decoded && snapshot().has_unexpected);
    no_more_tx(1U, 102U);

    start(200U); TestCanTx_Complete(BOARD_CAN_SHOULDER);
    f = response(GDS_PROPERTY_POSITION, 201U); TestClock_Set(201U); queue(&f); queue(&f);
    TestCan_Interrupt(BOARD_CAN_SHOULDER); CanMonitor_Poll();
    assert(snapshot().state == SHOULDER_QUAL_FAILED && snapshot().completed == 1U
        && snapshot().reason == SHOULDER_QUAL_REPLY);
    no_more_tx(1U, 202U); /* Duplicate in one batch cannot become request two. */

    start(300U); TestClock_Set(301U);
    f = heartbeat_frame(8U, 0U); queue(&f); f = heartbeat_frame(1U, 0U); queue(&f);
    TestCan_Interrupt(BOARD_CAN_SHOULDER); CanMonitor_Poll();
    assert(snapshot().state == SHOULDER_QUAL_FAILED && snapshot().reason == SHOULDER_QUAL_HEALTH);
    GdsFeedback hb; bool wire; assert(CanMonitor_GetShoulderObservation(&hb, &wire) && hb.axis_state == 1U);
    no_more_tx(1U, 302U); /* Restored IDLE in same batch must not hide state change. */

    start(400U); f = heartbeat_frame(1U, 0U); f.dlc = 7U; inject(&f, 401U);
    assert(snapshot().state == SHOULDER_QUAL_FAILED && snapshot().reason == SHOULDER_QUAL_HEALTH);

    start(500U); heartbeat(590U); heartbeat(680U);
    TestClock_Set(750U); ShoulderQualification_Poll(750U);
    assert(snapshot().state == SHOULDER_QUAL_FAILED && snapshot().reason == SHOULDER_QUAL_TIMEOUT);
    no_more_tx(1U, 751U);

    start(1000U); TestClock_Set(3000U); ShoulderQualification_Poll(3000U);
    assert(snapshot().state == SHOULDER_QUAL_FAILED && snapshot().reason == SHOULDER_QUAL_WINDOW);

    boot(4000U); heartbeat(4000U); TestCanTx_FailNext(BOARD_CAN_SHOULDER);
    assert(!ShoulderQualification_Start(4000U));
    assert(snapshot().claimed && snapshot().attempts == 1U && !snapshot().sample[0].requested
        && snapshot().state == SHOULDER_QUAL_FAILED && snapshot().reason == SHOULDER_QUAL_TX);
    no_more_tx(0U, 4001U);

    start(5000U); TestCanTx_Fail(BOARD_CAN_SHOULDER, false, true);
    assert(!ShoulderQualification_Cancel(5001U));
    assert(snapshot().state == SHOULDER_QUAL_FAILED && snapshot().reason == SHOULDER_QUAL_CANCEL_TX
        && !snapshot().cancel_accepted);
    no_more_tx(1U, 5002U);
}
static void ownership(void)
{
    boot(100U); assert(!ShoulderQualification_Start(100U)); /* No fresh IDLE. */
    heartbeat(100U); TestCanTx_Busy(BOARD_CAN_SHOULDER, true);
    assert(!ShoulderQualification_Start(100U) && !snapshot().claimed);
    assert(TestCanTx_Sent(BOARD_CAN_SHOULDER) == 0U && TestCanTx_Aborts(BOARD_CAN_SHOULDER) == 0U);
    TestCanTx_Complete(BOARD_CAN_SHOULDER);
    BoardCanFrame f = {0}; f.id = 0x029U; f.dlc = 8U;
    TestClock_Set(100U); queue(&f); TestCan_Interrupt(BOARD_CAN_SHOULDER);
    assert(!ShoulderQualification_Start(100U) && !snapshot().claimed); CanMonitor_Poll();
    SafetyConfig c = TestS3_ConfirmedConfig(); TestS3Site_Configure(&c); S3Bench_Init();
    assert(!ShoulderQualification_Start(100U) && snapshot().reason == SHOULDER_QUAL_GATE);

    boot(200U); heartbeat(200U); assert(ShoulderRead_ClaimStream());
    assert(!ShoulderQualification_Start(200U) && snapshot().reason == SHOULDER_QUAL_OWNED);
    boot(300U); heartbeat(300U); assert(ShoulderRead_ClaimS3());
    assert(!ShoulderQualification_Start(300U) && snapshot().reason == SHOULDER_QUAL_OWNED);
    boot(400U); heartbeat(400U); assert(ShoulderRead_Start(GDS_READ_POSITION, 400U));
    TestCanTx_Complete(BOARD_CAN_SHOULDER);
    assert(!ShoulderQualification_Start(400U) && snapshot().reason == SHOULDER_QUAL_OWNED);

    start(500U); assert(ShoulderQualification_Cancel(501U));
    assert(snapshot().state == SHOULDER_QUAL_CANCELLED && snapshot().cancel_accepted);
    assert(!ShoulderRead_ClaimStream() && !ShoulderRead_ClaimS3() && !EncoderStream_Start(501U));
    assert(!ShoulderRead_Start(GDS_READ_VELOCITY, 501U)); no_more_tx(1U, 502U);

    start(600U); TestS3Site_Configure(&c); S3Bench_Init(); ShoulderQualification_Poll(601U);
    assert(snapshot().state == SHOULDER_QUAL_FAILED && snapshot().reason == SHOULDER_QUAL_GATE);
    no_more_tx(1U, 602U);
}
static void can_fault(void)
{
    start(100U); hcan1.ErrorCode = HAL_CAN_ERROR_ACK; HAL_CAN_ErrorCallback(&hcan1);
    hcan1.ErrorCode = 0U; heartbeat(101U);
    assert(snapshot().state == SHOULDER_QUAL_FAILED && snapshot().reason == SHOULDER_QUAL_HEALTH);
    no_more_tx(1U, 102U); /* Current recovery cannot erase cumulative error evidence. */
}
static void uart_fault(void)
{
    start(100U); HAL_UART_ErrorCallback(&huart2); ShoulderQualification_Poll(101U);
    assert(BoardConsole_HasFault() && snapshot().state == SHOULDER_QUAL_FAILED
        && snapshot().reason == SHOULDER_QUAL_HEALTH); no_more_tx(1U, 102U);
}
void TestShoulderQualification_Run(const char *name)
{
    if (!strcmp(name, "shoulder_qual_success")) { success(); }
    else if (!strcmp(name, "shoulder_qual_faults")) { faults(); }
    else if (!strcmp(name, "shoulder_qual_ownership")) { ownership(); }
    else if (!strcmp(name, "shoulder_qual_can_fault")) { can_fault(); }
    else if (!strcmp(name, "shoulder_qual_uart_fault")) { uart_fault(); }
    else { assert(false); }
}
