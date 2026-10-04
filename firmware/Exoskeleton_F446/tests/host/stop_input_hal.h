#ifndef TEST_STOP_INPUT_HAL_H
#define TEST_STOP_INPUT_HAL_H
/* Host-only HAL seam for the actual L1 reader. No GPIO initialization API. */
#include <stdbool.h>
#include <stdint.h>
typedef struct { volatile uint32_t MODER, IDR; } GPIO_TypeDef;
typedef enum { GPIO_PIN_RESET = 0U, GPIO_PIN_SET = 1U } GPIO_PinState;
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *gpio, uint16_t pin);

GPIO_TypeDef *StopInputMock_Port(unsigned index);
bool StopInputMock_ClockEnabled(unsigned index);
void TestStopInputMock_Reset(void);
GPIO_TypeDef *TestStopInputMock_GPIO(unsigned index);
void TestStopInputMock_SetClock(unsigned index, bool enabled);
unsigned TestStopInputMock_ClockChecks(void);
unsigned TestStopInputMock_PortLookups(void);
unsigned TestStopInputMock_Reads(void);

#define GPIOA StopInputMock_Port(0U)
#define GPIOB StopInputMock_Port(1U)
#define GPIOC StopInputMock_Port(2U)
#define GPIOD StopInputMock_Port(3U)
#define GPIOE StopInputMock_Port(4U)
#define GPIOF StopInputMock_Port(5U)
#define GPIOG StopInputMock_Port(6U)
#define GPIOH StopInputMock_Port(7U)
#define __HAL_RCC_GPIOA_IS_CLK_ENABLED() StopInputMock_ClockEnabled(0U)
#define __HAL_RCC_GPIOB_IS_CLK_ENABLED() StopInputMock_ClockEnabled(1U)
#define __HAL_RCC_GPIOC_IS_CLK_ENABLED() StopInputMock_ClockEnabled(2U)
#define __HAL_RCC_GPIOD_IS_CLK_ENABLED() StopInputMock_ClockEnabled(3U)
#define __HAL_RCC_GPIOE_IS_CLK_ENABLED() StopInputMock_ClockEnabled(4U)
#define __HAL_RCC_GPIOF_IS_CLK_ENABLED() StopInputMock_ClockEnabled(5U)
#define __HAL_RCC_GPIOG_IS_CLK_ENABLED() StopInputMock_ClockEnabled(6U)
#define __HAL_RCC_GPIOH_IS_CLK_ENABLED() StopInputMock_ClockEnabled(7U)
#endif
