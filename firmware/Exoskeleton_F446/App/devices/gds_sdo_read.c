#include "gds_sdo_read.h"
#include <float.h>
#include <math.h>
#include <stddef.h>
#include <string.h>
_Static_assert(sizeof(float) == 4 && FLT_RADIX == 2 && FLT_MANT_DIG == 24
               && FLT_MAX_EXP == 128, "SDO float32 required");

static bool request(uint16_t endpoint, BoardCanFrame *out)
{
    if (out == NULL || endpoint == 0U) { return false; }
    memset(out, 0, sizeof(*out));
    out->id = 0x024U; out->dlc = 8U; /* node1, read opcode0, reserved/value0 */
    out->data[1] = (uint8_t)endpoint;
    out->data[2] = (uint8_t)(endpoint >> 8);
    return true;
}
static bool response_bits(uint16_t endpoint, BoardCanBus bus,
                          const BoardCanFrame *f, uint32_t *out)
{
    if (endpoint == 0U || f == NULL || out == NULL || bus != BOARD_CAN_SHOULDER
        || f->id != 0x025U || f->dlc != 8U || f->data[0] != 0U
        || f->data[3] != 0U
        || ((uint16_t)f->data[1] | (uint16_t)((uint16_t)f->data[2] << 8)) != endpoint) {
        return false;
    }
    *out = (uint32_t)f->data[4] | (uint32_t)f->data[5] << 8
        | (uint32_t)f->data[6] << 16 | (uint32_t)f->data[7] << 24;
    return true;
}
uint16_t GdsSdoRead_Endpoint(GdsReadField field)
{
    return field == GDS_READ_POSITION ? 372U
        : field == GDS_READ_VELOCITY ? 378U : 0U;
}
bool GdsSdoRead_Request(GdsReadField field, BoardCanFrame *out)
{
    return request(GdsSdoRead_Endpoint(field), out);
}
bool GdsSdoRead_Decode(GdsReadField field, BoardCanBus bus,
                       const BoardCanFrame *f, float *out)
{
    uint32_t bits;
    if (out == NULL || !response_bits(GdsSdoRead_Endpoint(field), bus, f, &bits)) {
        return false;
    }
    float value; memcpy(&value, &bits, sizeof(value));
    if (!isfinite(value)) { return false; }
    *out = value; /* Native encoder estimate: rev or rev/s, no joint mapping. */
    return true;
}
uint16_t GdsSdoRead_PropertyEndpoint(GdsPropertyField field)
{
    switch (field) {
    case GDS_PROPERTY_POSITION: return 372U;
    case GDS_PROPERTY_VELOCITY: return 378U;
    case GDS_PROPERTY_MOTOR_CALIBRATED: return 195U;
    case GDS_PROPERTY_ENCODER_READY: return 366U;
    case GDS_PROPERTY_INPUT_TORQUE: return 272U;
    case GDS_PROPERTY_CONTROL_MODE: return 287U;
    case GDS_PROPERTY_INPUT_MODE: return 288U;
    default: return 0U;
    }
}
bool GdsSdoRead_PropertyRequest(GdsPropertyField field, BoardCanFrame *out)
{
    return request(GdsSdoRead_PropertyEndpoint(field), out);
}
bool GdsSdoRead_PropertyDecode(GdsPropertyField field, BoardCanBus bus,
                              const BoardCanFrame *f, GdsPropertyValue *out)
{
    uint32_t bits;
    if (out == NULL || !response_bits(GdsSdoRead_PropertyEndpoint(field), bus, f, &bits)) {
        return false;
    }
    GdsPropertyValue value = {0};
    if (field == GDS_PROPERTY_POSITION || field == GDS_PROPERTY_VELOCITY
        || field == GDS_PROPERTY_INPUT_TORQUE) {
        memcpy(&value.real, &bits, sizeof(value.real));
        if (!isfinite(value.real)) { return false; }
    } else {
        /* Value is four LE bytes. Reject unknown/nonzero high-byte padding. */
        if (bits > 255U
            || ((field == GDS_PROPERTY_MOTOR_CALIBRATED || field == GDS_PROPERTY_ENCODER_READY)
                && bits > 1U)) { return false; }
        value.byte = (uint8_t)bits;
    }
    *out = value;
    return true;
}
