/* Behavioral tests execute the real firmware modules and pinned libraries.
 * Each scenario runs in a fresh process (reset-only transport state).
 */
#include "main.h"
#include "board_console.h"
#include "board_runtime.h"
#include "console_commands.h"
#include "app_main.h"
#include "s1_config.h"
#include "can_mock.h"
#include "can_monitor.h"
#include "test_c620.h"
#include "test_sdo.h"
#include "test_encoder_stream.h"
#include "encoder_stream.h"
#include "shoulder_read_diagnostic.h"
#include "test_s3.h"
#include "test_shoulder_properties.h"
#include "test_stop_input.h"
#include "test_shoulder_chain.h"
#include "test_shoulder_standstill.h"
#include "test_gateway_deadlines.h"
#include "test_shoulder_qualification.h"
#include "test_console_probe.h"
#include "s3_bench.h"
#include "s3_site_mock.h"
#include "can_tx_mock.h"
#include "s2_config.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static DMA_HandleTypeDef rx_handle = {S1_RX_DMA_SIZE};
UART_HandleTypeDef huart2 = {&rx_handle};
static uint8_t *dma_memory;
static size_t dma_pos;
static uint32_t ticks, primask, group = NVIC_PRIORITYGROUP_4;
static uint32_t priorities[] = {0, 15, 5, 5, 5}; /* BSP has overwritten EXTI. */
static unsigned leds, aborts, tx_calls;
static HAL_StatusTypeDef rx_result = HAL_OK, tx_result = HAL_OK;
static const uint8_t *inflight;
static size_t inflight_length;
static char wire[32768];
static size_t wire_length;
static bool complete_on_lock;
static void complete(void);

uint32_t __get_PRIMASK(void)
{
    /* Inject TC at the old check/timeout window, before interrupts are masked. */
    if (complete_on_lock) { complete_on_lock = false; complete(); }
    return primask;
}
void __disable_irq(void) { primask = 1U; }
void __set_PRIMASK(uint32_t p) { primask = p; }
uint32_t HAL_GetTick(void) { return ticks; }
void TestClock_Set(uint32_t value) { ticks = value; }
uint32_t HAL_NVIC_GetPriorityGrouping(void) { return group; }
void HAL_NVIC_SetPriority(IRQn_Type irq, uint32_t p, uint32_t s)
{ assert(s == 0U); priorities[irq] = p; }
void HAL_NVIC_GetPriority(IRQn_Type irq, uint32_t g, uint32_t *p, uint32_t *s)
{ assert(g == NVIC_PRIORITYGROUP_4); *p = priorities[irq]; *s = 0U; }
void BSP_LED_Toggle(unsigned led) { assert(led == LED2); leds++; }
HAL_StatusTypeDef HAL_UARTEx_ReceiveToIdle_DMA(UART_HandleTypeDef *h, uint8_t *p, uint16_t n)
{ assert(h == &huart2 && n == S1_RX_DMA_SIZE); dma_memory = p; return rx_result; }
HAL_StatusTypeDef HAL_UART_Transmit_DMA(UART_HandleTypeDef *h, const uint8_t *p, uint16_t n)
{
    assert(h == &huart2 && n <= S1_TX_CHUNK_SIZE);
    tx_calls++;
    if (tx_result == HAL_OK) { assert(inflight == NULL); inflight = p; inflight_length = n; }
    return tx_result;
}
HAL_StatusTypeDef HAL_UART_Abort_IT(UART_HandleTypeDef *h)
{ assert(h == &huart2); aborts++; inflight = NULL; return HAL_OK; }

static void complete(void)
{
    if (inflight == NULL) { return; }
    assert(wire_length + inflight_length < sizeof(wire));
    memcpy(wire + wire_length, inflight, inflight_length);
    wire_length += inflight_length;
    wire[wire_length] = '\0';
    inflight = NULL;
    HAL_UART_TxCpltCallback(&huart2);
}

static void feed(const void *data, size_t n)
{
    const uint8_t *p = data;
    assert(dma_memory != NULL);
    for (size_t i = 0; i < n; i++) {
        dma_memory[dma_pos++] = p[i];
        rx_handle.remaining = S1_RX_DMA_SIZE - (uint32_t)dma_pos;
        if (dma_pos == S1_RX_DMA_SIZE / 2U || dma_pos == S1_RX_DMA_SIZE) {
            HAL_UARTEx_RxEventCallback(&huart2, (uint16_t)dma_pos);
        }
        if (dma_pos == S1_RX_DMA_SIZE) { dma_pos = 0; rx_handle.remaining = S1_RX_DMA_SIZE; }
    }
    HAL_UARTEx_RxEventCallback(&huart2, (uint16_t)dma_pos); /* IDLE */
}

static void service(unsigned count)
{
    for (unsigned i = 0; i < count; i++) {
        ticks++;
        BoardConsole_Poll(ticks);
        ConsoleCommands_Poll(ticks, 0);
        complete();
    }
}

static void query(const char *input, const char *expected)
{
    size_t before = wire_length;
    feed(input, strlen(input));
    service(5);
    assert(strcmp(wire + before, expected) == 0);
}

