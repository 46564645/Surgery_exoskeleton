#ifndef BOARD_STOP_INPUT_H
#define BOARD_STOP_INPUT_H
#include <stdbool.h>
#include <stdint.h>

/* L1: selection only. No GPIO, pull or peripheral clock is initialized here.
 * NONE is the zero/default configuration; no button is selected implicitly. */
typedef enum {
    BOARD_STOP_PORT_NONE,
    BOARD_STOP_PORT_A, BOARD_STOP_PORT_B, BOARD_STOP_PORT_C,
    BOARD_STOP_PORT_D, BOARD_STOP_PORT_E, BOARD_STOP_PORT_F,
    BOARD_STOP_PORT_G, BOARD_STOP_PORT_H
} BoardStopPort;

typedef struct {
    BoardStopPort port;
    uint16_t pin_mask;
    bool active_high;
} BoardStopInputConfig;

/* Static shape validation only; not a wiring or physical meaning proof. */
bool BoardStopInput_ConfigValid(const BoardStopInputConfig *config);
/* Read one already-clocked GPIO configured as input. Failure clears *active.
 * This never enables a clock, configures a pin or returns a cached sample. */
bool BoardStopInput_Read(const BoardStopInputConfig *config, bool *active);
#endif
