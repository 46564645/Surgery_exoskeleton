/* Behavioral protocol/freshness tests use real L2 and L4 modules, mock L1 HAL. */
#include "c620_feedback.h"
#include "can_monitor.h"
#include "can_mock.h"
#include "test_c620.h"
#include "s2_config.h"
#include <assert.h>
#include <math.h>
#include <string.h>

static BoardCanFrame frame(uint16_t angle, int16_t rpm, int16_t current, uint32_t stamp)
{
    uint16_t speed = (uint16_t)rpm, amps = (uint16_t)current;
    BoardCanFrame f = {0}; f.id = 0x201; f.dlc = 8; f.received_ms = stamp;
    f.data[0] = (uint8_t)(angle >> 8); f.data[1] = (uint8_t)angle;
    f.data[2] = (uint8_t)(speed >> 8); f.data[3] = (uint8_t)speed;
    f.data[4] = (uint8_t)(amps >> 8); f.data[5] = (uint8_t)amps;
    f.data[6] = 30;
    return f;
}
static C620Feedback get(C620Device *d, uint32_t now)
{
    C620Feedback s; assert(C620_GetFeedback(d, now, &s)); return s;
}
static void codec(void)
{
    C620Device d; assert(C620_Init(&d, 1, 100));
    /* First real payload captured at 2026-10-01 15:16:34 UTC. */
    BoardCanFrame f = {.received_ms = 1000, .id = 0x201, .dlc = 8,
                      .data = {0x0C,0x13,0,0,0xFF,0xFC,0x1E,0}};
    assert(C620_Receive(&d, BOARD_CAN_ELBOW, &f, 1001));
    C620Feedback s = get(&d, 1001);
    assert(s.has_sample && s.valid && s.received_ms == 1000 && s.age_ms == 1);
    assert(s.rotor_angle_ticks == 3091 && s.rotor_speed_rpm == 0);
    assert(s.torque_current_raw == -4 && s.temperature_c == 30);
    assert(fabs((double)s.rotor_position_rad - 2.370767308) < 0.000001);
    assert(s.rotor_velocity_rad_s == 0.0f);
    /* Independently specified wire values verify big endian and both signs. */
    f = frame(258, 60, 28, 1010); f.data[7] = 0xA5;
    assert(C620_Receive(&d, BOARD_CAN_ELBOW, &f, 1010)); s = get(&d, 1010);
    assert(s.rotor_angle_ticks == 258 && s.reserved_raw == 0xA5);
    assert(fabs((double)s.rotor_velocity_rad_s - 6.283185307) < 0.00001);
    f = frame(4096, -60, -51, 1020);
    assert(C620_Receive(&d, BOARD_CAN_ELBOW, &f, 1020)); s = get(&d, 1020);
    assert(fabs((double)s.rotor_position_rad - 3.141592654) < 0.000001);
    assert(fabs((double)s.rotor_velocity_rad_s + 6.283185307) < 0.00001);
    assert(s.torque_current_raw == -51);
    f = frame(8191, INT16_MIN, INT16_MIN, 1030); f.data[6] = 255;
    assert(C620_Receive(&d, BOARD_CAN_ELBOW, &f, 1030)); s = get(&d, 1030);
    assert(s.rotor_angle_ticks == 8191 && s.rotor_position_rad < 6.283185307f);
    assert(s.rotor_speed_rpm == INT16_MIN && s.torque_current_raw == INT16_MIN);
    assert(fabs((double)s.rotor_velocity_rad_s + 3431.456937) < 0.001);
    f = frame(0, INT16_MAX, INT16_MAX, 1040);
    assert(C620_Receive(&d, BOARD_CAN_ELBOW, &f, 1040)); s = get(&d, 1040);
    assert(s.rotor_position_rad == 0.0f && s.rotor_speed_rpm == INT16_MAX);
    assert(s.torque_current_raw == INT16_MAX);
    f = frame(0, 0, 0, 1050); memset(f.data, 0, sizeof(f.data));
    assert(C620_Receive(&d, BOARD_CAN_ELBOW, &f, 1050)); s = get(&d, 1050);
    assert(s.valid && s.rotor_angle_ticks == 0 && s.temperature_c == 0);
}
static void invalid(void)
{
    C620Device d; assert(C620_Init(&d, 1, 100));
    BoardCanFrame good = frame(3091, -60, -4, 10);
    assert(C620_Receive(&d, BOARD_CAN_ELBOW, &good, 10));
    const uint16_t wrong_ids[] = {0x200, 0x1FF, 0x202, 0x208, 0x800};
    for (unsigned i = 0; i < sizeof(wrong_ids)/sizeof(wrong_ids[0]); i++) {
        BoardCanFrame bad = frame(4000, 60, 1, 20); bad.id = wrong_ids[i];
        assert(!C620_Receive(&d, BOARD_CAN_ELBOW, &bad, 20));
    }
    for (uint8_t dlc = 0; dlc <= 9; dlc++) {
        if (dlc == 8) { continue; }
        BoardCanFrame bad = frame(4000, 60, 1, 20); bad.dlc = dlc;
        assert(!C620_Receive(&d, BOARD_CAN_ELBOW, &bad, 20));
    }
    BoardCanFrame bad = frame(8192, 60, 1, 20);
    assert(!C620_Receive(&d, BOARD_CAN_ELBOW, &bad, 20));
    bad = frame(UINT16_MAX, 60, 1, 20);
    assert(!C620_Receive(&d, BOARD_CAN_ELBOW, &bad, 20));
    assert(!C620_Receive(&d, BOARD_CAN_SHOULDER, &good, 20));
    assert(!C620_Receive(&d, BOARD_CAN_COUNT, &good, 20));
    assert(!C620_Receive(&d, BOARD_CAN_ELBOW, NULL, 20));
    C620Feedback s = get(&d, 20);
    assert(s.received_ms == 10 && s.rotor_angle_ticks == 3091 && s.rotor_speed_rpm == -60);
    assert(s.valid && s.age_ms == 10);
    assert(!C620_Init(&d, 0, 100) && !C620_Init(&d, 9, 100));
    assert(!C620_Init(&d, 1, 0) && !C620_Init(&d, 1, 0x80000000U));
    assert(!C620_Init(NULL, 1, 100));
    assert(!C620_GetFeedback(&d, 20, &s) && !C620_GetFeedback(NULL, 20, &s));
    assert(!C620_Receive(&d, BOARD_CAN_ELBOW, &good, 20));
    assert(C620_Init(&d, 8, 100)); good.id = 0x208;
    assert(C620_Receive(&d, BOARD_CAN_ELBOW, &good, 20));
    assert(!C620_GetFeedback(&d, 20, NULL)); C620_Poll(NULL, 20);
}
static void freshness(void)
{
    C620Device d; assert(C620_Init(&d, 1, 100));
    C620Feedback s = get(&d, 10); assert(!s.has_sample && !s.valid);
    BoardCanFrame f = frame(3091, 0, -4, 10);
    assert(C620_Receive(&d, BOARD_CAN_ELBOW, &f, 11));
    s = get(&d, 110); assert(s.valid && s.age_ms == 100);
    s = get(&d, 111); assert(!s.valid && s.has_sample && s.received_ms == 10);
    f = frame(4000, 60, 1, 112); f.dlc = 7;
    assert(!C620_Receive(&d, BOARD_CAN_ELBOW, &f, 112));
    s = get(&d, 112); assert(!s.valid && s.received_ms == 10 && s.rotor_angle_ticks == 3091);
    f.dlc = 8; f.id = 0x202;
    assert(!C620_Receive(&d, BOARD_CAN_ELBOW, &f, 112));
    f.id = 0x201; f.received_ms = 10;
    assert(!C620_Receive(&d, BOARD_CAN_ELBOW, &f, 112)); /* Queued old frame. */
    f.received_ms = 120;
    assert(C620_Receive(&d, BOARD_CAN_ELBOW, &f, 121));
    s = get(&d, 121); assert(s.valid && s.received_ms == 120 && s.age_ms == 1);
    f = frame(4001, -60, -1, 119);
    assert(!C620_Receive(&d, BOARD_CAN_ELBOW, &f, 122)); /* Out of order. */
    f.received_ms = 120;
    assert(C620_Receive(&d, BOARD_CAN_ELBOW, &f, 122));
    s = get(&d, 122); assert(s.received_ms == 120 && s.age_ms == 2 && s.rotor_angle_ticks == 4001);
    f.received_ms = 124;
    assert(!C620_Receive(&d, BOARD_CAN_ELBOW, &f, 123)); /* Future clock snapshot. */
    assert(get(&d, 221).valid == false);
    /* Returning to a small uint32 age cannot resurrect an offline cache. */
    assert(!get(&d, 120).valid);
}
static void wrap(void)
{
    C620Device d; assert(C620_Init(&d, 1, 100));
    BoardCanFrame f = frame(1, -60, 0, UINT32_MAX - 15U);
    assert(C620_Receive(&d, BOARD_CAN_ELBOW, &f, UINT32_MAX - 14U));
    C620Feedback s = get(&d, 0x20); assert(s.valid && s.age_ms == 48);
    s = get(&d, 0x54); assert(s.valid && s.age_ms == 100);
    s = get(&d, 0x55); assert(!s.valid && s.age_ms == 101);
    assert(!get(&d, UINT32_MAX - 14U).valid);
    f.received_ms = 0x70; assert(C620_Receive(&d, BOARD_CAN_ELBOW, &f, 0x71));
    s = get(&d, 0x71); assert(s.valid && s.received_ms == 0x70 && s.age_ms == 1);
}
static void integration(void)
{
    TestClock_Set(0); CanMonitor_Init();
    C620Feedback s; assert(CanMonitor_GetElbowFeedback(&s) && !s.has_sample);
    BoardCanFrame f = frame(3091, 0, -4, 10);
    TestClock_Set(10);
    TestCan_InjectData(BOARD_CAN_SHOULDER, f.id, 8, CAN_ID_STD, CAN_RTR_DATA, f.data);
    TestCan_Interrupt(BOARD_CAN_SHOULDER); CanMonitor_Poll();
    assert(CanMonitor_GetElbowFeedback(&s) && !s.has_sample);
    TestCan_InjectData(BOARD_CAN_ELBOW, f.id, 8, CAN_ID_EXT, CAN_RTR_DATA, f.data);
    TestCan_InjectData(BOARD_CAN_ELBOW, f.id, 8, CAN_ID_STD, CAN_RTR_REMOTE, f.data);
    TestCan_Interrupt(BOARD_CAN_ELBOW); CanMonitor_Poll();
    assert(CanMonitor_GetElbowFeedback(&s) && !s.has_sample);
    TestCan_InjectData(BOARD_CAN_ELBOW, f.id, 8, CAN_ID_STD, CAN_RTR_DATA, f.data);
    TestCan_Interrupt(BOARD_CAN_ELBOW); CanMonitor_Poll();
    assert(CanMonitor_GetElbowFeedback(&s) && s.valid && s.received_ms == 10);
    /* A later wrong-ID frame remains visible raw, but cannot refresh feedback. */
    TestClock_Set(50);
    TestCan_InjectData(BOARD_CAN_ELBOW, 0x202, 8, CAN_ID_STD, CAN_RTR_DATA, f.data);
    TestCan_Interrupt(BOARD_CAN_ELBOW); CanMonitor_Poll();
    BoardCanFrame raw; assert(CanMonitor_GetLast(BOARD_CAN_ELBOW, &raw) && raw.id == 0x202);
    assert(CanMonitor_GetElbowFeedback(&s) && s.received_ms == 10 && s.age_ms == 40);
    TestClock_Set(111); CanMonitor_Poll();
    assert(CanMonitor_GetElbowFeedback(&s) && !s.valid);
    /* Historical HAL errors remain queryable even after new valid feedback. */
    hcan2.ErrorCode = HAL_CAN_ERROR_ACK; HAL_CAN_ErrorCallback(&hcan2);
    TestClock_Set(120);
    TestCan_InjectData(BOARD_CAN_ELBOW, f.id, 8, CAN_ID_STD, CAN_RTR_DATA, f.data);
    TestCan_Interrupt(BOARD_CAN_ELBOW); CanMonitor_Poll();
    assert(CanMonitor_GetElbowFeedback(&s) && s.valid && s.received_ms == 120);
    BoardCanStats stats; assert(BoardCan_GetStats(BOARD_CAN_ELBOW, &stats));
    assert(stats.invalid == 2 && stats.errors == 1 && stats.last_error == HAL_CAN_ERROR_ACK);
}
static void queue_age(void)
{
    TestClock_Set(10); CanMonitor_Init();
    BoardCanFrame f = frame(3091, 0, -4, 10);
    TestCan_InjectData(BOARD_CAN_ELBOW, f.id, 8, 0, 0, f.data);
    TestCan_Interrupt(BOARD_CAN_ELBOW);
    TestClock_Set(111); CanMonitor_Poll();
    C620Feedback s; assert(CanMonitor_GetElbowFeedback(&s) && !s.has_sample);
    BoardCanFrame raw; assert(CanMonitor_GetLast(BOARD_CAN_ELBOW, &raw) && raw.received_ms == 10);
    TestClock_Set(112);
    TestCan_InjectData(BOARD_CAN_ELBOW, f.id, 8, 0, 0, f.data);
    TestCan_Interrupt(BOARD_CAN_ELBOW); CanMonitor_Poll();
    assert(CanMonitor_GetElbowFeedback(&s) && s.valid && s.received_ms == 112);
}
void TestC620_Run(const char *name)
{
    if (!strcmp(name, "c620_codec")) { codec(); }
    else if (!strcmp(name, "c620_invalid")) { invalid(); }
    else if (!strcmp(name, "c620_freshness")) { freshness(); }
    else if (!strcmp(name, "c620_wrap")) { wrap(); }
    else if (!strcmp(name, "c620_integration")) { integration(); }
    else if (!strcmp(name, "c620_queue_age")) { queue_age(); }
    else { assert(0); }
}