static void test_commands(void)
{
    assert(BoardConsole_Start()); ConsoleCommands_Init();
    feed("pi", 2); service(2); assert(wire_length == 0);
    feed("ng\r\n", 4); service(4); assert(strcmp(wire, "OK pong\r\n") == 0);
    query("ping\nping\n", "OK pong\r\nOK pong\r\n");
    query("\n \r\n", "");
    query("? extra\n", "ERR args\r\n");
    query("ping \"\"\n", "ERR args\r\n");
    query("\"ping\"\n", "ERR syntax\r\n");
    query("p\\ing\n", "ERR syntax\r\n");
    query("PING\n", "ERR unknown\r\n");
    query("jointlock\n", "ERR unknown\r\n");
    query("ping\rx\n", "ERR syntax\r\n");
    query("ping\t\n", "ERR syntax\r\n");
    query("help\n?\n", "OK commands=ping,status,help,?,canstatus,shoulder,elbow,elbowfeedback,shoulderfeedback,shoulderreadpos,shoulderreadvel,shoulderreadstatus,encstart,encstop,encfeedback,outputstatus,stopstatus,prepare,arm,cancel,ackfault,shoulderpulse,elbowpulse,s3help\r\n"
          "OK commands=ping,status,help,?,canstatus,shoulder,elbow,elbowfeedback,shoulderfeedback,shoulderreadpos,shoulderreadvel,shoulderreadstatus,encstart,encstop,encfeedback,outputstatus,stopstatus,prepare,arm,cancel,ackfault,shoulderpulse,elbowpulse,s3help\r\n");
    char boundary[100]; memset(boundary, ' ', 96); memcpy(boundary, "ping", 4);
    boundary[96] = '\n'; boundary[97] = 0;
    query(boundary, "OK pong\r\n");
    boundary[96] = ' '; boundary[97] = '\n'; boundary[98] = 0;
    query(boundary, "ERR line_too_long\r\n");
    query("ping\n", "OK pong\r\n");
    const uint8_t bad[] = {'p', 0, 'i', 'n', 'g', '\n'};
    size_t before = wire_length; feed(bad, sizeof(bad)); service(3);
    assert(strcmp(wire + before, "ERR syntax\r\n") == 0);
    before = wire_length; feed("status\n", 7); service(3);
    assert(strstr(wire + before, "stage=S2_CAN_RX") && strstr(wire + before, "output_allowed=0"));
    assert(strstr(wire + before, "uart_errors=0"));
    S3Bench_Init();
    query("outputstatus\n", "OK stage=S2_CAN_RX s3_state=0 reason=1 configured=0 transport_bound=0 "
          "output_allowed=0 request_valid=0 stop_attempted=0 prep_state=0 arm_token=0 feedback_pending=0 reader=0 reader_reason=0\r\n");
}

static void test_rx_positions(void)
{
    assert(BoardConsole_Start());
    uint8_t input[700], out[700];
    for (size_t i = 0; i < sizeof(input); i++) { input[i] = (uint8_t)i; }
    feed(input, 257);
    HAL_UARTEx_RxEventCallback(&huart2, 256); /* delayed, stale HT payload */
    assert(BoardConsole_Read(out, sizeof(out)) == 257);
    assert(memcmp(input, out, 257) == 0);
    feed(input + 257, 255); /* TC and duplicate IDLE at reload */
    HAL_UARTEx_RxEventCallback(&huart2, 512);
    assert(BoardConsole_Read(out, sizeof(out)) == 255);
    assert(memcmp(input + 257, out, 255) == 0);
    feed(input, sizeof(input));
    assert(BoardConsole_Read(out, sizeof(out)) == sizeof(input));
    assert(memcmp(input, out, sizeof(input)) == 0);
    assert(BoardConsole_Read(out, 1) == 0);
}

static void test_tx_queue(void)
{
    assert(BoardConsole_Start());
    uint8_t data[2047]; for (size_t i = 0; i < sizeof(data); i++) { data[i] = (uint8_t)i; }
    assert(BoardConsole_TxFree() == sizeof(data));
    assert(BoardConsole_Write(data, sizeof(data)));
    assert(!BoardConsole_Write((const uint8_t *)"x", 1));
    BoardConsole_Poll(1); assert(tx_calls == 1 && inflight_length == 256);
    BoardConsole_Poll(2); assert(tx_calls == 1); /* DMA/USART completion not signalled */
    assert(memcmp(inflight, data, 256) == 0);
    while (wire_length < sizeof(data)) { complete(); BoardConsole_Poll(3); }
    assert(memcmp(wire, data, sizeof(data)) == 0);
    assert(BoardConsole_TxFree() == sizeof(data));
    assert(BoardConsole_Write(data, sizeof(data))); /* wraps software ring */
    while (wire_length < 2 * sizeof(data)) { BoardConsole_Poll(4); complete(); }
    BoardConsole_Poll(4);
    assert(memcmp(wire + sizeof(data), data, sizeof(data)) == 0);
    primask = 1; BoardConsoleStats s; BoardConsole_GetStats(&s);
    assert(primask == 1 && s.tx_bytes == 2 * sizeof(data));
}

static void test_backpressure(void)
{
    assert(BoardConsole_Start()); ConsoleCommands_Init();
    uint8_t padding[1800]; memset(padding, 'x', sizeof(padding));
    assert(BoardConsole_Write(padding, sizeof(padding)));
    feed("ping\n", 5); ConsoleCommands_Poll(1, 0);
    assert(BoardConsole_TxFree() == 247); /* no partial reply, command retained */
    service(20);
    assert(wire_length == sizeof(padding) + strlen("OK pong\r\n"));
    assert(strcmp(wire + sizeof(padding), "OK pong\r\n") == 0);
}

static void test_fault(const char *kind)
{
    if (strcmp(kind, "start_error") == 0) { rx_result = HAL_ERROR; }
    bool ok = BoardConsole_Start();
    if (strcmp(kind, "start_error") == 0) { assert(!ok); }
    else { assert(ok); }
    if (strcmp(kind, "rx_overflow") == 0) {
        uint8_t bytes[1024] = {0}; feed(bytes, sizeof(bytes));
    } else if (strcmp(kind, "uart_error") == 0) {
        HAL_UART_ErrorCallback(&huart2);
    } else if (strcmp(kind, "tx_error") == 0) {
        tx_result = HAL_BUSY; assert(BoardConsole_Write((const uint8_t *)"x", 1));
        BoardConsole_Poll(1);
    } else if (strcmp(kind, "timeout") == 0) {
        assert(BoardConsole_Write((const uint8_t *)"x", 1));
        BoardConsole_Poll(UINT32_MAX - 50U);
        BoardConsole_Poll(48); assert(!BoardConsole_HasFault());
        BoardConsole_Poll(49);
    }
    assert(BoardConsole_HasFault());
    uint8_t out; assert(BoardConsole_Read(&out, 1) == 0);
    assert(!BoardConsole_Write((const uint8_t *)"x", 1));
    assert(!BoardConsole_Start());
    BoardConsole_Poll(500); BoardConsole_Poll(501); assert(aborts == 1);
}

