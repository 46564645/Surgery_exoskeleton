#ifndef TEST_CAN_HAL_H
#define TEST_CAN_HAL_H
/* Host-only CAN HAL seam; values below match the F446 register definitions. */
typedef struct { uint32_t BTR, ESR; } CAN_TypeDef;
typedef enum { HAL_CAN_STATE_RESET, HAL_CAN_STATE_READY, HAL_CAN_STATE_LISTENING, HAL_CAN_STATE_ERROR } HAL_CAN_StateTypeDef;
typedef struct { CAN_TypeDef *Instance; uint32_t ErrorCode; HAL_CAN_StateTypeDef State; } CAN_HandleTypeDef;
typedef struct {
    uint32_t FilterIdHigh, FilterIdLow, FilterMaskIdHigh, FilterMaskIdLow;
    uint32_t FilterFIFOAssignment, FilterBank, FilterMode, FilterScale, FilterActivation, SlaveStartFilterBank;
} CAN_FilterTypeDef;
typedef struct { uint32_t StdId, ExtId, IDE, RTR, DLC; } CAN_RxHeaderTypeDef;
typedef struct { uint32_t StdId, ExtId, IDE, RTR, DLC, TransmitGlobalTime; } CAN_TxHeaderTypeDef;
#define CAN_BTR_BRP 0x3FFU
#define CAN_BTR_TS1 0xF0000U
#define CAN_BTR_TS2 0x700000U
#define CAN_BTR_TS1_Pos 16U
#define CAN_BTR_TS2_Pos 20U
#define CAN_ESR_BOFF 4U
#define CAN_RI0R_IDE 4U
#define CAN_RI0R_RTR 2U
#define CAN_ID_STD 0U
#define CAN_ID_EXT 4U
#define CAN_RTR_DATA 0U
#define CAN_RTR_REMOTE 2U
#define CAN_RX_FIFO0 0U
#define CAN_RX_FIFO1 1U
#define CAN_FILTERMODE_IDMASK 0U
#define CAN_FILTERSCALE_32BIT 1U
#define ENABLE 1U
#define DISABLE 0U
#define CAN_TX_MAILBOX0 1U
#define CAN_TX_MAILBOX1 2U
#define CAN_TX_MAILBOX2 4U
#define CAN_IT_RX_FIFO0_MSG_PENDING (1U << 1)
#define CAN_IT_RX_FIFO0_OVERRUN (1U << 3)
#define CAN_IT_RX_FIFO1_MSG_PENDING (1U << 4)
#define CAN_IT_RX_FIFO1_OVERRUN (1U << 6)
#define CAN_IT_ERROR_WARNING (1U << 8)
#define CAN_IT_ERROR_PASSIVE (1U << 9)
#define CAN_IT_BUSOFF (1U << 10)
#define CAN_IT_LAST_ERROR_CODE (1U << 11)
#define CAN_IT_ERROR (1U << 15)
#define HAL_CAN_ERROR_BOF 4U
#define HAL_CAN_ERROR_RX_FOV0 0x200U
#define HAL_CAN_ERROR_RX_FOV1 0x400U
#define HAL_CAN_ERROR_TIMEOUT 0x20000U
#define HAL_CAN_ERROR_ACK 0x20U
HAL_StatusTypeDef HAL_CAN_ConfigFilter(CAN_HandleTypeDef *, const CAN_FilterTypeDef *);
HAL_StatusTypeDef HAL_CAN_Start(CAN_HandleTypeDef *);
HAL_StatusTypeDef HAL_CAN_Stop(CAN_HandleTypeDef *);
HAL_StatusTypeDef HAL_CAN_ActivateNotification(CAN_HandleTypeDef *, uint32_t);
HAL_StatusTypeDef HAL_CAN_DeactivateNotification(CAN_HandleTypeDef *, uint32_t);
HAL_CAN_StateTypeDef HAL_CAN_GetState(const CAN_HandleTypeDef *);
uint32_t HAL_CAN_GetError(const CAN_HandleTypeDef *);
HAL_StatusTypeDef HAL_CAN_ResetError(CAN_HandleTypeDef *);
uint32_t HAL_CAN_GetRxFifoFillLevel(const CAN_HandleTypeDef *, uint32_t);
HAL_StatusTypeDef HAL_CAN_GetRxMessage(CAN_HandleTypeDef *, uint32_t, CAN_RxHeaderTypeDef *, uint8_t *);
uint32_t HAL_RCC_GetPCLK1Freq(void);
uint32_t HAL_CAN_IsTxMessagePending(const CAN_HandleTypeDef *, uint32_t);
uint32_t HAL_CAN_GetTxMailboxesFreeLevel(const CAN_HandleTypeDef *);
HAL_StatusTypeDef HAL_CAN_AbortTxRequest(CAN_HandleTypeDef *, uint32_t);
HAL_StatusTypeDef HAL_CAN_AddTxMessage(CAN_HandleTypeDef *, const CAN_TxHeaderTypeDef *, const uint8_t [], uint32_t *);
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *);
void HAL_CAN_RxFifo1MsgPendingCallback(CAN_HandleTypeDef *);
void HAL_CAN_ErrorCallback(CAN_HandleTypeDef *);
#endif
