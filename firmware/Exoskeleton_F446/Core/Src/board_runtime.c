/* L1: final board initialization, after CubeMX AND BSP initialization. */
#include "board_runtime.h"
#include "main.h"
#include "s1_config.h"

static bool priority_matches(IRQn_Type irq, uint32_t expected)
{
    uint32_t preempt, sub;
    HAL_NVIC_GetPriority(irq, NVIC_PRIORITYGROUP_4, &preempt, &sub);
    return preempt == expected && sub == 0U;
}

bool BoardRuntime_FinalizeInterrupts(void)
{
    if (HAL_NVIC_GetPriorityGrouping() != NVIC_PRIORITYGROUP_4) {
        return false;
    }
    /* BSP_PB_Init sets 15. Do not edit BSP; this must run AFTER it. */
    HAL_NVIC_SetPriority(EXTI15_10_IRQn, S1_IRQ_BUTTON, 0U);
    return priority_matches(EXTI15_10_IRQn, S1_IRQ_BUTTON)
        && priority_matches(SysTick_IRQn, S1_IRQ_TICK)
        && priority_matches(USART2_IRQn, S1_IRQ_COMM)
        && priority_matches(DMA1_Stream5_IRQn, S1_IRQ_COMM)
        && priority_matches(DMA1_Stream6_IRQn, S1_IRQ_COMM);
}

uint32_t BoardRuntime_NowMs(void)
{
    return HAL_GetTick();
}

void BoardRuntime_ToggleLed(void)
{
    BSP_LED_Toggle(LED2);
}

void BoardRuntime_FatalBlink(void)
{
    uint32_t last = BoardRuntime_NowMs();
    /* No command receiver is started on this path; interrupts stay enabled. */
    for (;;) {
        uint32_t now = BoardRuntime_NowMs();
        if ((uint32_t)(now - last) >= S1_FAULT_LED_MS) {
            last = now;
            BoardRuntime_ToggleLed();
        }
    }
}