static void test_late_poll(void)
{
    assert(BoardConsole_Start()); assert(BoardConsole_Write((const uint8_t *)"x", 1));
    BoardConsole_Poll(0); complete(); BoardConsole_Poll(150);
    assert(!BoardConsole_HasFault());
}

static void test_timeout_race(void)
{
    assert(BoardConsole_Start()); assert(BoardConsole_Write((const uint8_t *)"x", 1));
    BoardConsole_Poll(0);
    complete_on_lock = true;
    BoardConsole_Poll(100);
    assert(!BoardConsole_HasFault());
    BoardConsole_Poll(101);
    assert(wire_length == 1 && BoardConsole_TxFree() == 2047);
}

static void test_scheduler(void)
{
    ticks = UINT32_MAX - 2U; App_Init();
    feed("ping\nping\n", 10);
    App_Poll(); assert(tx_calls == 0);
    ticks++; App_Poll(); /* one command only */
    ticks++; App_Poll(); complete();
    assert(strcmp(wire, "OK pong\r\n") == 0);
    ticks = 0; App_Poll(); complete();
    assert(strcmp(wire, "OK pong\r\nOK pong\r\n") == 0);
    ticks = 500; App_Poll(); assert(leds == 1);
    feed("status\n", 7); ticks++; App_Poll(); ticks++; App_Poll(); complete();
    assert(strstr(wire, "scheduler_overruns=1"));
    HAL_UART_ErrorCallback(&huart2); ticks = 599; App_Poll(); assert(leds == 1);
    ticks = 600; App_Poll(); assert(leds == 2);
}

static void test_priorities(void)
{
    assert(priorities[EXTI15_10_IRQn] == 15);
    assert(BoardRuntime_FinalizeInterrupts()); assert(priorities[EXTI15_10_IRQn] == 10);
    group = 0; assert(!BoardRuntime_FinalizeInterrupts()); group = 4;
    priorities[DMA1_Stream5_IRQn] = 6; assert(!BoardRuntime_FinalizeInterrupts());
}

static void test_can_commands(bool fail_start)
{
    if (fail_start) { TestCan_Fail(BOARD_CAN_SHOULDER, "start"); }
    App_Init();
    query("ping\n", "OK pong\r\n");
    size_t before = wire_length;
    feed("canstatus\n", 10); service(5);
    assert(strstr(wire + before, fail_start ? "can1_started=0" : "can1_started=1"));
    assert(strstr(wire + before, "can2_started=1"));
    assert(strstr(wire + before, "can1_baud=500000") && strstr(wire + before, "can2_baud=1000000"));
    before = wire_length; feed("shoulder\n", 9); service(5);
    assert(strstr(wire + before, "seen=0") && strstr(wire + before, "age_ms=na"));
    ticks = UINT32_MAX - 1U;
    TestCan_Inject(BOARD_CAN_ELBOW, 0x201, 8, 0, 0, 0xAB);
    TestCan_Interrupt(BOARD_CAN_ELBOW); CanMonitor_Poll();
    ticks = 0;
    before = wire_length; feed("elbow\n", 6); service(1);
    service(1); /* Complete the queued DMA transmission. */
    assert(strstr(wire + before, "seen=1 id=201 dlc=8 data=ABABABABABABABAB age_ms=3"));
    assert(wire_length - before < S1_REPLY_SIZE);
    /* ISR records a new frame after App_Poll sampled its scheduling time. */
    ticks = 51U;
    TestCan_Inject(BOARD_CAN_ELBOW, 0x202, 8, 0, 0, 0xCD);
    TestCan_Interrupt(BOARD_CAN_ELBOW); CanMonitor_Poll();
    before = wire_length; feed("elbow\n", 6);
    ConsoleCommands_Poll(50U, 0U); service(2);
    assert(strstr(wire + before, "id=202 dlc=8 data=CDCDCDCDCDCDCDCD age_ms=0\r\n"));
    query("elbow extra\n", "ERR args\r\n");
    before = wire_length; feed("status\n", 7); service(5);
    assert(strstr(wire + before, "output_allowed=0") && strstr(wire + before, "stage=S2_CAN_RX"));
}

