#include "main.h"
#include "recorder.h"
#include "clip.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

CAN_TypeDef mock_can1, mock_can2;
RCC_TypeDef mock_rcc;
SysTick_Type mock_systick;
uint32_t mock_mask, mock_tick;
static char reply[256];
static bool reject_write;
static unsigned irq_calls, tx_calls, rx_calls, abort_calls, reset_calls, clip_calls;
static HAL_StatusTypeDef tx_result = HAL_OK;
void __wrap_HAL_CAN_IRQHandler(CAN_HandleTypeDef *h);
HAL_StatusTypeDef __wrap_HAL_CAN_GetRxMessage(CAN_HandleTypeDef *, uint32_t, CAN_RxHeaderTypeDef *, uint8_t *);
HAL_StatusTypeDef __wrap_HAL_CAN_AddTxMessage(CAN_HandleTypeDef *, const CAN_TxHeaderTypeDef *, const uint8_t *, uint32_t *);
HAL_StatusTypeDef __wrap_HAL_CAN_AbortTxRequest(CAN_HandleTypeDef *, uint32_t);
HAL_StatusTypeDef __wrap_HAL_CAN_ResetError(CAN_HandleTypeDef *);
void __wrap_clip_cmd_parse_line(const struct clip *, const struct clip_command *, char *, void *);
bool BoardConsole_Write(const uint8_t *data, size_t size)
{
    assert(size < sizeof(reply));
    if (reject_write) { return false; }
    memcpy(reply, data, size); reply[size] = '\0'; return true;
}
void __real_HAL_CAN_IRQHandler(CAN_HandleTypeDef *h)
{ ++irq_calls; h->Instance->ESR = 0; h->Instance->MSR = 0; }
HAL_StatusTypeDef __real_HAL_CAN_GetRxMessage(CAN_HandleTypeDef *h, uint32_t fifo, CAN_RxHeaderTypeDef *hdr, uint8_t *data)
{ (void)h; (void)fifo; ++rx_calls; hdr->DLC = 8; hdr->StdId = 0x29; memset(data, 0xA5, 8); return HAL_OK; }
HAL_StatusTypeDef __real_HAL_CAN_AddTxMessage(CAN_HandleTypeDef *h, const CAN_TxHeaderTypeDef *hdr, const uint8_t *data, uint32_t *mb)
{ (void)h; (void)hdr; (void)data; ++tx_calls; if (tx_result == HAL_OK) { *mb = 2; } return tx_result; }
HAL_StatusTypeDef __real_HAL_CAN_AbortTxRequest(CAN_HandleTypeDef *h, uint32_t mask)
{ (void)h; assert(mask == 2); ++abort_calls; return HAL_OK; }
HAL_StatusTypeDef __real_HAL_CAN_ResetError(CAN_HandleTypeDef *h)
{ ++reset_calls; h->error = 0; return HAL_OK; }
void __real_clip_cmd_parse_line(const struct clip *c, const struct clip_command *cmd, char *line, void *context)
{ (void)c; (void)cmd; (void)line; (void)context; ++clip_calls; }
static void command(const char *text)
{ char line[64]; strcpy(line, text); __wrap_clip_cmd_parse_line(NULL, NULL, line, NULL); }
static void ring_test(void)
{
    BbRecorder r = {0}; BbEvent e = {0}, out;
    assert(!Bb_Read(&r, 0, &out));
    for (unsigned i = 0; i < 1000; ++i) { e.ms = i; Bb_Push(&r, &e, false); }
    e.ms = 1000; Bb_Push(&r, &e, true);
    assert(r.trigger_seq == 1000 && r.post_left == 16);
    for (unsigned i = 0; i < 16; ++i) { ++e.ms; Bb_Push(&r, &e, true); }
    assert(r.frozen && r.count == 128 && r.trigger_seq == 1000);
    assert(r.overwritten == 889);
    for (uint16_t i = 0; i < 128; ++i) { assert(Bb_Read(&r, i, &out)); assert(out.seq == 889U+i && out.ms == 889U+i); }
    Bb_Push(&r, &e, false); assert(r.suppressed == 1);
    assert(!Bb_Read(&r, 128, &out));
    BbRecorder manual = {0}; Bb_Freeze(&manual); Bb_Push(&manual, &e, true);
    assert(manual.count == 0 && !manual.triggered && manual.suppressed == 1);
}
int main(void)
{
    ring_test();
    CAN_HandleTypeDef h = {CAN1, 0}, other = {&mock_can2, 0};
    command("bbnext"); assert(strstr(reply, "freeze_first"));
    __wrap_HAL_CAN_IRQHandler(&other); assert(irq_calls == 1);
    command("bbstatus"); assert(strstr(reply, "count=0"));
    CAN_TxHeaderTypeDef hdr = {0x24, 0, 0, 0, 8}; uint8_t data[8] = {0}; uint32_t mailbox = 0;
    assert(__wrap_HAL_CAN_AddTxMessage(&h, &hdr, data, &mailbox) == HAL_OK && mailbox == 2 && tx_calls == 1);
    CAN_RxHeaderTypeDef rx = {0}; assert(__wrap_HAL_CAN_GetRxMessage(&h, 0, &rx, data) == HAL_OK && rx_calls == 1);
    mock_can1.ESR = 0x00030020; mock_can1.MSR = 4; mock_can1.BTR = 0x001A0005;
    mock_mask = 1; __wrap_HAL_CAN_IRQHandler(&h);
    assert(mock_mask == 1 && mock_can1.ESR == 0 && irq_calls == 2); mock_mask = 0;
    h.error = 0x10; assert(__wrap_HAL_CAN_ResetError(&h) == HAL_OK && h.error == 0 && reset_calls == 1);
    tx_result = HAL_ERROR; mailbox = 0xDEADBEEF;
    assert(__wrap_HAL_CAN_AddTxMessage(&h, &hdr, data, &mailbox) == HAL_ERROR && mailbox == 0xDEADBEEF && tx_calls == 2);
    assert(__wrap_HAL_CAN_AbortTxRequest(&h, 2) == HAL_OK && abort_calls == 1);
    command("ping"); assert(clip_calls == 1);
    command("bbfreeze"); command("bbrewind");
    command("bbstatus"); assert(strstr(reply, "frozen=1") && strstr(reply, "trigger_seq=3"));
    reject_write = true; command("bbnext"); reject_write = false;
    command("bbnext"); assert(strstr(reply, "i=0 seq=0") && strstr(reply, "kind=3"));
    command("bbnext"); assert(strstr(reply, "kind=4") && strstr(reply, "aux=00000002"));
    command("bbnext"); assert(strstr(reply, "kind=2") && strstr(reply, "data=A5A5A5A5A5A5A5A5"));
    command("bbnext"); assert(strstr(reply, "kind=1") && strstr(reply, "esr=00030020"));
    for (unsigned i = 0; i < 3; ++i) { command("bbnext"); assert(strstr(reply, "kind=8")); }
    command("bbnext"); assert(strstr(reply, "kind=5") && strstr(reply, "aux=00000010"));
    command("bbnext"); command("bbnext"); assert(strstr(reply, "status=1") && strstr(reply, "aux=00000000"));
    command("bbconfig"); assert(strstr(reply, "config=1") && strstr(reply, "btr=001A0005"));
    assert(mock_mask == 0);
    puts("PASS ring wrap/first-fault/freeze; pre-HAL capture; forwarding; CAN2 isolation; UART retry; PRIMASK restore");
    return 0;
}
