#ifndef BB_TEST_MAIN_H
#define BB_TEST_MAIN_H
#include <stdint.h>
typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef struct { uint32_t TIR, TDTR, TDLR, TDHR; } Mailbox;
typedef struct { uint32_t ESR, TSR, MSR, MCR, BTR, IER; Mailbox sTxMailBox[3]; } CAN_TypeDef;
typedef struct { CAN_TypeDef *Instance; uint32_t error; } CAN_HandleTypeDef;
typedef struct { uint32_t StdId, ExtId, IDE, RTR, DLC; } CAN_TxHeaderTypeDef;
typedef CAN_TxHeaderTypeDef CAN_RxHeaderTypeDef;
typedef struct { uint32_t CR, PLLCFGR, CFGR; } RCC_TypeDef;
typedef struct { uint32_t LOAD; } SysTick_Type;
extern CAN_TypeDef mock_can1, mock_can2;
extern RCC_TypeDef mock_rcc;
extern SysTick_Type mock_systick;
extern uint32_t mock_mask, mock_tick;
#define CAN1 (&mock_can1)
#define RCC (&mock_rcc)
#define SysTick (&mock_systick)
#define CAN_ESR_LEC 0x70U
#define CAN_ESR_LEC_Pos 4U
#define CAN_MSR_ERRI 4U
#define CAN_ID_STD 0U
#define CAN_RTR_DATA 0U
#define HAL_CAN_ERROR_NONE 0U
static inline uint32_t __get_PRIMASK(void) { return mock_mask; }
static inline void __disable_irq(void) { mock_mask = 1U; }
static inline void __set_PRIMASK(uint32_t n) { mock_mask = n; }
static inline uint32_t HAL_GetTick(void) { return mock_tick; }
static inline uint32_t HAL_CAN_GetError(CAN_HandleTypeDef *h) { return h->error; }
#endif