static void test_shoulder_console(void)
{
    App_Init(); size_t before = wire_length;
    feed("shoulderfeedback\n", 17U); service(5U);
    assert(strstr(wire + before, "verified=0 est=0 hb=0 wire_valid=0 pos_mrad=na"));
    assert(strstr(wire + before, "output_allowed=0") && wire_length - before < S1_REPLY_SIZE);
    const uint8_t estimate[8] = {0,0,0xA0,0x3F,0,0,0,0xBF};
    const uint8_t heartbeat[8] = {0,0,0,0,1,0,0,1}; ticks = 100U;
    TestCan_InjectData(BOARD_CAN_SHOULDER, 0x29U, 8U, 0U, 0U, estimate);
    TestCan_InjectData(BOARD_CAN_SHOULDER, 0x21U, 8U, 0U, 0U, heartbeat);
    TestCan_Interrupt(BOARD_CAN_SHOULDER); CanMonitor_Poll();
    before = wire_length; feed("shoulderfeedback\n", 17U); service(5U);
    assert(strstr(wire + before, "verified=0 est=1 hb=1 wire_valid=1 pos_mrad=7853 vel_mrad_s=-3141"));
    assert(strstr(wire + before, "est_ms=100 hb_ms=100 state=1 error=00000000 fault=0 output_allowed=0"));
    assert(wire_length - before < S1_REPLY_SIZE);
    ticks = 201U; before = wire_length; feed("shoulderfeedback\n", 17U); service(5U);
    assert(strstr(wire + before, "wire_valid=0") && strstr(wire + before, "est_ms=100 hb_ms=100"));
}
static void test_c620_console(bool fail_start)
{
    if (fail_start) { TestCan_Fail(BOARD_CAN_ELBOW, "start"); }
    App_Init();
    size_t before = wire_length;
    feed("elbowfeedback\n", 14); service(5);
    assert(strstr(wire + before, "seen=0 valid=0 online=0 rotor_pos_rad=na"));
    assert(strstr(wire + before, "received_ms=na age_ms=na output_allowed=0\r\n"));
    query("elbowfeedback extra\n", "ERR args\r\n");
    if (fail_start) {
        before = wire_length; feed("canstatus\n", 10); service(5);
        assert(strstr(wire + before, "can2_started=0") && strstr(wire + before, "output_allowed=0"));
        return;
    }
    const uint8_t captured[8] = {0x0C,0x13,0xFF,0xC4,0xFF,0xFC,0x1E,0};
    ticks = 1000;
    TestCan_InjectData(BOARD_CAN_ELBOW, 0x201, 8, 0, 0, captured);
    TestCan_Interrupt(BOARD_CAN_ELBOW); CanMonitor_Poll();
    before = wire_length; feed("elbowfeedback\n", 14); service(5);
    assert(strstr(wire + before, "seen=1 valid=1 online=1"));
    const char *position = strstr(wire + before, "rotor_pos_rad=");
    assert(position != NULL);
    float radians = strtof(position + strlen("rotor_pos_rad="), NULL);
    assert(radians > 2.370765f && radians < 2.370769f);
    assert(strstr(wire + before, "rotor_vel_rad_s=-6.283 angle_ticks=3091 rpm=-60 current_raw=-4 temp_c=30"));
    assert(strstr(wire + before, "received_ms=1000 age_ms=1 output_allowed=0\r\n"));
    assert(wire_length - before < S1_REPLY_SIZE);
    ticks = 1040;
    TestCan_InjectData(BOARD_CAN_ELBOW, 0x201, 7, 0, 0, captured);
    TestCan_Interrupt(BOARD_CAN_ELBOW); CanMonitor_Poll();
    before = wire_length; feed("elbowfeedback\n", 14); service(5);
    assert(strstr(wire + before, "valid=1 online=1"));
    assert(strstr(wire + before, "received_ms=1000 age_ms=41"));
    ticks = 1101;
    before = wire_length; feed("elbowfeedback\n", 14); service(5);
    assert(strstr(wire + before, "seen=1 valid=0 online=0"));
    assert(strstr(wire + before, "received_ms=1000 age_ms=102"));
    /* New fresh valid frame recovers without clearing raw diagnostics. */
    hcan2.ErrorCode = HAL_CAN_ERROR_ACK; HAL_CAN_ErrorCallback(&hcan2);
    ticks = 1120;
    const uint8_t recovered[8] = {0x10,0,0,0x3C,0,1,0x1F,0};
    TestCan_InjectData(BOARD_CAN_ELBOW, 0x201, 8, 0, 0, recovered);
    TestCan_Interrupt(BOARD_CAN_ELBOW); CanMonitor_Poll();
    before = wire_length; feed("elbowfeedback\n", 14); service(5);
    assert(strstr(wire + before, "valid=1 online=1") && strstr(wire + before, "rotor_vel_rad_s=6.283"));
    assert(strstr(wire + before, "received_ms=1120 age_ms=1"));
    before = wire_length; feed("elbow\n", 6); service(5);
    assert(strstr(wire + before, "err=1") && strstr(wire + before, "seen=1 id=201 dlc=8"));
    /* Extreme representable values and 10-digit clocks must fit one reply. */
    const uint8_t extreme[8] = {0x1F,0xFF,0x80,0,0x80,0,0xFF,0};
    ticks = UINT32_MAX - 10U;
    TestCan_InjectData(BOARD_CAN_ELBOW, 0x201, 8, 0, 0, extreme);
    TestCan_Interrupt(BOARD_CAN_ELBOW); CanMonitor_Poll();
    before = wire_length; feed("elbowfeedback\n", 14); service(5);
    assert(strstr(wire + before, "rpm=-32768 current_raw=-32768 temp_c=255"));
    assert(!strstr(wire + before, "ERR format") && wire_length - before < S1_REPLY_SIZE);
    ticks = UINT32_MAX - 20U;
    before = wire_length; feed("elbowfeedback\n", 14); service(5);
    assert(strstr(wire + before, "valid=0 online=0"));
    assert(!strstr(wire + before, "ERR format") && wire_length - before < S1_REPLY_SIZE);
}


