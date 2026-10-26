/* L1: ST HAL abort/free-level/AddTxMessage, BetaRobot CANTransmit interface
 * adapted to bounded calls, no wait loop/queue/TX IRQ. RX code is untouched. */
#include "board_can_tx.h"
#include "main.h"
#include <stddef.h>

extern CAN_HandleTypeDef hcan1, hcan2;
static CAN_HandleTypeDef *const handles[2] = {&hcan1, &hcan2};

static uint32_t shoulder_read_mailbox;
static BoardCanReadTxResult shoulder_read_result;
static bool shoulder_read_abort_requested;
static uint32_t output_mailbox[2];
static bool output_sent[2];
static bool output_failed[2];
static uint32_t output_abort_mask[2];
static bool output_abort_pending[2];
static BoardCanFrame output_frame[2];
static bool output_resent[2];
static volatile uint32_t output_alst_resends[2]; /* SWD-readable bench evidence. */
static bool listening(CAN_HandleTypeDef *h);
static bool add_frame(unsigned bus, const BoardCanFrame *f, uint32_t *mailbox);
static uint32_t output_txok_bit(uint32_t mailbox)
{
    if (mailbox == CAN_TX_MAILBOX0) { return CAN_TSR_TXOK0; }
    if (mailbox == CAN_TX_MAILBOX1) { return CAN_TSR_TXOK1; }
    if (mailbox == CAN_TX_MAILBOX2) { return CAN_TSR_TXOK2; }
    return 0U;
}
static uint32_t output_rqcp_bit(uint32_t mailbox)
{
    if (mailbox == CAN_TX_MAILBOX0) { return CAN_TSR_RQCP0; }
    if (mailbox == CAN_TX_MAILBOX1) { return CAN_TSR_RQCP1; }
    if (mailbox == CAN_TX_MAILBOX2) { return CAN_TSR_RQCP2; }
    return 0U;
}
static void reconcile_output(unsigned bus)
{
    if (output_mailbox[bus] != 0U
        && HAL_CAN_IsTxMessagePending(handles[bus], output_mailbox[bus]) == 0U) {
        uint32_t rqcp = output_rqcp_bit(output_mailbox[bus]);
        uint32_t proof = rqcp | output_txok_bit(output_mailbox[bus]);
        uint32_t alst = rqcp << 2U, terr = rqcp << 3U; /* RQCPx/TXOKx/ALSTx/TERRx */
        uint32_t tsr = handles[bus]->Instance->TSR;
        output_sent[bus] = (tsr & proof) == proof;
        output_failed[bus] = !output_sent[bus];
        output_mailbox[bus] = 0U;
        /* Hardware auto-retransmission is on since 2026-10-22 (transient CAN2
         * bit error, TEC=7, latched TRANSPORT), so ALST/TERR normally retry in
         * hardware. Kept as a defensive path: ALST without TERR means the frame
         * never won the bus, so one same-frame resend is safe. */
        uint32_t mailbox;
        if (!output_sent[bus] && !output_resent[bus] && (tsr & rqcp) != 0U
            && (tsr & alst) != 0U && (tsr & terr) == 0U && listening(handles[bus])
            && HAL_CAN_GetTxMailboxesFreeLevel(handles[bus]) != 0U
            && add_frame(bus, &output_frame[bus], &mailbox)) {
            output_resent[bus] = true;
            output_alst_resends[bus]++;
            output_mailbox[bus] = mailbox;
            output_failed[bus] = false;
        }
    }
}
static uint32_t pending_read(void)
{
    HAL_CAN_StateTypeDef state = HAL_CAN_GetState(handles[0]);
    if (state != HAL_CAN_STATE_READY && state != HAL_CAN_STATE_LISTENING) { return shoulder_read_mailbox; }
    if (shoulder_read_mailbox != 0U
        && HAL_CAN_IsTxMessagePending(handles[0], shoulder_read_mailbox) == 0U) {
        uint32_t tsr = handles[0]->Instance->TSR;
        uint32_t rqcp = shoulder_read_mailbox == CAN_TX_MAILBOX0 ? CAN_TSR_RQCP0
            : shoulder_read_mailbox == CAN_TX_MAILBOX1 ? CAN_TSR_RQCP1 : CAN_TSR_RQCP2;
        uint32_t txok = output_txok_bit(shoulder_read_mailbox);
        uint32_t alst = shoulder_read_mailbox == CAN_TX_MAILBOX0 ? CAN_TSR_ALST0
            : shoulder_read_mailbox == CAN_TX_MAILBOX1 ? CAN_TSR_ALST1 : CAN_TSR_ALST2;
        uint32_t terr = shoulder_read_mailbox == CAN_TX_MAILBOX0 ? CAN_TSR_TERR0
            : shoulder_read_mailbox == CAN_TX_MAILBOX1 ? CAN_TSR_TERR1 : CAN_TSR_TERR2;
        shoulder_read_result = (tsr & rqcp) == 0U ? BOARD_CAN_READ_TX_FAILED
            : (tsr & txok) != 0U ? BOARD_CAN_READ_TX_SENT
            : (tsr & alst) != 0U && (tsr & terr) == 0U ? BOARD_CAN_READ_TX_ALST
            : BOARD_CAN_READ_TX_FAILED;
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
    reconcile_output(bus);
    if (!listening(handles[bus]) || HAL_CAN_GetTxMailboxesFreeLevel(handles[bus]) == 0U) {
        return false;
    }
    return add_frame(bus, f, mailbox);
}

static bool add_frame(unsigned bus, const BoardCanFrame *f, uint32_t *mailbox)
{
    CAN_HandleTypeDef *h = handles[bus];
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
        /* A read completed between preflight and this output reusing its
         * mailbox. Its result bits were overwritten; never infer TXOK/ALST. */
        shoulder_read_result = BOARD_CAN_READ_TX_FAILED;
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
        } else if (keep_read) {
            output_mailbox[i] = mailbox;
            output_sent[i] = false;
            output_failed[i] = false;
            output_frame[i] = frames[i];
            output_resent[i] = false;
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
        if (output_abort_pending[i]) {
            if (HAL_CAN_IsTxMessagePending(h, output_abort_mask[i]) != 0U) {
                complete = false;
                continue;
            }
            output_abort_pending[i] = false;
            output_abort_mask[i] = 0U;
            output_mailbox[i] = 0U;
            output_sent[i] = output_failed[i] = false;
        }
        reconcile_output(i);
        uint32_t keep = i == 0U ? pending_read() : 0U;
        uint32_t cancel = (CAN_TX_MAILBOX0 | CAN_TX_MAILBOX1 | CAN_TX_MAILBOX2) & ~keep;
        if (HAL_CAN_IsTxMessagePending(h, cancel) != 0U) {
            if (HAL_CAN_AbortTxRequest(h, cancel) != HAL_OK) { complete = false; continue; }
            if (HAL_CAN_IsTxMessagePending(h, cancel) != 0U) {
                output_abort_pending[i] = true;
                output_abort_mask[i] = cancel;
                complete = false;
                continue;
            }
        }
        if ((output_mailbox[i] & cancel) != 0U) {
            output_mailbox[i] = 0U;
        }
        /* A successful purge starts a new output epoch, including when the
         * previous mailbox had already completed before this call. */
        output_sent[i] = false;
        output_failed[i] = false;
    }
    return complete;
}
bool BoardCanTx_OutputPurgePending(uint8_t axes_mask)
{
    if (!axes_valid(axes_mask)) { return false; }
    for (unsigned i = 0U; i < 2U; i++) {
        if (axis_selected(axes_mask, i) && output_abort_pending[i]
            && listening(handles[i])) { return true; }
    }
    return false;
}
bool BoardCanTx_OutputAxesPending(uint8_t axes_mask)
{
    if (!axes_valid(axes_mask)) { return false; }
    for (unsigned i = 0U; i < 2U; i++) {
        if (!axis_selected(axes_mask, i)) { continue; }
        reconcile_output(i);
        if (output_mailbox[i] != 0U) { return true; }
    }
    return false;
}
bool BoardCanTx_ShoulderOutputDrained(void)
{ return BoardCanTx_OutputAxesDrained(1U); }
bool BoardCanTx_ShoulderOutputFailed(void)
{ return BoardCanTx_OutputAxesFailed(1U); }
bool BoardCanTx_OutputAxesDrained(uint8_t axes_mask)
{
    if (!axes_valid(axes_mask)) { return false; }
    for (unsigned i = 0U; i < 2U; i++) {
        if (!axis_selected(axes_mask, i)) { continue; }
        if (!listening(handles[i])) { return false; }
        reconcile_output(i);
        if (output_mailbox[i] != 0U || !output_sent[i]) { return false; }
    }
    return true;
}
bool BoardCanTx_OutputAxesFailed(uint8_t axes_mask)
{
    if (!axes_valid(axes_mask)) { return true; }
    for (unsigned i = 0U; i < 2U; i++) {
        if (!axis_selected(axes_mask, i)) { continue; }
        if (!listening(handles[i])) { return true; }
        reconcile_output(i);
        if (output_mailbox[i] == 0U && output_failed[i]) { return true; }
    }
    return false;
}
bool BoardCanTx_ShoulderReadReady(void)
{ return listening(handles[0]) && pending_read() == 0U && HAL_CAN_GetTxMailboxesFreeLevel(handles[0]) == 3U; }
BoardCanReadTxResult BoardCanTx_ShoulderReadResult(void)
{ (void)pending_read(); return shoulder_read_result; }
bool BoardCanTx_SubmitShoulderRead(const BoardCanFrame *f)
{
    /* This entry cannot carry an enable, write or torque command. */
    if (f == NULL || f->id != 0x024U || f->dlc != 8U || f->data[0] != 0U || f->data[3] != 0U
        || f->data[4] != 0U || f->data[5] != 0U || f->data[6] != 0U || f->data[7] != 0U
        || !BoardCanTx_ShoulderReadReady()) { return false; }
    if (!send_mailbox(0U, f, &shoulder_read_mailbox)) { return false; }
    shoulder_read_result = BOARD_CAN_READ_TX_PENDING;
    shoulder_read_abort_requested = false;
    return true;
}
BoardCanReadCancelResult BoardCanTx_CancelShoulderReadStep(void)
{
    uint32_t mask = pending_read();
    shoulder_read_result = BOARD_CAN_READ_TX_NONE;
    if (mask == 0U) {
        shoulder_read_abort_requested = false;
        return BOARD_CAN_READ_CANCEL_DONE;
    }
    HAL_CAN_StateTypeDef state = HAL_CAN_GetState(handles[0]);
    if (state != HAL_CAN_STATE_READY && state != HAL_CAN_STATE_LISTENING) {
        return BOARD_CAN_READ_CANCEL_FAILED;
    }
    if (!shoulder_read_abort_requested) {
        if (HAL_CAN_AbortTxRequest(handles[0], mask) != HAL_OK) {
            return BOARD_CAN_READ_CANCEL_FAILED;
        }
        shoulder_read_abort_requested = true;
    }
    if (pending_read() != 0U) { return BOARD_CAN_READ_CANCEL_PENDING; }
    shoulder_read_abort_requested = false;
    shoulder_read_result = BOARD_CAN_READ_TX_NONE;
    return BOARD_CAN_READ_CANCEL_DONE;
}
bool BoardCanTx_CancelShoulderRead(void)
{ return BoardCanTx_CancelShoulderReadStep() == BOARD_CAN_READ_CANCEL_DONE; }
bool BoardCanTx_Submit(const BoardCanFrame frames[2], bool stop)
{
    return BoardCanTx_SubmitAxes(frames, 3U, stop);
}
