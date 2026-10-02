/* L1: F446 bxCAN receive adapter. Reference: BetaRobot CANConfig/Rx callbacks
 * (see S2 research.md), ST CubeF4 V1.28.3 HAL. Adapted to two static queues;
 * no dynamic registration, device parsing, application TX, or ISR logging.
 */
#include "board_can.h"
#include "main.h"
#include "s2_config.h"
#include <stddef.h>
#include <string.h>

extern CAN_HandleTypeDef hcan1, hcan2; /* CubeMX owns these handles. */
typedef struct {
    BoardCanFrame frames[S2_CAN_QUEUE_CAPACITY];
    uint16_t head, tail, count;
    BoardCanStats stats;
} CanQueue;
static CanQueue queues[BOARD_CAN_COUNT];
static CAN_HandleTypeDef *const handles[BOARD_CAN_COUNT] = {&hcan1, &hcan2};
static const uint32_t fifos[BOARD_CAN_COUNT] = {CAN_RX_FIFO0, CAN_RX_FIFO1};
static BoardCanBus initializing = BOARD_CAN_COUNT;
static bool attempted;

static bool valid_bus(BoardCanBus bus) { return (unsigned)bus < BOARD_CAN_COUNT; }
static void increment(uint32_t *value) { if (*value != UINT32_MAX) { (*value)++; } }
static uint32_t lock(void)
{
    uint32_t saved = __get_PRIMASK();
    __disable_irq();
    return saved;
}

void BoardCan_InitBegin(BoardCanBus bus) { initializing = bus; }
void BoardCan_InitEnd(void) { initializing = BOARD_CAN_COUNT; }
bool BoardCan_HandleInitFailure(void)
{
    if (!valid_bus(initializing)) { return false; }
    queues[initializing].stats.start_error = BOARD_CAN_INIT_FAILED;
    return true;
}

static void record_error(BoardCanBus bus)
{
    CAN_HandleTypeDef *can = handles[bus];
    uint32_t code = HAL_CAN_GetError(can);
    uint32_t saved = lock();
    BoardCanStats *s = &queues[bus].stats;
    increment(&s->errors);
    s->last_error = code;
    uint32_t overrun = bus == BOARD_CAN_SHOULDER ? HAL_CAN_ERROR_RX_FOV0 : HAL_CAN_ERROR_RX_FOV1;
    if ((code & overrun) != 0U) { increment(&s->fifo_overruns); }
    /* HAL accumulates ErrorCode; reset after accounting, so an old overrun
     * is not counted again on every subsequent, unrelated error callback. */
    (void)HAL_CAN_ResetError(can);
    __set_PRIMASK(saved);
}

static bool start_bus(BoardCanBus bus)
{
    CanQueue *q = &queues[bus];
    CAN_HandleTypeDef *can = handles[bus];
    if (q->stats.start_error == BOARD_CAN_INIT_FAILED || HAL_CAN_GetState(can) != HAL_CAN_STATE_READY) {
        q->stats.start_error = BOARD_CAN_INIT_FAILED;
        q->stats.last_error = HAL_CAN_GetError(can);
        return false;
    }
    CAN_FilterTypeDef filter = {0};
    filter.FilterMode = CAN_FILTERMODE_IDMASK;
    filter.FilterScale = CAN_FILTERSCALE_32BIT;
    /* Standard DATA only: compare IDE and RTR, leave all eleven ID bits open. */
    filter.FilterMaskIdLow = CAN_RI0R_IDE | CAN_RI0R_RTR;
    filter.FilterActivation = ENABLE;
    filter.SlaveStartFilterBank = S2_CAN_FILTER_SPLIT;
    filter.FilterBank = bus == BOARD_CAN_SHOULDER ? 0U : S2_CAN_FILTER_SPLIT;
    filter.FilterFIFOAssignment = fifos[bus];
    if (HAL_CAN_ConfigFilter(can, &filter) != HAL_OK) {
        q->stats.start_error = BOARD_CAN_FILTER_FAILED;
        record_error(bus);
        return false;
    }
    if (HAL_CAN_Start(can) != HAL_OK) {
        q->stats.start_error = BOARD_CAN_START_FAILED;
        record_error(bus);
        return false;
    }
    uint32_t notifications = CAN_IT_ERROR | CAN_IT_ERROR_WARNING | CAN_IT_ERROR_PASSIVE
        | CAN_IT_BUSOFF | CAN_IT_LAST_ERROR_CODE;
    notifications |= bus == BOARD_CAN_SHOULDER
        ? CAN_IT_RX_FIFO0_MSG_PENDING | CAN_IT_RX_FIFO0_OVERRUN
        : CAN_IT_RX_FIFO1_MSG_PENDING | CAN_IT_RX_FIFO1_OVERRUN;
    q->stats.started = true; /* An IRQ may arrive as soon as notification is enabled. */
    if (HAL_CAN_ActivateNotification(can, notifications) != HAL_OK) {
        q->stats.started = false;
        q->stats.start_error = BOARD_CAN_NOTIFY_FAILED;
        record_error(bus);
        (void)HAL_CAN_DeactivateNotification(can, notifications);
        (void)HAL_CAN_Stop(can);
        return false;
    }
    return true;
}