static S3SiteSample bench_sample;
static void bench_service(unsigned count)
{
    for (unsigned i = 0U; i < count; i++) {
        ticks++;
        for (unsigned j = 0U; j < 2U; j++) { bench_sample.joint[j].received_ms = ticks; }
        bench_sample.driver[0].received_ms = ticks;
        bench_sample.driver[1].received_ms = ticks;
        TestS3Site_SetSample(&bench_sample, true);
        App_Poll(); complete();
    }
}
static void bench_query(const char *command, const char *expected)
{
    size_t before = wire_length; feed(command, strlen(command)); bench_service(5U);
    if (strcmp(wire + before, expected) != 0) {
        fprintf(stderr, "CLI command: %sExpected: %sActual: %s", command, expected, wire + before);
        S3BenchStatus state; S3Bench_GetStatus(&state);
        fprintf(stderr, "Gate state=%u reason=%u configured=%u bound=%u phase=%u\nPrior replies: %s\n",
            state.state, state.reason, state.configured, state.transport_bound, state.preparation, wire);
        for (unsigned bus = 0U; bus < 2U; bus++) {
            BoardCanStats stats; (void)BoardCan_GetStats((BoardCanBus)bus, &stats);
            fprintf(stderr, "CAN%u started=%u err=%lu invalid=%lu dropped=%lu start_error=%u\n",
                bus, stats.started, (unsigned long)stats.errors, (unsigned long)stats.invalid,
                (unsigned long)stats.dropped, stats.start_error);
        }
    }
    assert(strcmp(wire + before, expected) == 0);
}
static void bench_prepare(void)
{
    bench_sample.driver[0].axis_state = 1U;
    bench_sample.driver[0].zero_verified = bench_sample.driver[0].mode_verified = false;
    bench_sample.driver[1].zero_verified = false;
    bench_query("prepare\n", "OK preparing output_allowed=0\r\n");
    S3BenchStatus s; S3Bench_GetStatus(&s);
    assert(s.state == OUTPUT_PREPARING && !s.output_allowed);
    bench_sample.driver[0].zero_verified = true;
    bench_sample.driver[1].zero_verified = true; bench_sample.driver[1].zero_verified_ms = ticks + 1U;
    bench_sample.driver[0].zero_verified_ms = ticks + 1U; bench_service(1U);
    assert(TestCanTx_Last(BOARD_CAN_SHOULDER)->id == 0x2BU);
    bench_sample.driver[0].mode_verified = true;
    bench_sample.driver[0].mode_verified_ms = ticks + 1U; bench_service(1U);
    assert(TestCanTx_Last(BOARD_CAN_SHOULDER)->id == 0x27U);
    assert(TestCanTx_Last(BOARD_CAN_SHOULDER)->data[0] == 8U);
    bench_sample.driver[0].axis_state = 8U; bench_service(1U);
    S3Bench_GetStatus(&s); assert(s.state == OUTPUT_READY && !s.output_allowed && s.arm_token == 0U);
}
static void test_s3_cli(void)
{
    /* Real App_Poll -> byte console -> CLI -> gateway -> L1 -> host HAL. */
    TestS3Site_Reset(); App_Init(); TestCanTx_Reset(); /* HAL READY before real CAN startup. */
    bench_query("prepare\n", "ERR blocked\r\n");
    bench_query("arm\n", "ERR blocked\r\n");
    bench_query("elbowpulse 1 100 20\n", "ERR blocked\r\n");
    bench_query("ackfault\n", "ERR blocked\r\n");
    assert(TestCanTx_Sent(BOARD_CAN_SHOULDER) == 0U && TestCanTx_Sent(BOARD_CAN_ELBOW) == 0U);
    SafetyConfig config = TestS3_ConfirmedConfig();
    TestS3Site_Configure(&config); TestCanTx_Reset(); S3Bench_Init();
    memset(&bench_sample, 0, sizeof(bench_sample));
    for (unsigned j = 0U; j < 2U; j++) { bench_sample.joint[j].valid = true; }
    bench_sample.operator_stop_clear = bench_sample.independently_stopped = true; bench_sample.driver[0].valid = true;
    bench_sample.driver[0].calibration_verified = true; bench_sample.driver[0].node = 1U;
    bench_sample.driver[1].valid = true; bench_sample.driver[1].node = 1U;
    bench_sample.driver[0].axis_state = 1U; bench_service(1U);
    bench_query("arm\n", "ERR blocked\r\n"); /* No prepare bypass. */
    bench_prepare(); bench_query("arm\n", "OK armed token=1\r\n");
    bench_query("elbowpulse 1 -100 40\n", "OK request\r\n");
    S3BenchStatus s; S3Bench_GetStatus(&s); assert(s.state == OUTPUT_ACTIVE && s.output_allowed);
    assert(TestCanTx_Last(BOARD_CAN_ELBOW)->data[0] == 0U); /* Joint direction -1 -> positive A. */
    size_t before = wire_length; feed("status\n", 7U); bench_service(5U);
    assert(strstr(wire + before, "output_allowed=1"));
    bench_query("cancel\n", "OK cancelled output_allowed=0\r\n");
    S3Bench_GetStatus(&s); assert(s.state == OUTPUT_SAFE_OFF && s.arm_token == 0U && !s.request_valid);
    bench_query("elbowpulse 1 100 40\n", "ERR blocked\r\n");
    bench_prepare(); bench_query("arm\n", "OK armed token=2\r\n");
    bench_query("elbowpulse 1 100 40\n", "ERR blocked\r\n"); /* Earlier permit is stale. */
    bench_query("shoulderpulse 2 100 40\n", "OK request\r\n");
    bench_query("shoulderpulse 2 nan 40\n", "ERR args\r\n");
    S3Bench_GetStatus(&s); assert(s.state == OUTPUT_FAULT && !s.output_allowed && s.arm_token == 0U);
    bench_query("cancel\n", "OK cancelled output_allowed=0\r\n");
    S3Bench_GetStatus(&s); assert(s.state == OUTPUT_FAULT); /* Cancel cannot clear fault. */
    bench_query("ackfault\n", "ERR blocked\r\n"); /* CLI cannot assert independent stop. */
    bench_sample.independently_stopped = true; bench_sample.driver[0].axis_state = 1U;
    bench_query("ackfault\n", "OK fault_acknowledged output_allowed=0\r\n");
    bench_query("arm\n", "ERR blocked\r\n");
    bench_prepare(); bench_query("arm\n", "OK armed token=3\r\n");
    bench_query("elbowpulse 3 100 10\n", "OK request\r\n"); bench_service(10U);
    S3Bench_GetStatus(&s); assert(s.state == OUTPUT_FAULT && s.reason == OUTPUT_REASON_EXPIRED);
    unsigned sent = TestCanTx_Sent(BOARD_CAN_ELBOW); bench_service(5U);
    assert(TestCanTx_Sent(BOARD_CAN_ELBOW) == sent); /* No retry or stale replay. */
    bench_sample.driver[0].axis_state = 1U;
    bench_query("ackfault\n", "OK fault_acknowledged output_allowed=0\r\n");
    bench_prepare(); bench_query("arm\n", "OK armed token=4\r\n");
    bench_query("elbowpulse 4 100 40\n", "OK request\r\n");
    bench_sample.joint[0].valid = false; bench_service(1U);
    S3Bench_GetStatus(&s); assert(s.state == OUTPUT_FAULT && !s.output_allowed);
    assert(TestCanTx_Last(BOARD_CAN_SHOULDER)->id == 0x27U);
    sent = TestCanTx_Sent(BOARD_CAN_ELBOW);
    TestS3Site_Reset(); S3Bench_Init(); bench_service(2U);
    S3Bench_GetStatus(&s); assert(s.state == OUTPUT_SAFE_OFF && s.arm_token == 0U && !s.request_valid);
    assert(TestCanTx_Sent(BOARD_CAN_ELBOW) == sent); /* Reboot/init does not restore an action. */
}

