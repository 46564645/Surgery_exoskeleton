/* SteadyWin rev2.2 4.1.1/4.1.2, original PDF pp41,44-45,48. */
#include "gds_feedback.h"
#include <float.h>
#include <math.h>
#include <stddef.h>
#include <string.h>
_Static_assert(sizeof(float) == 4 && FLT_RADIX == 2 && FLT_MANT_DIG == 24
               && FLT_MAX_EXP == 128, "SIMPLE float32 required");
static uint32_t le32(const uint8_t *p)
{ return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static float decode_float(const uint8_t *p)
{ uint32_t bits = le32(p); float value; memcpy(&value, &bits, sizeof(value)); return value; }
bool GdsFeedback_Init(GdsDevice *d, uint8_t node, GdsProfile profile, uint32_t timeout_ms)
{
    if (d == NULL) { return false; }
    memset(d, 0, sizeof(*d));
    if (node > 63U || profile != GDS_PROFILE_V22_FW_GE_0513 || timeout_ms == 0U
        || timeout_ms >= UINT32_C(0x80000000)) { return false; }
    d->node = node; d->profile = profile; d->timeout_ms = timeout_ms; d->configured = true;
    return true;
}
bool GdsFeedback_Receive(GdsDevice *d, BoardCanBus bus, const BoardCanFrame *f, uint32_t now_ms)
{
    if (d == NULL || !d->configured || f == NULL || bus != BOARD_CAN_SHOULDER
        || f->id > 0x7FFU || (f->id >> 5) != d->node || f->dlc != 8U
        || (uint32_t)(now_ms - f->received_ms) > d->timeout_ms) { return false; }
    GdsFeedback next = d->sample;
    uint16_t command = f->id & 0x1FU;
    if (command == 0x009U) {
        const float tau = 6.2831853071795864769f;
        float p = decode_float(f->data) * tau, v = decode_float(f->data + 4) * tau;
        if (!isfinite(p) || !isfinite(v)) { return false; }
        next.rotor_position_rad = p; next.rotor_velocity_rad_s = v;
        next.estimate_ms = f->received_ms; next.has_estimate = true;
    } else if (command == 0x001U) {
        /* Explicit >=0.5.13 layout; never guess an older format from payload. */
        next.axis_error = le32(f->data); next.axis_state = f->data[4];
        next.fault_flags = f->data[5] & 0x0FU; next.life = f->data[7];
        next.life_contiguous = !next.has_heartbeat
            || (uint8_t)(d->sample.life + 1U) == next.life;
        next.heartbeat_ms = f->received_ms; next.has_heartbeat = true;
    } else { return false; }
    d->sample = next;
    return true;
}
bool GdsFeedback_Get(const GdsDevice *d, uint32_t now_ms, GdsFeedback *out)
{
    if (d == NULL || out == NULL || !d->configured) { return false; }
    *out = d->sample;
    return out->has_estimate && out->has_heartbeat && out->axis_error == 0U
        && out->fault_flags == 0U && out->life_contiguous
        && (uint32_t)(now_ms - out->estimate_ms) <= d->timeout_ms
        && (uint32_t)(now_ms - out->heartbeat_ms) <= d->timeout_ms;
}
