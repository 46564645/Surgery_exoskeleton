/* Host-only HAL seam. Never included by firmware CMake. */
#ifndef HOST_MAIN_H
#define HOST_MAIN_H
#include <stdint.h>
typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY } HAL_StatusTypeDef;
#include "can_hal.h"
typedef struct { uint32_t remaining; } DMA_HandleTypeDef;
typedef struct { DMA_HandleTypeDef *hdmarx; } UART_HandleTypeDef;
typedef enum { SysTick_IRQn, EXTI15_10_IRQn, USART2_IRQn,
               DMA1_Stream5_IRQn, DMA1_Stream6_IRQn } IRQn_Type;
#define NVIC_PRIORITYGROUP_4 4U
#define LED2 0U
#define __HAL_DMA_GET_COUNTER(dma) ((dma)->remaining)
uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t mask);
uint32_t HAL_GetTick(void);
uint32_t HAL_NVIC_GetPriorityGrouping(void);
void HAL_NVIC_SetPriority(IRQn_Type irq, uint32_t preempt, uint32_t sub);
void HAL_NVIC_GetPriority(IRQn_Type irq, uint32_t group, uint32_t *preempt, uint32_t *sub);
void BSP_LED_Toggle(unsigned led);
HAL_StatusTypeDef HAL_UARTEx_ReceiveToIdle_DMA(UART_HandleTypeDef *, uint8_t *, uint16_t);
HAL_StatusTypeDef HAL_UART_Transmit_DMA(UART_HandleTypeDef *, const uint8_t *, uint16_t);
HAL_StatusTypeDef HAL_UART_Abort_IT(UART_HandleTypeDef *);
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *, uint16_t);
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *);
void HAL_UART_ErrorCallback(UART_HandleTypeDef *);
#include "stop_input_hal.h"
#endif