bool BoardCan_Start(void)
{
    if (attempted) { return false; } /* Reset-only startup, no implicit recovery. */
    attempted = true;
    bool shoulder = start_bus(BOARD_CAN_SHOULDER);
    bool elbow = start_bus(BOARD_CAN_ELBOW); /* Still observe healthy bus on partial failure. */
    return shoulder && elbow;
}

bool BoardCan_Read(BoardCanBus bus, BoardCanFrame *out)
{
    if (!valid_bus(bus) || out == NULL) { return false; }
    uint32_t saved = lock();
    CanQueue *q = &queues[bus];
    bool available = q->count != 0U;
    if (available) {
        *out = q->frames[q->tail];
        q->tail = (uint16_t)((q->tail + 1U) % S2_CAN_QUEUE_CAPACITY);
        q->count--;
    }
    __set_PRIMASK(saved);
    return available;
}

bool BoardCan_GetStats(BoardCanBus bus, BoardCanStats *out)
{
    if (!valid_bus(bus) || out == NULL) { return false; }
    uint32_t saved = lock();
    *out = queues[bus].stats;
    out->queued = queues[bus].count;
    CAN_HandleTypeDef *can = handles[bus];
    if (can->Instance != NULL) {
        uint32_t timing = can->Instance->BTR;
        uint32_t divisor = ((timing & CAN_BTR_BRP) + 1U)
            * (3U + ((timing & CAN_BTR_TS1) >> CAN_BTR_TS1_Pos)
                  + ((timing & CAN_BTR_TS2) >> CAN_BTR_TS2_Pos));
        out->baudrate = HAL_RCC_GetPCLK1Freq() / divisor;
        out->bus_off = (can->Instance->ESR & CAN_ESR_BOFF) != 0U;
    }
    __set_PRIMASK(saved);
    return true;
}

static void receive(BoardCanBus bus)
{
    CAN_HandleTypeDef *can = handles[bus];
    CanQueue *q = &queues[bus];
    if (!q->stats.started) { return; }
    for (unsigned i = 0U; i < S2_CAN_ISR_BUDGET; i++) {
        if (HAL_CAN_GetRxFifoFillLevel(can, fifos[bus]) == 0U) { break; }
        CAN_RxHeaderTypeDef header;
        BoardCanFrame frame = {0};
        if (HAL_CAN_GetRxMessage(can, fifos[bus], &header, frame.data) != HAL_OK) {
            record_error(bus);
            break;
        }
        uint32_t saved = lock();
        if (header.IDE != CAN_ID_STD || header.RTR != CAN_RTR_DATA || header.DLC > 8U || header.StdId > 0x7FFU) {
            increment(&q->stats.invalid);
        } else {
            frame.id = (uint16_t)header.StdId;
            frame.dlc = (uint8_t)header.DLC;
            frame.received_ms = HAL_GetTick();
            increment(&q->stats.received);
            if (q->count == S2_CAN_QUEUE_CAPACITY) { increment(&q->stats.dropped); }
            else {
                q->frames[q->head] = frame;
                q->head = (uint16_t)((q->head + 1U) % S2_CAN_QUEUE_CAPACITY);
                q->count++;
            }
        }
        __set_PRIMASK(saved);
    }
}

void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *can)
{
    if (can == &hcan1) { receive(BOARD_CAN_SHOULDER); }
}
void HAL_CAN_RxFifo1MsgPendingCallback(CAN_HandleTypeDef *can)
{
    if (can == &hcan2) { receive(BOARD_CAN_ELBOW); }
}
void HAL_CAN_ErrorCallback(CAN_HandleTypeDef *can)
{
    if (can == &hcan1) { record_error(BOARD_CAN_SHOULDER); }
    else if (can == &hcan2) { record_error(BOARD_CAN_ELBOW); }
}
