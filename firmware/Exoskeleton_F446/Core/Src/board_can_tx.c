/* L1: ST HAL abort/free-level/AddTxMessage, BetaRobot CANTransmit interface
 * adapted to bounded calls, no wait loop/queue/TX IRQ. RX code is untouched. */
#include "board_can_tx.h"
#include "main.h"
#include <stddef.h>

extern CAN_HandleTypeDef hcan1, hcan2;
static CAN_HandleTypeDef *const handles[2] = {&hcan1, &hcan2};

static uint32_t shoulder_read_mailbox;
static uint32_t pending_read(void)
{
    HAL_CAN_StateTypeDef state = HAL_CAN_GetState(handles[0]);
    if (state != HAL_CAN_STATE_READY && state != HAL_CAN_STATE_LISTENING) { return shoulder_read_mailbox; }
    if (shoulder_read_mailbox != 0U
        && HAL_CAN_IsTxMessagePending(handles[0], shoulder_read_mailbox) == 0U) {
        shoulder_read_mailbox = 0U;
    }
    return shoulder_read_mailbox;
}
static bool axes_valid(uint8_t axes_mask)
{
    return axes_mask >= 1U && axes_mask <= 3U;
}

static bool axis_selected(uint8_t axes_mask, unsigned bus)
{
    return (axes_mask & (uint8_t)(1U << bus)) != 0U;
}

static bool listening(CAN_HandleTypeDef *h)
{
    return HAL_CAN_GetState(h) == HAL_CAN_STATE_LISTENING
        && (h->Instance->ESR & CAN_ESR_BOFF) == 0U;
}

bool BoardCanTx_PurgeAxes(uint8_t axes_mask)
{
    if (!axes_valid(axes_mask)) { return false; }
    bool complete = true;
    for (unsigned i = 0U; i < 2U; i++) {
        if (!axis_selected(axes_mask, i)) { continue; }
        CAN_HandleTypeDef *h = handles[i];
        HAL_CAN_StateTypeDef state = HAL_CAN_GetState(h);
        if (state != HAL_CAN_STATE_READY && state != HAL_CAN_STATE_LISTENING) {
            complete = false;
            continue;
        }
        if (HAL_CAN_GetTxMailboxesFreeLevel(h) == 3U) { continue; }
        /* HAL_OK means ABRQ written, NOT cancellation complete. Never spin. */
        if (HAL_CAN_AbortTxRequest(h, CAN_TX_MAILBOX0 | CAN_TX_MAILBOX1 | CAN_TX_MAILBOX2) != HAL_OK
            || HAL_CAN_GetTxMailboxesFreeLevel(h) != 3U) { complete = false; }
    }
    return complete;
}

bool BoardCanTx_Purge(void)
{
    return BoardCanTx_PurgeAxes(3U);
}

static bool send_mailbox(unsigned bus, const BoardCanFrame *f, uint32_t *mailbox)
{
    CAN_HandleTypeDef *h = handles[bus];
    if (!listening(h) || HAL_CAN_GetTxMailboxesFreeLevel(h) == 0U) { return false; }
    CAN_TxHeaderTypeDef header = {0};
    header.StdId = f->id;
    header.IDE = CAN_ID_STD;
    header.RTR = CAN_RTR_DATA;
    header.DLC = f->dlc;
    header.TransmitGlobalTime = DISABLE;
    /* ST HAL copies header/data to peripheral registers synchronously. */
    bool ok = HAL_CAN_AddTxMessage(h, &header, f->data, mailbox) == HAL_OK;
    /* Hardware can complete the read between preflight and AddTxMessage.
     * HAL may then allocate its old slot to this output. Reconcile the owner. */
    if (ok && bus == 0U && mailbox != &shoulder_read_mailbox && *mailbox == shoulder_read_mailbox) {
        shoulder_read_mailbox = 0U;
    }
    return ok;
}

