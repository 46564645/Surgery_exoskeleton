/* Temporary GNU-ld wrappers. Each real operation is called exactly once.
 * Sources/limitations/removal: README.md in this directory. CAN1 only. */
#include "main.h"
#include "board_console.h"
#include "s1_config.h"
#include "clip.h"
#include "recorder.h"
#include <stdio.h>
#include <string.h>

static BbRecorder recorder;
static uint16_t cursor;
static struct {
    uint32_t mcr, btr, ier, rcc_cr, pll, cfgr, systick_load;
    bool valid;
} config;
_Static_assert(sizeof(recorder) + sizeof(config) < 6144U, "Blackbox RAM budget");

void __real_HAL_CAN_IRQHandler(CAN_HandleTypeDef *h);
HAL_StatusTypeDef __real_HAL_CAN_GetRxMessage(CAN_HandleTypeDef *h, uint32_t fifo, CAN_RxHeaderTypeDef *header, uint8_t *data);
HAL_StatusTypeDef __real_HAL_CAN_AddTxMessage(CAN_HandleTypeDef *h, const CAN_TxHeaderTypeDef *header, const uint8_t *data, uint32_t *mailbox);
HAL_StatusTypeDef __real_HAL_CAN_AbortTxRequest(CAN_HandleTypeDef *h, uint32_t mailboxes);
HAL_StatusTypeDef __real_HAL_CAN_ResetError(CAN_HandleTypeDef *h);
void __real_clip_cmd_parse_line(const struct clip *self, const struct clip_command *cmd, char *line, void *context);

