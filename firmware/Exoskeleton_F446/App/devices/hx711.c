/* L2: HX711 sample cache over the L1 bit-bang read. */
#include "hx711.h"
#include "board_hx711.h"
#include <stddef.h>

static Hx711Sample sample;
/* 2026-10-17, current mounting/load point only:
 * unloaded mean 144811.46; 452 g mean -55221.87; g=9.80665 m/s^2.
 * User accepted this temporary calibration without return-to-zero/multipoint
 * validation. STM32-Libraries HX711_get_units/tare: offset then scale.
 * Load decreases raw counts, so subtract raw from zero.
 * 500 ms freshness is a display/tare timeout (about five 10 SPS periods),
 * not a motor safety threshold. */
static const float calibration_zero_raw = 144811.46f;
static const float counts_per_newton = 45127.7057f;
static const uint32_t freshness_ms = 500U;
static float zero_raw;

int32_t Hx711_SignExtend24(uint32_t raw24)
{
    raw24 &= 0x00FFFFFFU;
    return (raw24 & 0x00800000U) != 0U ? (int32_t)(raw24 | 0xFF000000U) : (int32_t)raw24;
}

void Hx711_Init(void)
{
    sample = (Hx711Sample){0};
    zero_raw = calibration_zero_raw;
}

void Hx711_Poll(uint32_t now_ms)
{
    if (!BoardHx711_DataReady()) { return; }
    uint32_t raw24 = BoardHx711_ReadRaw24() & 0x00FFFFFFU;
    sample.raw = Hx711_SignExtend24(raw24);
    sample.saturated = raw24 == 0x007FFFFFU || raw24 == 0x00800000U;
    sample.received_ms = now_ms;
    sample.sequence++;
    sample.has_sample = true;
}

void Hx711_Get(Hx711Sample *out)
{
    if (out != NULL) { *out = sample; }
}

static bool fresh(uint32_t now_ms)
{
    return sample.has_sample && !sample.saturated
        && (uint32_t)(now_ms - sample.received_ms) < freshness_ms;
}

bool Hx711_GetForce(uint32_t now_ms, float *force_n)
{
    if (force_n == NULL) { return false; }
    *force_n = 0.0f;
    if (!fresh(now_ms)) { return false; }
    *force_n = (zero_raw - (float)sample.raw) / counts_per_newton;
    return true;
}

bool Hx711_Tare(uint32_t now_ms)
{
    if (!fresh(now_ms)) { return false; }
    zero_raw = (float)sample.raw;
    return true;
}

float Hx711_GetZero(void)
{
    return zero_raw;
}
