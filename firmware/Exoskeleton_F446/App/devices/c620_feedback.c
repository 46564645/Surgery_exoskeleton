/* L2: C620 manual p33; BetaRobot/UICRM big-endian fields and freshness pattern.
 * Main-loop adaptation of the protocol only: no RTOS, CAN transmit, gearing,
 * direction/zero calibration, or unverified feedback-current scaling.
 */
#include "c620_feedback.h"
#include <stddef.h>
#include <string.h>

#define C620_ANGLE_TO_RAD (6.2831853071795864769f / 8192.0f)
#define C620_RPM_TO_RAD_S (6.2831853071795864769f / 60.0f)

static uint16_t read_u16_be(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static int16_t read_i16_be(const uint8_t *data)
{
    uint16_t raw = read_u16_be(data);
    int32_t value = raw < 0x8000U ? (int32_t)raw : (int32_t)raw - 65536;
    return (int16_t)value; /* Conversion is always within int16_t range. */
}

bool C620_Init(C620Device *device, uint8_t motor_id, uint32_t timeout_ms)
{
    if (device == NULL) { return false; }
    memset(device, 0, sizeof(*device));
    if (motor_id < 1U || motor_id > 8U || timeout_ms == 0U || timeout_ms >= 0x80000000U) {
        return false;
    }
    device->feedback_id = (uint16_t)(0x200U + motor_id);
    device->timeout_ms = timeout_ms;
    device->configured = true;
    return true;
}

void C620_Poll(C620Device *device, uint32_t now_ms)
{
    if (device != NULL && device->configured && device->feedback.valid
        && (uint32_t)(now_ms - device->feedback.received_ms) > device->timeout_ms) {
        device->feedback.valid = false;
    }
    /* An old timestamp wrapping back to a small age cannot restore validity. */
}

bool C620_Receive(C620Device *device, BoardCanBus bus,
                  const BoardCanFrame *frame, uint32_t now_ms)
{
    if (device == NULL || !device->configured) { return false; }
    C620_Poll(device, now_ms);
    if (frame == NULL || bus != BOARD_CAN_ELBOW || frame->id != device->feedback_id
        || frame->dlc != 8U) { return false; }
    uint16_t angle = read_u16_be(frame->data);
    uint32_t age = (uint32_t)(now_ms - frame->received_ms);
    if (angle > 8191U || age > device->timeout_ms) { return false; }
    if (device->feedback.has_sample) {
        uint32_t previous_age = (uint32_t)(now_ms - device->feedback.received_ms);
        if (previous_age <= device->timeout_ms && age > previous_age) { return false; }
    }
    C620Feedback next = {0};
    next.received_ms = frame->received_ms;
    next.rotor_angle_ticks = angle;
    next.rotor_speed_rpm = read_i16_be(frame->data + 2);
    next.torque_current_raw = read_i16_be(frame->data + 4);
    next.temperature_c = frame->data[6];
    next.reserved_raw = frame->data[7];
    next.rotor_position_rad = (float)angle * C620_ANGLE_TO_RAD;
    next.rotor_velocity_rad_s = (float)next.rotor_speed_rpm * C620_RPM_TO_RAD_S;
    next.has_sample = true;
    next.valid = true;
    device->feedback = next;
    return true;
}

bool C620_GetFeedback(C620Device *device, uint32_t now_ms, C620Feedback *out)
{
    if (device == NULL || !device->configured || out == NULL) { return false; }
    C620_Poll(device, now_ms);
    *out = device->feedback;
    if (out->has_sample) { out->age_ms = (uint32_t)(now_ms - out->received_ms); }
    return true;
}
