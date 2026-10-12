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
    hcan1.Instance->TSR = hcan2.Instance->TSR = 0U;
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
void TestCanTx_Complete(BoardCanBus bus)
{
    CAN_TypeDef *can = bus == BOARD_CAN_SHOULDER ? hcan1.Instance : hcan2.Instance;
    if ((occupied[bus] & CAN_TX_MAILBOX0) != 0U) { can->TSR |= CAN_TSR_RQCP0 | CAN_TSR_TXOK0; }
    if ((occupied[bus] & CAN_TX_MAILBOX1) != 0U) { can->TSR |= CAN_TSR_RQCP1 | CAN_TSR_TXOK1; }
    if ((occupied[bus] & CAN_TX_MAILBOX2) != 0U) { can->TSR |= CAN_TSR_RQCP2 | CAN_TSR_TXOK2; }
    occupied[bus] = 0U;
}
void TestCanTx_CompleteFailed(BoardCanBus bus)
{
    CAN_TypeDef *can = bus == BOARD_CAN_SHOULDER ? hcan1.Instance : hcan2.Instance;
    if ((occupied[bus] & CAN_TX_MAILBOX0) != 0U) {
        can->TSR = (can->TSR & ~CAN_TSR_TXOK0) | CAN_TSR_RQCP0 | CAN_TSR_TERR0;
    }
    if ((occupied[bus] & CAN_TX_MAILBOX1) != 0U) {
        can->TSR = (can->TSR & ~CAN_TSR_TXOK1) | CAN_TSR_RQCP1 | CAN_TSR_TERR1;
    }
    if ((occupied[bus] & CAN_TX_MAILBOX2) != 0U) {
        can->TSR = (can->TSR & ~CAN_TSR_TXOK2) | CAN_TSR_RQCP2 | CAN_TSR_TERR2;
    }
    occupied[bus] = 0U; /* Mailbox free after failed completion, no TXOK proof. */
}
void TestCanTx_LoseArbitration(BoardCanBus bus)
{
    CAN_TypeDef *can = bus == BOARD_CAN_SHOULDER ? hcan1.Instance : hcan2.Instance;
    if ((occupied[bus] & CAN_TX_MAILBOX0) != 0U) {
        can->TSR = (can->TSR & ~(CAN_TSR_TXOK0 | CAN_TSR_TERR0))
            | CAN_TSR_RQCP0 | CAN_TSR_ALST0;
    }
    if ((occupied[bus] & CAN_TX_MAILBOX1) != 0U) {
        can->TSR = (can->TSR & ~(CAN_TSR_TXOK1 | CAN_TSR_TERR1))
            | CAN_TSR_RQCP1 | CAN_TSR_ALST1;
    }
    if ((occupied[bus] & CAN_TX_MAILBOX2) != 0U) {
        can->TSR = (can->TSR & ~(CAN_TSR_TXOK2 | CAN_TSR_TERR2))
            | CAN_TSR_RQCP2 | CAN_TSR_ALST2;
    }
    occupied[bus] = 0U;
}
void TestCanTx_CompleteUnknown(BoardCanBus bus)
{
    CAN_TypeDef *can = bus == BOARD_CAN_SHOULDER ? hcan1.Instance : hcan2.Instance;
    can->TSR &= ~(CAN_TSR_RQCP0 | CAN_TSR_TXOK0 | CAN_TSR_ALST0 | CAN_TSR_TERR0
        | CAN_TSR_RQCP1 | CAN_TSR_TXOK1 | CAN_TSR_ALST1 | CAN_TSR_TERR1
        | CAN_TSR_RQCP2 | CAN_TSR_TXOK2 | CAN_TSR_ALST2 | CAN_TSR_TERR2);
    occupied[bus] = 0U;
}
void TestCanTx_CompleteOnAdd(BoardCanBus bus) { complete_on_add[bus] = true; }
void TestCanTx_AbortCompletes(BoardCanBus bus, bool completes_immediately)
{ complete_abort[bus] = completes_immediately; }
void TestCanTx_CompleteAbort(BoardCanBus bus)
{
    CAN_TypeDef *can = bus == BOARD_CAN_SHOULDER ? hcan1.Instance : hcan2.Instance;
    if ((occupied[bus] & CAN_TX_MAILBOX0) != 0U) {
        can->TSR = (can->TSR & ~CAN_TSR_TXOK0) | CAN_TSR_RQCP0;
    }
    if ((occupied[bus] & CAN_TX_MAILBOX1) != 0U) {
        can->TSR = (can->TSR & ~CAN_TSR_TXOK1) | CAN_TSR_RQCP1;
    }
    if ((occupied[bus] & CAN_TX_MAILBOX2) != 0U) {
        can->TSR = (can->TSR & ~CAN_TSR_TXOK2) | CAN_TSR_RQCP2;
    }
    occupied[bus] = 0U;
}
const BoardCanFrame *TestCanTx_Pending(BoardCanBus bus, unsigned slot)
{ assert(slot<3U); return (occupied[bus] & (1U<<slot)) != 0U ? &slots[bus][slot] : NULL; }
HAL_StatusTypeDef HAL_CAN_AbortTxRequest(CAN_HandleTypeDef *h, uint32_t mask)
{
    unsigned bus = index_of(h);
    assert(mask != 0U && (mask & ~7U) == 0U);
    aborts[bus]++;
    if (fail_abort[bus]) { return HAL_ERROR; }
    if (complete_abort[bus]) {
        occupied[bus] &= ~mask;
        if ((mask & CAN_TX_MAILBOX0) != 0U) { h->Instance->TSR &= ~CAN_TSR_TXOK0; }
        if ((mask & CAN_TX_MAILBOX1) != 0U) { h->Instance->TSR &= ~CAN_TSR_TXOK1; }
        if ((mask & CAN_TX_MAILBOX2) != 0U) { h->Instance->TSR &= ~CAN_TSR_TXOK2; }
    }
    return HAL_OK;
}
HAL_StatusTypeDef HAL_CAN_AddTxMessage(CAN_HandleTypeDef *h, const CAN_TxHeaderTypeDef *header,
                                     const uint8_t data[], uint32_t *mailbox)
{
    unsigned bus = index_of(h);
    assert(header->IDE == CAN_ID_STD && header->RTR == CAN_RTR_DATA);
    assert(header->TransmitGlobalTime == DISABLE && header->DLC <= 8U);
    if (fail_next[bus]) { fail_next[bus] = false; return HAL_ERROR; }
    if (complete_on_add[bus]) { complete_on_add[bus] = false; TestCanTx_Complete((BoardCanBus)bus); }
    if (fail_add[bus] || occupied[bus] == 7U) { return HAL_ERROR; }
    unsigned slot=0U; while ((occupied[bus] & (1U<<slot)) != 0U) { slot++; }
    *mailbox=1U<<slot; occupied[bus] |= *mailbox;
    if (*mailbox == CAN_TX_MAILBOX0) { h->Instance->TSR &= ~(CAN_TSR_RQCP0 | CAN_TSR_TXOK0 | CAN_TSR_ALST0 | CAN_TSR_TERR0); }
    if (*mailbox == CAN_TX_MAILBOX1) { h->Instance->TSR &= ~(CAN_TSR_RQCP1 | CAN_TSR_TXOK1 | CAN_TSR_ALST1 | CAN_TSR_TERR1); }
    if (*mailbox == CAN_TX_MAILBOX2) { h->Instance->TSR &= ~(CAN_TSR_RQCP2 | CAN_TSR_TXOK2 | CAN_TSR_ALST2 | CAN_TSR_TERR2); }
    sent[bus]++;
    latest[bus] = (BoardCanFrame){0};
    latest[bus].id = (uint16_t)header->StdId; latest[bus].dlc = (uint8_t)header->DLC;
    memcpy(latest[bus].data, data, 8);
    slots[bus][slot] = latest[bus];
    return HAL_OK;
}
