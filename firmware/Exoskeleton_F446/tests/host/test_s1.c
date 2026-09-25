/* Behavioral tests execute the real firmware modules and pinned libraries.
 * Each scenario runs in a fresh process (reset-only transport state).
 */
#include "main.h"
#include "board_console.h"
#include "board_runtime.h"
#include "console_commands.h"
#include "app_main.h"
#include "s1_config.h"
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
    query("help\n?\n", "OK commands=ping,status,help,?\r\nOK commands=ping,status,help,?\r\n");
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
    assert(strstr(wire + before, "stage=S1_COMM") && strstr(wire + before, "output_allowed=0"));
    assert(strstr(wire + before, "uart_errors=0"));
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

int main(int argc, char **argv)
{
    assert(argc == 2);
    if (!strcmp(argv[1], "commands")) { test_commands(); }
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