static uint32_t lock(void)
{ uint32_t saved = __get_PRIMASK(); __disable_irq(); return saved; }
static bool shoulder(const CAN_HandleTypeDef *h) { return h != NULL && h->Instance == CAN1; }
static bool hardware_fault(uint32_t esr, uint32_t msr)
{
    uint32_t lec = (esr & CAN_ESR_LEC) >> CAN_ESR_LEC_Pos;
    return (lec >= 1U && lec <= 6U) || (esr & 7U) != 0U || (msr & CAN_MSR_ERRI) != 0U;
}
static BbEvent snapshot(CAN_HandleTypeDef *h, BbKind kind)
{
    BbEvent e = {0};
    e.ms = HAL_GetTick(); e.esr = h->Instance->ESR; e.tsr = h->Instance->TSR;
    e.kind = (uint8_t)kind;
    return e;
}
static void save(BbEvent *e, bool fault)
{
    uint32_t saved = lock();
    Bb_Push(&recorder, e, fault);
    __set_PRIMASK(saved);
}
void __wrap_HAL_CAN_IRQHandler(CAN_HandleTypeDef *h)
{
    if (shoulder(h)) {
        /* Snapshot BEFORE HAL clears LEC/TSR. These are sequential reads,
         * not an atomic hardware capture and not a copy of a rejected frame. */
        uint32_t saved = lock();
        BbEvent e = snapshot(h, BB_IRQ);
        e.aux = h->Instance->MSR;
        if (!config.valid) {
            config.mcr = h->Instance->MCR; config.btr = h->Instance->BTR;
            config.ier = h->Instance->IER; config.rcc_cr = RCC->CR;
            config.pll = RCC->PLLCFGR; config.cfgr = RCC->CFGR;
            config.systick_load = SysTick->LOAD; config.valid = true;
        }
        bool fault = hardware_fault(e.esr, e.aux);
        bool first = fault && !recorder.triggered && !recorder.frozen;
        Bb_Push(&recorder, &e, fault);
        if (first) {
            for (unsigned i = 0U; i < 3U; ++i) {
                BbEvent m = snapshot(h, BB_MAILBOX);
                m.aux = h->Instance->sTxMailBox[i].TDTR;
                m.id = h->Instance->sTxMailBox[i].TIR; /* raw, including TXRQ/IDE/RTR */
                uint32_t lo = h->Instance->sTxMailBox[i].TDLR;
                uint32_t hi = h->Instance->sTxMailBox[i].TDHR;
                memcpy(m.data, &lo, 4U); memcpy(m.data + 4U, &hi, 4U);
                m.status = (uint8_t)i; m.dlc = 8U;
                Bb_Push(&recorder, &m, false);
            }
        }
        __set_PRIMASK(saved);
    }
    __real_HAL_CAN_IRQHandler(h);
}
HAL_StatusTypeDef __wrap_HAL_CAN_GetRxMessage(CAN_HandleTypeDef *h, uint32_t fifo, CAN_RxHeaderTypeDef *header, uint8_t *data)
{
    HAL_StatusTypeDef result = __real_HAL_CAN_GetRxMessage(h, fifo, header, data);
    if (shoulder(h) && result == HAL_OK) {
        BbEvent e = snapshot(h, BB_RX);
        e.id = header->IDE == CAN_ID_STD ? header->StdId : header->ExtId;
        e.dlc = (uint8_t)header->DLC; e.flags = (uint8_t)(header->IDE | header->RTR);
        e.aux = fifo;
        if (header->RTR == CAN_RTR_DATA) { memcpy(e.data, data, header->DLC > 8U ? 8U : header->DLC); }
        save(&e, false);
    }
    return result;
}
HAL_StatusTypeDef __wrap_HAL_CAN_AddTxMessage(CAN_HandleTypeDef *h, const CAN_TxHeaderTypeDef *header, const uint8_t *data, uint32_t *mailbox)
{
    BbEvent e = {0};
    if (shoulder(h)) {
        e = snapshot(h, BB_TX_BEGIN);
        e.id = header->IDE == CAN_ID_STD ? header->StdId : header->ExtId;
        e.dlc = (uint8_t)header->DLC; e.flags = (uint8_t)(header->IDE | header->RTR);
        if (header->RTR == CAN_RTR_DATA) { memcpy(e.data, data, header->DLC > 8U ? 8U : header->DLC); }
        save(&e, false);
    }
    HAL_StatusTypeDef result = __real_HAL_CAN_AddTxMessage(h, header, data, mailbox);
    if (shoulder(h)) {
        e.kind = BB_TX_RESULT; e.ms = HAL_GetTick(); e.esr = h->Instance->ESR; e.tsr = h->Instance->TSR;
        e.status = (uint8_t)result; e.aux = result == HAL_OK ? *mailbox : 0U;
        save(&e, result != HAL_OK);
    }
    return result;
}
HAL_StatusTypeDef __wrap_HAL_CAN_AbortTxRequest(CAN_HandleTypeDef *h, uint32_t mailboxes)
{
    if (shoulder(h)) { BbEvent e = snapshot(h, BB_ABORT_BEGIN); e.aux = mailboxes; save(&e, false); }
    HAL_StatusTypeDef result = __real_HAL_CAN_AbortTxRequest(h, mailboxes);
    if (shoulder(h)) {
        BbEvent e = snapshot(h, BB_ABORT_RESULT); e.aux = mailboxes; e.status = (uint8_t)result;
        save(&e, result != HAL_OK);
    }
    return result;
}
HAL_StatusTypeDef __wrap_HAL_CAN_ResetError(CAN_HandleTypeDef *h)
{
    if (shoulder(h)) {
        BbEvent e = snapshot(h, BB_HAL_ERROR); e.aux = HAL_CAN_GetError(h);
        save(&e, e.aux != HAL_CAN_ERROR_NONE);
    }
    return __real_HAL_CAN_ResetError(h);
}