static bool submit_axes(const BoardCanFrame frames[2], uint8_t axes_mask, bool stop, bool keep_read)
{
    if (frames == NULL || !axes_valid(axes_mask)) { return false; }
    bool frame_valid[2] = {false, false};
    for (unsigned i = 0U; i < 2U; i++) {
        if (!axis_selected(axes_mask, i)) { continue; }
        frame_valid[i] = frames[i].id <= 0x7FFU && frames[i].dlc <= 8U;
        if (!frame_valid[i] && !stop) { return false; }
    }
    if (!stop) {
        for (unsigned i = 0U; i < 2U; i++) {
            if (!axis_selected(axes_mask, i)) { continue; }
            uint32_t keep = keep_read && i == 0U ? pending_read() : 0U;
            uint32_t other = (CAN_TX_MAILBOX0 | CAN_TX_MAILBOX1 | CAN_TX_MAILBOX2) & ~keep;
            if (!listening(handles[i]) || (keep_read
                ? HAL_CAN_IsTxMessagePending(handles[i], other) != 0U
                : HAL_CAN_GetTxMailboxesFreeLevel(handles[i]) != 3U)) { return false; }
        }
    }
    bool accepted = true;
    for (unsigned i = 0U; i < 2U; i++) {
        if (!axis_selected(axes_mask, i)) { continue; }
        /* Malformed stop on one axis cannot suppress another valid stop. */
        if (!frame_valid[i]) { accepted = false; continue; }
        uint32_t mailbox;
        if (!send_mailbox(i, &frames[i], &mailbox)) {
            accepted = false;
            /* A failed stop must not suppress another selected bus's stop. */
            if (!stop) { break; }
        }
    }
    return accepted;
}

bool BoardCanTx_SubmitAxes(const BoardCanFrame frames[2], uint8_t axes_mask, bool stop)
{ return submit_axes(frames, axes_mask, stop, false); }
bool BoardCanTx_SubmitOutputAxes(const BoardCanFrame frames[2], uint8_t axes_mask, bool stop)
{ return submit_axes(frames, axes_mask, stop, true); }
bool BoardCanTx_PurgeOutputAxes(uint8_t axes_mask)
{
    if (!axes_valid(axes_mask)) { return false; }
    bool complete = true;
    for (unsigned i = 0U; i < 2U; i++) {
        if (!axis_selected(axes_mask, i)) { continue; }
        CAN_HandleTypeDef *h = handles[i];
        HAL_CAN_StateTypeDef state = HAL_CAN_GetState(h);
        if (state != HAL_CAN_STATE_READY && state != HAL_CAN_STATE_LISTENING) { complete = false; continue; }
        uint32_t keep = i == 0U ? pending_read() : 0U;
        uint32_t cancel = (CAN_TX_MAILBOX0 | CAN_TX_MAILBOX1 | CAN_TX_MAILBOX2) & ~keep;
        if (HAL_CAN_IsTxMessagePending(h, cancel) == 0U) { continue; }
        if (HAL_CAN_AbortTxRequest(h, cancel) != HAL_OK
            || HAL_CAN_IsTxMessagePending(h, cancel) != 0U) { complete = false; }
    }
    return complete;
}
bool BoardCanTx_ShoulderReadReady(void)
{ return listening(handles[0]) && pending_read() == 0U && HAL_CAN_GetTxMailboxesFreeLevel(handles[0]) == 3U; }
bool BoardCanTx_SubmitShoulderRead(const BoardCanFrame *f)
{
    /* This entry cannot carry an enable, write or torque command. */
    if (f == NULL || f->id != 0x024U || f->dlc != 8U || f->data[0] != 0U || f->data[3] != 0U
        || f->data[4] != 0U || f->data[5] != 0U || f->data[6] != 0U || f->data[7] != 0U
        || !BoardCanTx_ShoulderReadReady()) { return false; }
    return send_mailbox(0U, f, &shoulder_read_mailbox);
}
bool BoardCanTx_CancelShoulderRead(void)
{
    uint32_t mask = pending_read();
    if (mask == 0U) { return true; }
    HAL_CAN_StateTypeDef state = HAL_CAN_GetState(handles[0]);
    if (state != HAL_CAN_STATE_READY && state != HAL_CAN_STATE_LISTENING) { return false; }
    bool accepted = HAL_CAN_AbortTxRequest(handles[0], mask) == HAL_OK;
    return accepted && pending_read() == 0U;
}
bool BoardCanTx_Submit(const BoardCanFrame frames[2], bool stop)
{
    return BoardCanTx_SubmitAxes(frames, 3U, stop);
}