static void real_bench_service(unsigned count)
{
    const uint8_t feedback[8] = {0x0D,0x6D,0,0,0,0,28,0};
    for (unsigned i = 0; i < count; i++) {
        ticks++;
        TestCan_InjectData(BOARD_CAN_ELBOW, 0x201U, 8U, 0U, 0U, feedback);
        TestCan_Interrupt(BOARD_CAN_ELBOW); App_Poll(); complete();
    }
}
static void real_bench_query(const char *command, const char *expected)
{
    size_t before = wire_length; feed(command, strlen(command)); real_bench_service(5U);
    if (strcmp(wire + before, expected)) { fprintf(stderr, "%sExpected:%sActual:%s",command,expected,wire+before); }
    assert(strcmp(wire + before, expected) == 0);
}
static void test_bench_stop_cli(void)
{
    TestS3Site_Reset(); App_Init(); TestCanTx_Reset();
    SafetyConfig c = TestS3_ConfirmedConfig(); c.enabled_axes = SAFETY_ELBOW_AXES;
    c.stop_policy = SAFETY_STOP_SUPERVISED_C620_BENCH;
    c.joint[1].mapping = (JointMapping){1.0f,0.0f,1,true};
    c.joint[1].lower_rad = 2.3f; c.joint[1].upper_rad = 2.9f;
    c.joint[1].max_motor_command = 0.05f; c.joint[1].motor_slew_per_s = 1.0f;
    TestS3Site_ConfigureBench(&c); S3Bench_Init();
    real_bench_query("prepare\n", "ERR blocked\r\n");
    real_bench_service(251U);
    TestCanTx_FailNext(BOARD_CAN_ELBOW);
    real_bench_query("prepare\n", "ERR blocked\r\n");
    S3BenchStatus failed; S3Bench_GetStatus(&failed);
    assert(failed.state == OUTPUT_FAULT && failed.stop_accepted && !failed.stop_delivery_failed);
    real_bench_query("ackfault\n", "ERR blocked\r\n"); /* Same-call Prepare failure discarded old proof. */
    real_bench_service(251U);
    real_bench_query("ackfault\n", "OK fault_acknowledged output_allowed=0\r\n");
    real_bench_query("prepare\n", "OK preparing output_allowed=0\r\n");
    real_bench_query("arm\n", "ERR blocked\r\n"); /* Pre-zero quiet window discarded. */
    real_bench_service(251U);
    real_bench_query("arm\n", "OK armed token=1\r\n");
    real_bench_query("elbowpulse 1 50 100\n", "OK request\r\n");
    real_bench_query("cancel\n", "OK cancelled output_allowed=0\r\n");
    real_bench_query("prepare\n", "ERR blocked\r\n"); /* Fresh post-stop window required. */
    real_bench_service(251U);
    real_bench_query("prepare\n", "OK preparing output_allowed=0\r\n"); real_bench_service(251U);
    real_bench_query("arm\n", "OK armed token=2\r\n");
    real_bench_query("elbowpulse 2 50 100\n", "OK request\r\n"); real_bench_service(101U);
    S3BenchStatus status; S3Bench_GetStatus(&status);
    assert(status.state == OUTPUT_FAULT && status.stop_accepted && !status.stop_delivery_failed);
    real_bench_query("ackfault\n", "ERR blocked\r\n");
    real_bench_service(251U); real_bench_query("ackfault\n", "OK fault_acknowledged output_allowed=0\r\n");
    size_t before = wire_length; feed("stopstatus\n", 11U); real_bench_service(5U);
    assert(strstr(wire + before, "policy=1 source=c620_feedback seen=1 fresh=1 stationary=1"));
    assert(TestCanTx_Sent(BOARD_CAN_SHOULDER) == 0U);
}

static void test_sdo_cli(void)
{
    TestS3Site_Reset(); App_Init(); TestCanTx_Reset();
    query("shoulderreadpos 372\n", "ERR args\r\n");
    query("shoulderreadvel 1\n", "ERR args\r\n");
    query("shoulderreadstatus extra\n", "ERR args\r\n");
    query("shoulderreadpos\n", "ERR read_blocked\r\n");
    const uint8_t heartbeat[8] = {0,0,0,0,1,0,0,1};
    ticks = 100U;
    TestCan_InjectData(BOARD_CAN_SHOULDER, 0x021U, 8U, 0U, 0U, heartbeat);
    TestCan_Interrupt(BOARD_CAN_SHOULDER); CanMonitor_Poll();
    query("shoulderreadpos\n", "OK read_pending output_allowed=0\r\n");
    assert(TestCanTx_Sent(BOARD_CAN_SHOULDER) == 1U && TestCanTx_Sent(BOARD_CAN_ELBOW) == 0U);
    const uint8_t response[8] = {0,0x74,1,0,0,0,0x40,0x40}; /* 3rev */
    TestCan_InjectData(BOARD_CAN_SHOULDER, 0x025U, 8U, 0U, 0U, response);
    TestCan_Interrupt(BOARD_CAN_SHOULDER); CanMonitor_Poll();
    size_t before = wire_length; feed("shoulderreadstatus\n", 19U); service(5U);
    assert(strstr(wire + before, "src=encoder_sdo field=pos unit=rev phase=received"));
    assert(strstr(wire + before, "ep=372 accepted=1 value=3.000000 fresh=1"));
    assert(strstr(wire + before, "id=025 dlc=8 raw=0074010000004040 rejected=0 output_allowed=0"));
    assert(wire_length - before < S1_REPLY_SIZE);
    query("shoulderreadpos\n", "ERR read_blocked\r\n");
    TestCanTx_Reset(); /* Host seam has no automatic TX completion. */
    query("shoulderreadvel\n", "OK read_pending output_allowed=0\r\n");
    const uint8_t velocity[8] = {0,0x7A,1,0,0,0,0,0xBE}; /* -0.125rev/s */
    TestCan_InjectData(BOARD_CAN_SHOULDER, 0x025U, 8U, 0U, 0U, velocity);
    TestCan_Interrupt(BOARD_CAN_SHOULDER); CanMonitor_Poll();
    before = wire_length; feed("shoulderreadstatus\n", 19U); service(5U);
    assert(strstr(wire + before, "field=vel unit=rev_s phase=received"));
    assert(strstr(wire + before, "ep=378 accepted=1 value=-0.125000 fresh=1"));
    assert(wire_length - before < S1_REPLY_SIZE && !strstr(wire + before, "ERR format"));
    query("shoulderreadvel\n", "ERR read_blocked\r\n");
    S3BenchStatus gate; S3Bench_GetStatus(&gate);
    assert(gate.state == OUTPUT_SAFE_OFF && !gate.configured && !gate.transport_bound && !gate.output_allowed);
    assert(TestCanTx_Sent(BOARD_CAN_SHOULDER) == 1U && TestCanTx_Sent(BOARD_CAN_ELBOW) == 0U);
    ticks += SHOULDER_READ_TIMEOUT_MS;
    before = wire_length; feed("shoulderreadstatus\n", 19U); service(5U);
    assert(strstr(wire + before, "value=-0.125000 fresh=0")); /* Cached is not live feedback. */
}

