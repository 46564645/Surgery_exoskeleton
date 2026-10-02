#ifndef BOARD_CAN_H
#define BOARD_CAN_H
/* L1: main-loop receive API. No application transmit path. */
#include <stdbool.h>
#include <stdint.h>

typedef enum { BOARD_CAN_SHOULDER, BOARD_CAN_ELBOW, BOARD_CAN_COUNT } BoardCanBus;
typedef struct {
    uint32_t received_ms;
    uint16_t id;
    uint8_t dlc;
    uint8_t data[8];
} BoardCanFrame;
typedef enum {
    BOARD_CAN_START_OK, BOARD_CAN_INIT_FAILED, BOARD_CAN_FILTER_FAILED,
    BOARD_CAN_START_FAILED, BOARD_CAN_NOTIFY_FAILED
} BoardCanStartError;
typedef struct {
    uint32_t baudrate;
    uint32_t received, dropped, invalid, fifo_overruns, errors, last_error;
    uint16_t queued;
    bool started, bus_off;
    BoardCanStartError start_error;
} BoardCanStats;

bool BoardCan_Start(void);
bool BoardCan_Read(BoardCanBus bus, BoardCanFrame *out);
bool BoardCan_GetStats(BoardCanBus bus, BoardCanStats *out);
/* CubeMX USER CODE hooks: only CAN initialization errors may return from the
 * generated Error_Handler. Clock/UART/unrelated failures keep their behavior. */
void BoardCan_InitBegin(BoardCanBus bus);
void BoardCan_InitEnd(void);
bool BoardCan_HandleInitFailure(void);
#endif