static bool command(const char *line)
{
    char out[S1_REPLY_SIZE]; int n;
    if (strcmp(line, "bbfreeze") == 0) {
        uint32_t saved = lock(); Bb_Freeze(&recorder); __set_PRIMASK(saved);
        n = snprintf(out, sizeof(out), "OK bb frozen=1\r\n");
    } else if (strcmp(line, "bbrewind") == 0) {
        cursor = 0U; n = snprintf(out, sizeof(out), "OK bb cursor=0\r\n");
    } else if (strcmp(line, "bbstatus") == 0) {
        uint32_t saved = lock();
        unsigned count = recorder.count, frozen = recorder.frozen, triggered = recorder.triggered, post = recorder.post_left;
        uint32_t overwritten = recorder.overwritten, suppressed = recorder.suppressed, trigger = recorder.trigger_seq;
        __set_PRIMASK(saved);
        n = snprintf(out, sizeof(out), "OK bb v=1 bus=1 count=%u frozen=%u triggered=%u trigger_seq=%lu post=%u overwritten=%lu suppressed=%lu cursor=%u\r\n",
            count, frozen, triggered, (unsigned long)trigger, post, (unsigned long)overwritten, (unsigned long)suppressed, (unsigned)cursor);
    } else if (strcmp(line, "bbconfig") == 0) {
        uint32_t saved = lock();
        uint32_t mcr = config.mcr, btr = config.btr, ier = config.ier, cr = config.rcc_cr,
                 pll = config.pll, cfgr = config.cfgr, load = config.systick_load;
        bool valid = config.valid;
        __set_PRIMASK(saved);
        n = snprintf(out, sizeof(out), "OK bb config=%u source=first_irq mcr=%08lX btr=%08lX ier=%08lX cr=%08lX pll=%08lX cfgr=%08lX systick_load=%lu\r\n",
            (unsigned)valid, (unsigned long)mcr, (unsigned long)btr, (unsigned long)ier,
            (unsigned long)cr, (unsigned long)pll, (unsigned long)cfgr, (unsigned long)load);
    } else if (strcmp(line, "bbnext") == 0) {
        BbEvent e; uint32_t saved = lock();
        bool frozen = recorder.frozen, available = Bb_Read(&recorder, cursor, &e);
        __set_PRIMASK(saved);
        if (!frozen) { n = snprintf(out, sizeof(out), "ERR bb freeze_first\r\n"); }
        else if (!available) { n = snprintf(out, sizeof(out), "OK bb end=1\r\n"); }
        else {
            char hex[17]; static const char digits[] = "0123456789ABCDEF";
            for (unsigned i = 0U; i < 8U; ++i) { hex[2U*i] = digits[e.data[i] >> 4]; hex[2U*i+1U] = digits[e.data[i] & 15U]; }
            hex[16] = '\0';
            n = snprintf(out, sizeof(out), "OK bb i=%u seq=%lu ms=%lu kind=%u esr=%08lX tsr=%08lX aux=%08lX id=%08lX dlc=%u flags=%u status=%u data=%s\r\n",
                (unsigned)cursor, (unsigned long)e.seq, (unsigned long)e.ms, (unsigned)e.kind,
                (unsigned long)e.esr, (unsigned long)e.tsr, (unsigned long)e.aux, (unsigned long)e.id,
                (unsigned)e.dlc, (unsigned)e.flags, (unsigned)e.status, hex);
            if (n > 0 && (size_t)n < sizeof(out) && BoardConsole_Write((const uint8_t *)out, (size_t)n)) { ++cursor; }
            return true; /* Retry same index if UART rejected this line. */
        }
    } else { return false; }
    if (n > 0 && (size_t)n < sizeof(out)) { (void)BoardConsole_Write((const uint8_t *)out, (size_t)n); }
    return true;
}
void __wrap_clip_cmd_parse_line(const struct clip *self, const struct clip_command *cmd, char *line, void *context)
{
    /* Console dispatch already enforces no args and reserves one reply slot.
     * No command here sends CAN, clears HAL errors or changes output permission. */
    if (cmd == NULL && command(line)) { return; }
    __real_clip_cmd_parse_line(self, cmd, line, context);
}
