#ifndef BOARD_CONSOLE_H
#define BOARD_CONSOLE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* L1 byte transport; only main loop calls public APIs. Reset-only fault recovery. */
typedef enum {
    BOARD_CONSOLE_OK = 0,
    BOARD_CONSOLE_UART_ERROR,
    BOARD_CONSOLE_RX_OVERFLOW,
    BOARD_CONSOLE_TX_TIMEOUT
} BoardConsoleFault;

typedef struct {
    uint32_t rx_bytes;
    uint32_t tx_bytes;
    uint32_t uart_errors;
    uint32_t rx_overflows;
    uint32_t tx_timeouts;
    BoardConsoleFault fault;
} BoardConsoleStats;

bool BoardConsole_Start(void);
void BoardConsole_Poll(uint32_t now_ms);
size_t BoardConsole_Read(uint8_t *dst, size_t capacity);
bool BoardConsole_Write(const uint8_t *src, size_t length);
size_t BoardConsole_TxFree(void);
bool BoardConsole_HasFault(void);
void BoardConsole_GetStats(BoardConsoleStats *out);
#endif
