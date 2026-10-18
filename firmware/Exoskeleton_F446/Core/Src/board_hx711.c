/* L1: bit-banged HX711 read; pin modes are set in MX_GPIO_Init (main.c).
 * Pulse/read order follows STM32-Libraries HX711.c (shiftIn MSB first, then
 * gain pulses); its blocking wait and DOUT pull-up are not used. */
#include "board_hx711.h"
#include "main.h"

#define HX711_SCK GPIO_PIN_3
#define HX711_DOUT GPIO_PIN_2
#define HX711_PULSES 25U /* Channel A, gain 128 for the next conversion. */

/* About 1-2 us at the current core clock: datasheet T3/T4 >= 0.2 us and the
 * high time stays far below the 50 us limit. */
static void settle(void)
{
    for (volatile unsigned i = 0U; i < 20U; i++) { }
}

bool BoardHx711_DataReady(void)
{
    return (GPIOC->IDR & HX711_DOUT) == 0U;
}

uint32_t BoardHx711_ReadRaw24(void)
{
    uint32_t raw = 0U;
    for (unsigned pulse = 0U; pulse < HX711_PULSES; pulse++) {
        uint32_t primask = __get_PRIMASK();
        __disable_irq();
        GPIOC->BSRR = HX711_SCK;
        settle();
        uint32_t level = (GPIOC->IDR & HX711_DOUT) != 0U ? 1U : 0U;
        GPIOC->BSRR = (uint32_t)HX711_SCK << 16U;
        __set_PRIMASK(primask);
        if (pulse < 24U) { raw = (raw << 1U) | level; }
        settle();
    }
    return raw;
}
