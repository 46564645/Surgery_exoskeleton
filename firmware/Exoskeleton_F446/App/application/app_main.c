/* L4: static S1 scheduling, using HAL-backed millisecond time through L1 API. */
#include "app_main.h"
#include "board_console.h"
#include "board_runtime.h"
#include "console_commands.h"
#include "s1_config.h"
#include <stdint.h>

static uint32_t last_service, last_led, overruns;

void App_Init(void)
{
    last_service = BoardRuntime_NowMs();
    last_led = last_service;
    overruns = 0U;
    ConsoleCommands_Init();
    (void)BoardConsole_Start(); /* Failure is latched; Poll shows fault heartbeat. */
}

void App_Poll(void)
{
    uint32_t now = BoardRuntime_NowMs();
    uint32_t elapsed = now - last_service;
    if (elapsed >= S1_SERVICE_MS) {
        if (elapsed > S1_SERVICE_MS && overruns != UINT32_MAX) { overruns++; }
        last_service = now; /* Skip missed slots, never replay a backlog. */
        BoardConsole_Poll(now);
        ConsoleCommands_Poll(now, overruns);
    }
    uint32_t led_period = BoardConsole_HasFault() ? S1_FAULT_LED_MS : S1_LED_MS;
    if ((uint32_t)(now - last_led) >= led_period) {
        last_led = now;
        BoardRuntime_ToggleLed();
    }
}
