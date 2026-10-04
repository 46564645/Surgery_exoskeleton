/* Host-only real L1 TX seam; no physical hardware. */
#include "can_tx_mock.h"
#include <assert.h>
#include <string.h>

static unsigned occupied[2], sent[2], aborts[2];
static bool complete_on_add[2];
static BoardCanFrame slots[2][3];
static bool complete_abort[2] = {true, true}, fail_add[2], fail_abort[2];
static BoardCanFrame latest[2];
static bool fail_next[2];
static unsigned index_of(const CAN_HandleTypeDef *h)
{ assert(h == &hcan1 || h == &hcan2); return h == &hcan1 ? 0U : 1U; }

void TestCanTx_Reset(void)
{
    memset(fail_next, 0, sizeof(fail_next));
    memset(sent, 0, sizeof(sent)); memset(aborts, 0, sizeof(aborts));
    memset(fail_add, 0, sizeof(fail_add)); memset(fail_abort, 0, sizeof(fail_abort));
    occupied[0] = occupied[1] = 0U;
    complete_on_add[0] = complete_on_add[1] = false;
    memset(slots, 0, sizeof(slots));
    complete_abort[0] = complete_abort[1] = true;
    hcan1.State = hcan2.State = HAL_CAN_STATE_LISTENING;
    hcan1.Instance->ESR = hcan2.Instance->ESR = 0U;
}
void TestCanTx_FailNext(BoardCanBus bus) { fail_next[bus] = true; }
void TestCanTx_Busy(BoardCanBus bus, bool abort_completes)
{ occupied[bus] = 7U; complete_abort[bus] = abort_completes; }
void TestCanTx_Fail(BoardCanBus bus, bool add, bool abort)
{ fail_add[bus] = add; fail_abort[bus] = abort; }
unsigned TestCanTx_Sent(BoardCanBus bus) { return sent[bus]; }
unsigned TestCanTx_Aborts(BoardCanBus bus) { return aborts[bus]; }
const BoardCanFrame *TestCanTx_Last(BoardCanBus bus) { return &latest[bus]; }
uint32_t HAL_CAN_GetTxMailboxesFreeLevel(const CAN_HandleTypeDef *h)
{ unsigned bits=occupied[index_of(h)]; return 3U-((bits&1U)+((bits>>1)&1U)+((bits>>2)&1U)); }
uint32_t HAL_CAN_IsTxMessagePending(const CAN_HandleTypeDef *h, uint32_t mask)
{ return (occupied[index_of(h)] & mask) != 0U; }
void TestCanTx_Complete(BoardCanBus bus) { occupied[bus] = 0U; }
void TestCanTx_CompleteOnAdd(BoardCanBus bus) { complete_on_add[bus] = true; }
const BoardCanFrame *TestCanTx_Pending(BoardCanBus bus, unsigned slot)
{ assert(slot<3U); return (occupied[bus] & (1U<<slot)) != 0U ? &slots[bus][slot] : NULL; }
HAL_StatusTypeDef HAL_CAN_AbortTxRequest(CAN_HandleTypeDef *h, uint32_t mask)
{
    unsigned bus = index_of(h);
    assert(mask != 0U && (mask & ~7U) == 0U);
    aborts[bus]++;
    if (fail_abort[bus]) { return HAL_ERROR; }
    if (complete_abort[bus]) { occupied[bus] &= ~mask; }
    return HAL_OK;
}
HAL_StatusTypeDef HAL_CAN_AddTxMessage(CAN_HandleTypeDef *h, const CAN_TxHeaderTypeDef *header,
                                     const uint8_t data[], uint32_t *mailbox)
{
    unsigned bus = index_of(h);
    assert(header->IDE == CAN_ID_STD && header->RTR == CAN_RTR_DATA);
    assert(header->TransmitGlobalTime == DISABLE && header->DLC <= 8U);
    if (fail_next[bus]) { fail_next[bus] = false; return HAL_ERROR; }
    if (complete_on_add[bus]) { complete_on_add[bus] = false; occupied[bus] = 0U; }
    if (fail_add[bus] || occupied[bus] == 7U) { return HAL_ERROR; }
    unsigned slot=0U; while ((occupied[bus] & (1U<<slot)) != 0U) { slot++; }
    *mailbox=1U<<slot; occupied[bus] |= *mailbox;
    sent[bus]++;
    latest[bus] = (BoardCanFrame){0};
    latest[bus].id = (uint16_t)header->StdId; latest[bus].dlc = (uint8_t)header->DLC;
    memcpy(latest[bus].data, data, 8);
    slots[bus][slot] = latest[bus];
    return HAL_OK;
}
