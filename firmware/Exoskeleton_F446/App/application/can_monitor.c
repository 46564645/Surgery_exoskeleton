/* L4: main-loop-only state; bounded consumption keeps the console responsive. */
#include "can_monitor.h"
#include "s2_config.h"
#include "board_runtime.h"
#include "c620_feedback.h"
#include <stddef.h>
#include <string.h>
static BoardCanFrame last[BOARD_CAN_COUNT];
static bool seen[BOARD_CAN_COUNT];
static C620Device elbow;
_Static_assert(S2_C620_ID >= 1U && S2_C620_ID <= 8U, "C620 ID range");
_Static_assert(S2_C620_TIMEOUT_MS > 0U && S2_C620_TIMEOUT_MS < 0x80000000U,
               "C620 timeout range");

void CanMonitor_Init(void)
{
    memset(last, 0, sizeof(last));
    memset(seen, 0, sizeof(seen));
    (void)C620_Init(&elbow, S2_C620_ID, S2_C620_TIMEOUT_MS);
    (void)BoardCan_Start(); /* Per-bus failure remains queryable. */
}
void CanMonitor_Poll(void)
{
    for (unsigned bus = 0U; bus < BOARD_CAN_COUNT; bus++) {
        for (unsigned i = 0U; i < S2_CAN_POLL_BUDGET; i++) {
            BoardCanFrame frame;
            if (!BoardCan_Read((BoardCanBus)bus, &frame)) { break; }
            last[bus] = frame;
            seen[bus] = true;
            if (bus == BOARD_CAN_ELBOW) {
                (void)C620_Receive(&elbow, (BoardCanBus)bus, &frame, BoardRuntime_NowMs());
            }
        }
    }
    C620_Poll(&elbow, BoardRuntime_NowMs());
}
bool CanMonitor_GetLast(BoardCanBus bus, BoardCanFrame *out)
{
    if ((unsigned)bus >= BOARD_CAN_COUNT || out == NULL || !seen[bus]) { return false; }
    *out = last[bus];
    return true;
}
bool CanMonitor_GetElbowFeedback(C620Feedback *out)
{
    return C620_GetFeedback(&elbow, BoardRuntime_NowMs(), out);
}
