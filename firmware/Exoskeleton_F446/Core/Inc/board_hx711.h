#ifndef BOARD_HX711_H
#define BOARD_HX711_H
#include <stdbool.h>
#include <stdint.h>

/* L1: HX711 on PC3 PD_SCK (push-pull output, latched low in MX_GPIO_Init) and
 * PC2 DOUT (input, no pull). Never waits: call ReadRaw24 only after
 * DataReady. Channel A gain 128 = 24 data pulses + 1 (HX711 datasheet p.5). */
bool BoardHx711_DataReady(void);
/* Returns the 24-bit two's-complement code, MSB first. Each SCK high is a
 * short critical section so an IRQ cannot stretch it past 60 us (power down). */
uint32_t BoardHx711_ReadRaw24(void);
#endif
