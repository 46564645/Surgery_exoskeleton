#ifndef C620_FEEDBACK_H
#define C620_FEEDBACK_H
/* L2: receive-only C620 rotor feedback; never joint coordinates or motor output. */
#include "board_can.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t received_ms;
    uint32_t age_ms; /* Derived by GetFeedback at the same time as validity. */
    float rotor_position_rad;
    float rotor_velocity_rad_s;
    uint16_t rotor_angle_ticks;
    int16_t rotor_speed_rpm;
    int16_t torque_current_raw; /* Feedback A/count scale not established. */
    uint8_t temperature_c;
    uint8_t reserved_raw;
    bool has_sample;
    bool valid; /* Freshness only; does not grant output permission. */
} C620Feedback;

typedef struct {
    C620Feedback feedback;
    uint32_t timeout_ms;
    uint16_t feedback_id;
    bool configured;
} C620Device;

/* All calls are main-loop-only. Frame format (standard DATA) is validated by L1. */
bool C620_Init(C620Device *device, uint8_t motor_id, uint32_t timeout_ms);
bool C620_Receive(C620Device *device, BoardCanBus bus,
                  const BoardCanFrame *frame, uint32_t now_ms);
void C620_Poll(C620Device *device, uint32_t now_ms);
bool C620_GetFeedback(C620Device *device, uint32_t now_ms, C620Feedback *out);
#endif
