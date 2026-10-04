/* L1: read-only digital input; GPIO mode and clock must already be valid. */
#include "board_stop_input.h"
#include "main.h"
#include <stddef.h>

bool BoardStopInput_ConfigValid(const BoardStopInputConfig *config)
{
    if (config == NULL || config->port < BOARD_STOP_PORT_A
        || config->port > BOARD_STOP_PORT_H || config->pin_mask == 0U) {
        return false;
    }
    /* HAL IS_GPIO_PIN accepts multiple bits; this interface must not. */
    return (config->pin_mask & (uint16_t)(config->pin_mask - 1U)) == 0U;
}

static GPIO_TypeDef *clocked_port(BoardStopPort port)
{
    /* Clock checks are reads of RCC AHB1ENR, never clock-enable operations. */
    switch (port) {
    case BOARD_STOP_PORT_A: return __HAL_RCC_GPIOA_IS_CLK_ENABLED() ? GPIOA : NULL;
    case BOARD_STOP_PORT_B: return __HAL_RCC_GPIOB_IS_CLK_ENABLED() ? GPIOB : NULL;
    case BOARD_STOP_PORT_C: return __HAL_RCC_GPIOC_IS_CLK_ENABLED() ? GPIOC : NULL;
    case BOARD_STOP_PORT_D: return __HAL_RCC_GPIOD_IS_CLK_ENABLED() ? GPIOD : NULL;
    case BOARD_STOP_PORT_E: return __HAL_RCC_GPIOE_IS_CLK_ENABLED() ? GPIOE : NULL;
    case BOARD_STOP_PORT_F: return __HAL_RCC_GPIOF_IS_CLK_ENABLED() ? GPIOF : NULL;
    case BOARD_STOP_PORT_G: return __HAL_RCC_GPIOG_IS_CLK_ENABLED() ? GPIOG : NULL;
    case BOARD_STOP_PORT_H: return __HAL_RCC_GPIOH_IS_CLK_ENABLED() ? GPIOH : NULL;
    default: return NULL;
    }
}

bool BoardStopInput_Read(const BoardStopInputConfig *config, bool *active)
{
    if (active == NULL) { return false; }
    *active = false;
    if (!BoardStopInput_ConfigValid(config)) { return false; }
    GPIO_TypeDef *gpio = clocked_port(config->port);
    if (gpio == NULL) { return false; }
    unsigned pin = 0U;
    uint16_t bit = config->pin_mask;
    while (bit > 1U) { bit >>= 1U; pin++; }
    /* F446 MODER: 00=input; output, alternate-function and analog all fail. */
    if (((gpio->MODER >> (2U * pin)) & 3U) != 0U) { return false; }
    GPIO_PinState state = HAL_GPIO_ReadPin(gpio, config->pin_mask);
    if (state != GPIO_PIN_RESET && state != GPIO_PIN_SET) { return false; }
    *active = (state == GPIO_PIN_SET) == config->active_high;
    return true;
}
