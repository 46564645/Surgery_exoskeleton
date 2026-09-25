#ifndef BOARD_RUNTIME_H
#define BOARD_RUNTIME_H
#include <stdbool.h>
#include <stdint.h>

/* L1 platform API: no vendor types escape this header. */
bool BoardRuntime_FinalizeInterrupts(void);
uint32_t BoardRuntime_NowMs(void);
void BoardRuntime_ToggleLed(void);
void BoardRuntime_FatalBlink(void);
#endif
