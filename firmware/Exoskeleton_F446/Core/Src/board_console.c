/* L1: HAL UART/DMA adapter. Reference: MaJerle usart_rx_check, LwRB.
 * HT/TC/IDLE are notifications: sample live NDTR, not stale HAL Size payloads.
 * USART2 and DMA RX IRQs have equal preemption priority (single producer).
 * Service latency must remain below half a DMA buffer (~22ms at 115200 8N1).
 * NDTR alone cannot detect a complete unserviced DMA lap.
 */
#include "board_console.h"
#include "main.h"
#include "s1_config.h"
#include "lwrb/lwrb.h"

extern UART_HandleTypeDef huart2; /* CubeMX-owned handle, private to L1. */
static uint8_t rx_dma[S1_RX_DMA_SIZE];
static uint8_t rx_storage[S1_RX_STORAGE_SIZE];
static uint8_t tx_storage[S1_TX_STORAGE_SIZE];
static uint8_t tx_chunk[S1_TX_CHUNK_SIZE];
static lwrb_t rx_queue, tx_queue;
static size_t rx_pos, tx_length;
static uint32_t tx_started_ms;
static bool started, tx_active, abort_requested;
static volatile bool tx_complete;
static volatile BoardConsoleStats stats;

static uint32_t lock(void)
{
    uint32_t saved = __get_PRIMASK();
    __disable_irq();
    return saved;
}

static void latch_fault(BoardConsoleFault fault)
{
    uint32_t saved = lock();
    if (stats.fault == BOARD_CONSOLE_OK) {
        stats.fault = fault;
        if (fault == BOARD_CONSOLE_UART_ERROR) { stats.uart_errors++; }
        if (fault == BOARD_CONSOLE_RX_OVERFLOW) { stats.rx_overflows++; }
        if (fault == BOARD_CONSOLE_TX_TIMEOUT) { stats.tx_timeouts++; }
    }
    __set_PRIMASK(saved);
}

bool BoardConsole_Start(void)
{
    if (started || BoardConsole_HasFault()) { return false; }
    lwrb_init(&rx_queue, rx_storage, sizeof(rx_storage));
    lwrb_init(&tx_queue, tx_storage, sizeof(tx_storage));
    started = true;
    if (HAL_UARTEx_ReceiveToIdle_DMA(&huart2, rx_dma, sizeof(rx_dma)) != HAL_OK) {
        latch_fault(BOARD_CONSOLE_UART_ERROR);
    }
    return !BoardConsole_HasFault();
}

static void receive_available(void)
{
    size_t pos = (S1_RX_DMA_SIZE - __HAL_DMA_GET_COUNTER(huart2.hdmarx))
        % S1_RX_DMA_SIZE;
    size_t count = (pos + S1_RX_DMA_SIZE - rx_pos) % S1_RX_DMA_SIZE;
    if (count == 0U) { return; }
    if (lwrb_get_free(&rx_queue) < count) {
        latch_fault(BOARD_CONSOLE_RX_OVERFLOW);
        return;
    }
    size_t first = S1_RX_DMA_SIZE - rx_pos;
    if (first > count) { first = count; }
    lwrb_write(&rx_queue, &rx_dma[rx_pos], first);
    if (count > first) { lwrb_write(&rx_queue, rx_dma, count - first); }
    rx_pos = pos;
    stats.rx_bytes += (uint32_t)count;
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *uart, uint16_t size)
{
    (void)size;
    if (uart == &huart2 && started && !BoardConsole_HasFault()) {
        receive_available();
    }
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *uart)
{
    if (uart == &huart2) { tx_complete = true; }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart)
{
    if (uart == &huart2) { latch_fault(BOARD_CONSOLE_UART_ERROR); }
}

void BoardConsole_Poll(uint32_t now_ms)
{
    if (!started) { return; }
    if (BoardConsole_HasFault()) {
        if (!abort_requested) {
            abort_requested = true;
            /* Nonblocking abort; never restart or replay queues after fault. */
            (void)HAL_UART_Abort_IT(&huart2);
        }
        return;
    }
    if (tx_active) {
        /* Decide completion vs deadline atomically. No copying/HAL calls here.
         * TC can arrive between the first completion read and timeout decision.
         */
        uint32_t saved = lock();
        bool done = tx_complete;
        if (done) {
            tx_complete = false;
        } else if ((uint32_t)(now_ms - tx_started_ms) >= S1_TX_TIMEOUT_MS) {
            latch_fault(BOARD_CONSOLE_TX_TIMEOUT);
        }
        __set_PRIMASK(saved);
        if (!done || BoardConsole_HasFault()) { return; }
        lwrb_skip(&tx_queue, tx_length);
        stats.tx_bytes += (uint32_t)tx_length;
        tx_active = false;
    }
    tx_length = lwrb_peek(&tx_queue, 0U, tx_chunk, sizeof(tx_chunk));
    if (tx_length != 0U && !BoardConsole_HasFault()) {
        tx_started_ms = now_ms;
        tx_complete = false;
        tx_active = true;
        if (HAL_UART_Transmit_DMA(&huart2, tx_chunk, (uint16_t)tx_length) != HAL_OK) {
            latch_fault(BOARD_CONSOLE_UART_ERROR);
        }
    }
}

size_t BoardConsole_Read(uint8_t *dst, size_t capacity)
{
    if (!started || BoardConsole_HasFault() || dst == NULL) { return 0U; }
    return lwrb_read(&rx_queue, dst, capacity);
}

bool BoardConsole_Write(const uint8_t *src, size_t length)
{
    if (!started || BoardConsole_HasFault() || src == NULL || length == 0U) {
        return false;
    }
    return lwrb_write_ex(&tx_queue, src, length, NULL, LWRB_FLAG_WRITE_ALL) != 0U;
}

size_t BoardConsole_TxFree(void)
{
    return started && !BoardConsole_HasFault() ? lwrb_get_free(&tx_queue) : 0U;
}

bool BoardConsole_HasFault(void)
{
    return stats.fault != BOARD_CONSOLE_OK;
}

void BoardConsole_GetStats(BoardConsoleStats *out)
{
    if (out != NULL) {
        uint32_t saved = lock();
        *out = stats;
        __set_PRIMASK(saved);
    }
}
