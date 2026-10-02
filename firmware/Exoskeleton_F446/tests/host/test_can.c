#include "can_mock.h"
#include "can_monitor.h"
#include <assert.h>
#include <string.h>

static void inject(BoardCanBus bus, uint8_t value)
{
    TestCan_Inject(bus, 0x201, 8, CAN_ID_STD, CAN_RTR_DATA, value);
    TestCan_Interrupt(bus);
}
static void startup(void)
{
    assert(BoardCan_Start());
    assert(!BoardCan_Start());
    BoardCanStats s;
    assert(BoardCan_GetStats(BOARD_CAN_SHOULDER, &s));
    assert(s.started && s.baudrate == 500000 && !s.errors && !s.received);
    assert(BoardCan_GetStats(BOARD_CAN_ELBOW, &s));
    assert(s.started && s.baudrate == 1000000);
    assert(!BoardCan_GetStats(BOARD_CAN_COUNT, &s));
    assert(!BoardCan_GetStats(BOARD_CAN_SHOULDER, NULL));
    assert(!BoardCan_Read(BOARD_CAN_ELBOW, NULL));
    assert(!BoardCan_Read((BoardCanBus)-1, &(BoardCanFrame){0}));
}
static void isolation(void)
{
    assert(BoardCan_Start());
    TestClock_Set(123U); inject(BOARD_CAN_SHOULDER, 0xAB);
    TestClock_Set(456U); inject(BOARD_CAN_ELBOW, 0xCD);
    BoardCanFrame f;
    assert(BoardCan_Read(BOARD_CAN_ELBOW, &f));
    assert(f.data[0] == 0xCD && f.data[7] == 0xCD && f.received_ms == 456U);
    assert(BoardCan_Read(BOARD_CAN_SHOULDER, &f));
    assert(f.data[0] == 0xAB && f.id == 0x201 && f.dlc == 8 && f.received_ms == 123U);
    assert(!BoardCan_Read(BOARD_CAN_ELBOW, &f));
    __disable_irq(); inject(BOARD_CAN_SHOULDER, 1);
    assert(BoardCan_Read(BOARD_CAN_SHOULDER, &f));
    assert(__get_PRIMASK() == 1); __set_PRIMASK(0);
}
static void queue(void)
{
    assert(BoardCan_Start());
    for (unsigned round = 0; round < 3; round++) {
        for (unsigned i = 0; i < 65; i++) { inject(BOARD_CAN_SHOULDER, (uint8_t)i); }
        BoardCanStats s; BoardCanFrame f;
        assert(BoardCan_GetStats(BOARD_CAN_SHOULDER, &s));
        assert(s.queued == 64 && s.dropped == round + 1);
        for (unsigned i = 0; i < 64; i++) {
            assert(BoardCan_Read(BOARD_CAN_SHOULDER, &f)); assert(f.data[0] == i);
        }
        assert(!BoardCan_Read(BOARD_CAN_SHOULDER, &f));
    }
}
static void budget(void)
{
    CanMonitor_Init();
    for (unsigned i = 0; i < 5; i++) { TestCan_Inject(BOARD_CAN_ELBOW, 1, 8, 0, 0, (uint8_t)i); }
    TestCan_Interrupt(BOARD_CAN_ELBOW);
    BoardCanStats s;
    assert(BoardCan_GetStats(BOARD_CAN_ELBOW, &s)); assert(s.received == 3);
    assert(HAL_CAN_GetRxFifoFillLevel(&hcan2, 1) == 2);
    TestCan_Interrupt(BOARD_CAN_ELBOW);
    for (unsigned i = 0; i < 15; i++) { inject(BOARD_CAN_ELBOW, 9); }
    CanMonitor_Poll();
    assert(BoardCan_GetStats(BOARD_CAN_ELBOW, &s)); assert(s.queued == 4);
    CanMonitor_Poll();
    assert(BoardCan_GetStats(BOARD_CAN_ELBOW, &s)); assert(s.queued == 0);
}
static void invalid(void)
{
    CanMonitor_Init();
    TestCan_Inject(BOARD_CAN_SHOULDER, 1, 9, 0, 0, 0);
    TestCan_Inject(BOARD_CAN_SHOULDER, 1, 8, CAN_ID_EXT, 0, 0);
    TestCan_Inject(BOARD_CAN_SHOULDER, 1, 8, 0, CAN_RTR_REMOTE, 0);
    TestCan_Interrupt(BOARD_CAN_SHOULDER);
    TestCan_Inject(BOARD_CAN_SHOULDER, 0x800, 8, 0, 0, 0);
    TestCan_Interrupt(BOARD_CAN_SHOULDER);
    BoardCanFrame f; CanMonitor_Poll();
    assert(!CanMonitor_GetLast(BOARD_CAN_SHOULDER, &f));
    BoardCanStats s; assert(BoardCan_GetStats(BOARD_CAN_SHOULDER, &s));
    assert(s.invalid == 4 && s.received == 0 && s.queued == 0);
    TestCan_Inject(BOARD_CAN_SHOULDER, 0x7FF, 0, 0, 0, 0); TestCan_Interrupt(BOARD_CAN_SHOULDER);
    assert(BoardCan_Read(BOARD_CAN_SHOULDER, &f)); assert(f.id == 0x7FF && f.dlc == 0);
}
static void failure(const char *op, BoardCanStartError expected)
{
    TestCan_Fail(BOARD_CAN_SHOULDER, op);
    assert(!BoardCan_Start());
    BoardCanStats a, b;
    assert(BoardCan_GetStats(BOARD_CAN_SHOULDER, &a));
    assert(BoardCan_GetStats(BOARD_CAN_ELBOW, &b));
    assert(!a.started && a.start_error == expected && a.errors == 1);
    assert(a.last_error == HAL_CAN_ERROR_TIMEOUT);
    assert(b.started && b.start_error == BOARD_CAN_START_OK);
    assert(TestCan_Notifications(BOARD_CAN_SHOULDER) == 0);
    inject(BOARD_CAN_ELBOW, 55); BoardCanFrame f;
    assert(BoardCan_Read(BOARD_CAN_ELBOW, &f) && f.data[0] == 55);
}
static void init_failure(void)
{
    assert(!BoardCan_HandleInitFailure());
    BoardCan_InitBegin(BOARD_CAN_ELBOW);
    assert(BoardCan_HandleInitFailure());
    BoardCan_InitEnd(); assert(!BoardCan_HandleInitFailure());
    assert(!BoardCan_Start());
    BoardCanStats s; assert(BoardCan_GetStats(BOARD_CAN_ELBOW, &s));
    assert(!s.started && s.start_error == BOARD_CAN_INIT_FAILED);
    assert(BoardCan_GetStats(BOARD_CAN_SHOULDER, &s) && s.started);
}
static void errors(void)
{
    assert(BoardCan_Start());
    hcan1.ErrorCode = HAL_CAN_ERROR_RX_FOV0; HAL_CAN_ErrorCallback(&hcan1);
    assert(hcan1.ErrorCode == 0);
    hcan1.ErrorCode = HAL_CAN_ERROR_ACK; HAL_CAN_ErrorCallback(&hcan1);
    BoardCanStats s; assert(BoardCan_GetStats(BOARD_CAN_SHOULDER, &s));
    assert(s.fifo_overruns == 1 && s.errors == 2 && s.last_error == HAL_CAN_ERROR_ACK);
    assert(BoardCan_GetStats(BOARD_CAN_ELBOW, &s)); assert(s.errors == 0);
    hcan2.Instance->ESR |= CAN_ESR_BOFF; hcan2.ErrorCode = HAL_CAN_ERROR_BOF;
    HAL_CAN_ErrorCallback(&hcan2);
    assert(BoardCan_GetStats(BOARD_CAN_ELBOW, &s)); assert(s.bus_off && s.errors == 1);
    TestCan_Fail(BOARD_CAN_SHOULDER, "receive"); inject(BOARD_CAN_SHOULDER, 0);
    assert(BoardCan_GetStats(BOARD_CAN_SHOULDER, &s)); assert(s.errors == 3 && s.received == 0);
}
void TestCan_Run(const char *name)
{
    if (!strcmp(name, "can_startup")) { startup(); }
    else if (!strcmp(name, "can_isolation")) { isolation(); }
    else if (!strcmp(name, "can_queue")) { queue(); }
    else if (!strcmp(name, "can_budget")) { budget(); }
    else if (!strcmp(name, "can_invalid")) { invalid(); }
    else if (!strcmp(name, "can_filter_failure")) { failure("filter", BOARD_CAN_FILTER_FAILED); }
    else if (!strcmp(name, "can_start_failure")) { failure("start", BOARD_CAN_START_FAILED); }
    else if (!strcmp(name, "can_notify_failure")) { failure("notify", BOARD_CAN_NOTIFY_FAILED); }
    else if (!strcmp(name, "can_init_failure")) { init_failure(); }
    else if (!strcmp(name, "can_errors")) { errors(); }
    else { assert(0); }
}
