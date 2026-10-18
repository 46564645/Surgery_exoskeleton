#ifndef HX711_H
#define HX711_H
#include <stdbool.h>
#include <stdint.h>

/* L2: HX711 10 SPS auxiliary force monitor (registry ADC-01).
 * Temporary single-point calibration; never a control or safety input. */
typedef struct {
    int32_t raw;            /* Signed 24-bit ADC code, channel A gain 128. */
    uint32_t sequence;      /* Increments once per new conversion read. */
    uint32_t received_ms;
    bool has_sample;
    bool saturated;         /* Code 0x7FFFFF or 0x800000: input out of range. */
} Hx711Sample;

int32_t Hx711_SignExtend24(uint32_t raw24);
void Hx711_Init(void);
/* Non-blocking: reads at most one conversion when DOUT already reports ready. */
void Hx711_Poll(uint32_t now_ms);
void Hx711_Get(Hx711Sample *out);
/* False for missing, saturated or stale data; clears *force_n on failure. */
bool Hx711_GetForce(uint32_t now_ms, float *force_n);
/* Explicit unloaded tare using the latest fresh sample, without waiting.
 * RAM only: Init restores the calibration zero. Does not change the scale. */
bool Hx711_Tare(uint32_t now_ms);
float Hx711_GetZero(void);
#endif