static void test_stream_cli(void)
{
    TestS3Site_Reset(); App_Init(); TestCanTx_Reset();
    query("encstart extra\n", "ERR args\r\n");
    query("encfeedback extra\n", "ERR args\r\n");
    query("encstart\n", "ERR encoder_stream_blocked\r\n");
    size_t before = wire_length; feed("encfeedback\n", 12U); service(5U);
    assert(strstr(wire + before, "src=encoder_sdo active=0 reason=0 valid=0 pairs=0 pos_rev=na vel_rev_s=na"));
    assert(TestCanTx_Sent(BOARD_CAN_SHOULDER) == 0U);
    const uint8_t heartbeat[8] = {0,0,0,0,1,0,0,1}; ticks = 100U;
    TestCan_InjectData(BOARD_CAN_SHOULDER, 0x021U, 8U, 0U, 0U, heartbeat);
    TestCan_Interrupt(BOARD_CAN_SHOULDER); CanMonitor_Poll();
    query("encstart\n", "OK encoder_stream_started output_allowed=0\r\n");
    assert(TestCanTx_Last(BOARD_CAN_SHOULDER)->id == 0x024U);
    assert(TestCanTx_Last(BOARD_CAN_SHOULDER)->data[1] == 0x74U);
    before = wire_length; feed("shoulderreadstatus\n", 19U); service(5U);
    assert(strstr(wire + before, "phase=stream_owned")); /* Added enum cannot index past text table. */
    query("shoulderreadpos\n", "ERR read_blocked\r\n");
    const uint8_t position[8] = {0,0x74,1,0,0,0,0x40,0x40};
    TestCanTx_Reset(); /* Complete simulated position request before receiving its ACK. */
    TestCan_InjectData(BOARD_CAN_SHOULDER, 0x025U, 8U, 0U, 0U, position);
    TestCan_Interrupt(BOARD_CAN_SHOULDER); CanMonitor_Poll();
    assert(TestCanTx_Last(BOARD_CAN_SHOULDER)->data[1] == 0x7AU);
    before = wire_length; feed("encfeedback\n", 12U); service(5U);
    assert(strstr(wire + before, "active=1 reason=0 valid=0 pairs=0 pos_rev=na"));
    const uint8_t velocity[8] = {0,0x7A,1,0,0,0,0,0xBE};
    TestCanTx_Reset();
    TestCan_InjectData(BOARD_CAN_SHOULDER, 0x025U, 8U, 0U, 0U, velocity);
    TestCan_Interrupt(BOARD_CAN_SHOULDER); CanMonitor_Poll();
    before = wire_length; feed("encfeedback\n", 12U); service(5U);
    assert(strstr(wire + before, "active=1 reason=0 valid=1 pairs=1 pos_rev=3.000000 vel_rev_s=-0.125000"));
    assert(strstr(wire + before, "tx=2 rejected=0 output_allowed=0"));
    assert(wire_length - before < S1_REPLY_SIZE);
    query("encstop extra\n", "ERR args\r\n");
    EncoderStreamStatus stream; EncoderStream_GetStatus(ticks, &stream); assert(stream.active);
    query("encstop\n", "OK encoder_stream_stopped output_allowed=0\r\n");
    before = wire_length; feed("encfeedback\n", 12U); service(5U);
    assert(strstr(wire + before, "active=0 reason=1 valid=0 pairs=1 pos_rev=3.000000"));
    query("encstart\n", "ERR encoder_stream_blocked\r\n");
    assert(TestCanTx_Sent(BOARD_CAN_SHOULDER) == 0U && TestCanTx_Sent(BOARD_CAN_ELBOW) == 0U);
    S3BenchStatus gate; S3Bench_GetStatus(&gate);
    assert(gate.state == OUTPUT_SAFE_OFF && !gate.configured && !gate.transport_bound && !gate.output_allowed);
}

