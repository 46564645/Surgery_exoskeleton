/* Host fake for the L1 HX711 bit-bang plus L2 cache and console checks. */
#include "test_hx711.h"
#include "test_console_probe.h"
#include "board_hx711.h"
#include "hx711.h"
#include "app_main.h"
#include "can_mock.h"
#include <assert.h>
#include <math.h>
#include <string.h>

static bool ready;
static uint32_t next_raw, reads;

bool BoardHx711_DataReady(void) { return ready; }
uint32_t BoardHx711_ReadRaw24(void)
{
    assert(ready); /* L2 must never clock a conversion that is not ready. */
    reads++; ready = false;
    return next_raw;
}

static void push(uint32_t raw, uint32_t now)
{
    next_raw = raw; ready = true;
    Hx711_Poll(now);
}

static void poll_and_decode(void)
{
    Hx711_Init(); ready = false; reads = 0U;
    Hx711Sample s;
    Hx711_Poll(10U); Hx711_Get(&s);
    assert(!s.has_sample && s.sequence == 0U && reads == 0U);
    push(0x000123U, 100U); Hx711_Get(&s);
    assert(s.has_sample && s.raw == 0x123 && s.sequence == 1U && s.received_ms == 100U && !s.saturated);
    Hx711_Poll(150U); Hx711_Get(&s); /* Not ready: cached sample unchanged. */
    assert(s.sequence == 1U && s.received_ms == 100U && reads == 1U);
    push(0xFFFFFFU, 200U); Hx711_Get(&s);
    assert(s.raw == -1 && s.sequence == 2U && !s.saturated);
    push(0x800000U, 300U); Hx711_Get(&s);
    assert(s.raw == -8388608 && s.saturated);
    push(0x7FFFFFU, 400U); Hx711_Get(&s);
    assert(s.raw == 8388607 && s.saturated);
    assert(Hx711_SignExtend24(0x01800000U) == -8388608); /* Bits above 24 ignored. */

    Hx711_Init();
    float force = 123.0f;
    assert(!Hx711_GetForce(400U, &force) && force == 0.0f);
    assert(!Hx711_Tare(400U) && !Hx711_GetForce(400U, NULL));
    push(144811U, 500U);
    assert(Hx711_GetForce(500U, &force) && fabsf(force) < 0.001f);
    push((uint32_t)-55222, 600U); /* Recorded 452 g point, rounded ADC code. */
    assert(Hx711_GetForce(600U, &force) && fabsf(force - 4.4326058f) < 0.001f);
    assert(Hx711_GetForce(1099U, &force));
    float previous_zero = Hx711_GetZero();
    assert(!Hx711_GetForce(1100U, &force) && force == 0.0f);
    assert(!Hx711_Tare(1100U) && Hx711_GetZero() == previous_zero);
    push(0x800000U, 1200U);
    assert(!Hx711_GetForce(1200U, &force) && !Hx711_Tare(1200U));
    assert(Hx711_GetZero() == previous_zero);
    push(150000U, UINT32_MAX - 50U);
    assert(Hx711_Tare(25U) && Hx711_GetZero() == 150000.0f); /* Timer wrap. */
    assert(Hx711_GetForce(25U, &force) && force == 0.0f);
    push(195128U, 30U); /* Opposite loading direction remains negative. */
    assert(Hx711_GetForce(30U, &force) && fabsf(force + 1.0f) < 0.001f);
    Hx711_Init();
    assert(Hx711_GetZero() == previous_zero); /* RAM tare is not persistent. */
}

static void console(void)
{
    ready = false; TestClock_Set(1000U); App_Init();
    assert(strstr(TestConsole_QueryCached("hx711\n"), "OK hx711 seen=0 seq=0 raw=0 sat=0 age_ms=na"));
    assert(strstr(TestConsole_QueryRaw("hxtare\n"), "ERR hx711 no_fresh_unsaturated_sample"));
    push(0xFFFFF0U, 1000U);
    const char *a = TestConsole_QueryCached("hx711\n");
    assert(strstr(a, "OK hx711 seen=1 seq=1 raw=-16 sat=0 age_ms="));
    push((uint32_t)-55222, 1000U);
    assert(strstr(TestConsole_QueryCached("hx711\n"), "valid=1 force_N=4.433"));
    float previous_zero = Hx711_GetZero();
    assert(strstr(TestConsole_QueryRaw("hxtare extra\n"), "ERR args"));
    assert(Hx711_GetZero() == previous_zero);
    push(150000U, 1000U);
    assert(strstr(TestConsole_QueryCached("hxtare\n"), "tared ram_only=1"));
    assert(strstr(TestConsole_QueryCached("hx711\n"), "force_N=0.000 zero_raw=150000 cal=temporary"));
    push(195128U, 1000U);
    assert(strstr(TestConsole_QueryCached("hx711\n"), "force_N=-1.000"));
    TestClock_Set(1500U);
    assert(strstr(TestConsole_QueryCached("hx711\n"), "valid=0 force_N=na"));
    assert(strstr(TestConsole_QueryRaw("hxtare\n"), "ERR hx711 no_fresh_unsaturated_sample"));
    assert(Hx711_GetZero() == 150000.0f);
}

void TestHx711_Run(const char *name)
{
    if (!strcmp(name, "hx711_poll")) { poll_and_decode(); }
    else if (!strcmp(name, "hx711_console")) { console(); }
    else { assert(!"unknown hx711 scenario"); }
}
