/* L4: static scheduling, using HAL-backed millisecond time through L1 API. */
#include "app_main.h"
#include "board_console.h"
#include "board_runtime.h"
#include "console_commands.h"
#include "can_monitor.h"
#include "s1_config.h"
#include "s3_bench.h"
#include "s3_site.h"
#include "s4_follow.h"
#include "s4_site.h"
#include "s5_follow.h"
#include "s5_site.h"
#include <stdint.h>

static uint32_t last_service, last_led, overruns;
static bool s4_mode, s5_mode;

void App_Init(void)
{
    last_service = BoardRuntime_NowMs();
    last_led = last_service;
    overruns = 0U;
    const S4SiteProfile *s4 = S4Site_GetProfile();
    const S5SiteProfile *s5 = S5Site_GetProfile();
#ifdef S5_R2_ELBOW_TRIAL
    /* Isolated R2 image runs only the S3 elbow direction trial. */
    s4_mode = s5_mode = false;
#else
    s5_mode = S5Site_ReadReady(s5);
    s4_mode = !s5_mode && S4Site_ReadReady(s4);
#endif
    if (!s4_mode && !s5_mode) { S3Bench_Init(); }
    ConsoleCommands_Init();
    (void)BoardConsole_Start(); /* Failure is latched; Poll shows fault heartbeat. */
    CanMonitor_Init(s5_mode ? &s5->s3 : s4_mode ? &s4->s3 : S3Site_GetProfile(),
                    s5_mode && s5->s3.safety.enabled_axes == SAFETY_ALL_AXES
                        ? SHOULDER_READER_S5
                        : s4_mode ? SHOULDER_READER_S4 : SHOULDER_READER_S3);
    S4Follow_Init(s4_mode ? s4 : NULL);
    S5Follow_Init(s5_mode ? s5 : NULL);
}

void App_Poll(void)
{
    uint32_t now = BoardRuntime_NowMs();
    uint32_t elapsed = now - last_service;
    if (elapsed >= S1_SERVICE_MS) {
        if (elapsed > S1_SERVICE_MS && overruns != UINT32_MAX) { overruns++; }
        last_service = now; /* Skip missed slots, never replay a backlog. */
        BoardConsole_Poll(now);
        CanMonitor_Poll();
        if (s5_mode) { S5Follow_Poll(BoardRuntime_NowMs(), elapsed); }
        else if (s4_mode) { S4Follow_Poll(BoardRuntime_NowMs(), elapsed); }
        else { S3Bench_Poll(BoardRuntime_NowMs(), elapsed); }
        ConsoleCommands_Poll(BoardRuntime_NowMs(), overruns);
    }
    uint32_t led_period = BoardConsole_HasFault() ? S1_FAULT_LED_MS : S1_LED_MS;
    if ((uint32_t)(now - last_led) >= led_period) {
        last_led = now;
        BoardRuntime_ToggleLed();
    }
}