const char *TestConsole_QueryCached(const char *command)
{
    size_t before = wire_length;
    feed(command, strlen(command));
    for (unsigned i = 0U; i < 5U; i++) {
        BoardConsole_Poll(ticks); ConsoleCommands_Poll(ticks, 0U); complete();
    }
    assert(wire_length - before < S1_REPLY_SIZE);
    assert(!strncmp(wire + before, "OK ", 3U));
    return wire + before;
}
static void test_s3_telemetry_cli(void)
{
    TestS3Site_Reset(); S3SiteProfile profile = {0};
    profile.shoulder_source = S3_SHOULDER_ENCODER_PROPERTIES;
    profile.shoulder_property_reply_ms = 20U; profile.safety.feedback_timeout_ms = 100U;
    TestS3Site_ConfigureShoulderChain(&profile); ticks = UINT32_MAX - 32U; App_Init(); TestCanTx_Reset();
    const uint32_t values[7] = {0x40300000U, 0xBD000000U, 1U, 1U, 0x3E000000U, 3U, 3U};
    for (unsigned i = 0U; i < GDS_PROPERTY_COUNT; i++) {
        assert(CanMonitor_ExpectShoulderProperty((GdsPropertyField)i, ticks));
        uint16_t ep = GdsSdoRead_PropertyEndpoint((GdsPropertyField)i);
        uint8_t data[8] = {0U, (uint8_t)ep, (uint8_t)(ep >> 8), 0U};
        for (unsigned j = 0U; j < 4U; j++) { data[j+4U] = (uint8_t)(values[i] >> (8U*j)); }
        ticks++; TestCan_InjectData(BOARD_CAN_SHOULDER, 0x025U, 8U, 0U, 0U, data);
        TestCan_Interrupt(BOARD_CAN_SHOULDER); CanMonitor_Poll(); ticks++;
    }
    const char *a = TestConsole_QueryCached("s3feedback\n");
    assert(strstr(a, "seq=1") && strstr(a, "fresh=1 pos=40300000 vel=BD000000"));
    char original[S1_REPLY_SIZE]; strcpy(original, a);
    assert(!strcmp(original, TestConsole_QueryCached("s3feedback\n")));
    a = TestConsole_QueryCached("s3properties\n");
    assert(strstr(a, "fresh=1 tq=3E000000 cal=1 ready=1 cm=3 im=3"));
    const char *commands[] = {"s3reader\n", "s3limits\n", "s3timing\n", "s3stopcfg\n", "qualstatus\n", "qualfield 6\n", "qualerror\n", "s3help\n"};
    for (unsigned i = 0U; i < sizeof(commands)/sizeof(commands[0]); i++) { (void)TestConsole_QueryCached(commands[i]); }
    assert(TestCanTx_Sent(BOARD_CAN_SHOULDER) == 0U && TestCanTx_Sent(BOARD_CAN_ELBOW) == 0U);
    query("qualfield 7\n", "ERR args\r\n"); query("qualfield -1\n", "ERR args\r\n");
    query("qualfield 1 extra\n", "ERR args\r\n"); query("s3feedback extra\n", "ERR args\r\n");
    ticks += 101U;
    a = TestConsole_QueryCached("s3feedback\n");
    assert(strstr(a, "seq=1") && strstr(a, "fresh=0 pos=40300000 vel=BD000000"));
    CanMonitor_InvalidateShoulderProperties();
    a = TestConsole_QueryCached("s3properties\n");
    assert(strstr(a, "seq=1") && strstr(a, "fresh=0"));
    assert(TestCanTx_Sent(BOARD_CAN_SHOULDER) == 0U);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    if (!strcmp(argv[1], "s2_stream_cli")) { test_stream_cli(); }
    else if (!strncmp(argv[1], "s2_stream_", 10)) { TestEncoderStream_Run(argv[1]); }
    else if (!strcmp(argv[1], "s2_sdo_cli")) { test_sdo_cli(); }
    else if (!strncmp(argv[1], "s2_sdo_", 7)) { TestSdo_Run(argv[1]); }
    else if (!strcmp(argv[1], "commands")) { test_commands(); }
    else if (!strcmp(argv[1], "can_commands")) { test_can_commands(false); }
    else if (!strcmp(argv[1], "can_failure_console")) { test_can_commands(true); }
    else if (!strcmp(argv[1], "c620_console")) { test_c620_console(false); }
    else if (!strcmp(argv[1], "c620_failure_console")) { test_c620_console(true); }
    else if (!strcmp(argv[1], "s3_telemetry_cli")) { test_s3_telemetry_cli(); }
    else if (!strncmp(argv[1], "shoulder_qual_", 14)) { TestShoulderQualification_Run(argv[1]); }
    else if (!strcmp(argv[1], "s3_shoulder_console")) { test_shoulder_console(); }
    else if (!strcmp(argv[1], "s3_bench_stop_cli")) { test_bench_stop_cli(); }
    else if (!strcmp(argv[1], "s3_cli")) { test_s3_cli(); }
    else if (!strcmp(argv[1], "s3_stop_input")) { TestStopInput_Run(); }
    else if (!strcmp(argv[1], "s3_shoulder_standstill")) { TestShoulderStandstill(); }
    else if (!strncmp(argv[1], "s3_gateway_", 11)) { TestGatewayDeadlines_Run(argv[1]); }
    else if (!strncmp(argv[1], "s3_chain_", 9)) { TestShoulderChain_Run(argv[1]); }
    else if (!strncmp(argv[1], "s3_properties_", 14)) { TestShoulderProperties_Run(argv[1]); }
    else if (!strncmp(argv[1], "s3_", 3)) { TestS3_Run(argv[1]); }
    else if (!strncmp(argv[1], "c620_", 5)) { TestC620_Run(argv[1]); }
    else if (!strncmp(argv[1], "can_", 4)) { TestCan_Run(argv[1]); }
    else if (!strcmp(argv[1], "rx_positions")) { test_rx_positions(); }
    else if (!strcmp(argv[1], "tx_queue")) { test_tx_queue(); }
    else if (!strcmp(argv[1], "backpressure")) { test_backpressure(); }
    else if (!strcmp(argv[1], "late_poll")) { test_late_poll(); }
    else if (!strcmp(argv[1], "timeout_race")) { test_timeout_race(); }
    else if (!strcmp(argv[1], "scheduler")) { test_scheduler(); }
    else if (!strcmp(argv[1], "priorities")) { test_priorities(); }
    else { test_fault(argv[1]); }
    printf("PASS %s\n", argv[1]);
    return 0;
}
