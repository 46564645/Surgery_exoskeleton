#include "can_mock.h"
#include <assert.h>
#include <string.h>
static CAN_TypeDef registers[2] = {{0x001A0005U, 0}, {0x001A0002U, 0}};
CAN_HandleTypeDef hcan1 = {&registers[0], 0, HAL_CAN_STATE_READY};
CAN_HandleTypeDef hcan2 = {&registers[1], 0, HAL_CAN_STATE_READY};
typedef struct { CAN_RxHeaderTypeDef header; uint8_t data[8]; } MockFrame;
static MockFrame fifo[2][128];
static unsigned head[2], tail[2];
static uint32_t notifications[2];
static const char *failure[2];
static unsigned index_of(const CAN_HandleTypeDef *h)
{ assert(h == &hcan1 || h == &hcan2); return h == &hcan1 ? 0U : 1U; }
static HAL_StatusTypeDef result(CAN_HandleTypeDef *h, const char *op)
{
    const char *fail = failure[index_of(h)];
    if (fail != NULL && !strcmp(fail, op)) { h->ErrorCode |= HAL_CAN_ERROR_TIMEOUT; return HAL_ERROR; }
    return HAL_OK;
}
void TestCan_Fail(BoardCanBus bus, const char *op) { failure[bus] = op; }
uint32_t TestCan_Notifications(BoardCanBus bus) { return notifications[bus]; }
uint32_t HAL_RCC_GetPCLK1Freq(void) { return 42000000U; }
HAL_CAN_StateTypeDef HAL_CAN_GetState(const CAN_HandleTypeDef *h) { return h->State; }
uint32_t HAL_CAN_GetError(const CAN_HandleTypeDef *h) { return h->ErrorCode; }
HAL_StatusTypeDef HAL_CAN_ResetError(CAN_HandleTypeDef *h) { h->ErrorCode = 0; return HAL_OK; }
HAL_StatusTypeDef HAL_CAN_ConfigFilter(CAN_HandleTypeDef *h, const CAN_FilterTypeDef *f)
{
    unsigned bus = index_of(h);
    assert(f->SlaveStartFilterBank == 14 && f->FilterBank == (bus == 0 ? 0U : 14U));
    assert(f->FilterFIFOAssignment == bus && f->FilterMode == CAN_FILTERMODE_IDMASK);
    assert(f->FilterScale == CAN_FILTERSCALE_32BIT && f->FilterActivation == ENABLE);
    assert(f->FilterMaskIdHigh == 0 && f->FilterMaskIdLow == 6);
    assert(f->FilterIdHigh == 0 && f->FilterIdLow == 0);
    return result(h, "filter");
}
HAL_StatusTypeDef HAL_CAN_Start(CAN_HandleTypeDef *h)
{
    HAL_StatusTypeDef r = result(h, "start");
    if (r == HAL_OK) { h->State = HAL_CAN_STATE_LISTENING; }
    return r;
}
HAL_StatusTypeDef HAL_CAN_Stop(CAN_HandleTypeDef *h) { h->State = HAL_CAN_STATE_READY; return HAL_OK; }
HAL_StatusTypeDef HAL_CAN_ActivateNotification(CAN_HandleTypeDef *h, uint32_t mask)
{
    assert((mask & CAN_IT_ERROR) != 0);
    uint32_t expected = index_of(h) == 0 ? CAN_IT_RX_FIFO0_MSG_PENDING : CAN_IT_RX_FIFO1_MSG_PENDING;
    assert((mask & expected) != 0);
    assert((mask & 1U) == 0); /* No TX mailbox IRQ. */
    HAL_StatusTypeDef r = result(h, "notify");
    if (r == HAL_OK) { notifications[index_of(h)] = mask; }
    return r;
}
HAL_StatusTypeDef HAL_CAN_DeactivateNotification(CAN_HandleTypeDef *h, uint32_t mask)
{ notifications[index_of(h)] &= ~mask; return HAL_OK; }
uint32_t HAL_CAN_GetRxFifoFillLevel(const CAN_HandleTypeDef *h, uint32_t which)
{ unsigned b = index_of(h); assert(which == b); return head[b] - tail[b]; }
HAL_StatusTypeDef HAL_CAN_GetRxMessage(CAN_HandleTypeDef *h, uint32_t which, CAN_RxHeaderTypeDef *header, uint8_t *data)
{
    unsigned b = index_of(h); assert(which == b && head[b] > tail[b]);
    HAL_StatusTypeDef r = result(h, "receive");
    if (r != HAL_OK) { return r; }
    MockFrame *f = &fifo[b][tail[b]++ % 128U];
    *header = f->header; memcpy(data, f->data, 8); return HAL_OK;
}
void TestCan_Inject(BoardCanBus b, uint32_t id, uint32_t dlc, uint32_t ide, uint32_t rtr, uint8_t value)
{
    uint8_t data[8]; memset(data, value, sizeof(data));
    TestCan_InjectData(b, id, dlc, ide, rtr, data);
}
void TestCan_InjectData(BoardCanBus b, uint32_t id, uint32_t dlc,
                        uint32_t ide, uint32_t rtr, const uint8_t data[8])
{
    assert(head[b] - tail[b] < 128U);
    MockFrame *f = &fifo[b][head[b]++ % 128U];
    f->header = (CAN_RxHeaderTypeDef){id, id, ide, rtr, dlc}; memcpy(f->data, data, 8);
}
void TestCan_Interrupt(BoardCanBus b)
{
    if (b == BOARD_CAN_SHOULDER) { HAL_CAN_RxFifo0MsgPendingCallback(&hcan1); }
    else { HAL_CAN_RxFifo1MsgPendingCallback(&hcan2); }
}
