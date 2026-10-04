#ifndef GDS_SDO_READ_H
#define GDS_SDO_READ_H
/* L2: read-only properties from this device's endpoint0 schema.
 * HW3.11.1/FW0.6.5/node1, 2026-10-02, SHA256:
 * cfd83580745d91c18d87c6832daa34677685e32e9d051e7f2b4bb562a90c9bc1.
 * Transport: SteadyWin rev2.2 pp45-46 RxSdo(004)/TxSdo(005).
 * This schema binding is diagnostic evidence, not a production permit.
 * The five added status/control properties have not been read over CAN.
 * Bool/uint8 decoding conservatively requires zero high Value bytes;
 * actual device padding for these properties remains unverified.
 */
#include "board_can.h"
typedef enum { GDS_READ_POSITION = 0, GDS_READ_VELOCITY = 1 } GdsReadField;
bool GdsSdoRead_Request(GdsReadField field, BoardCanFrame *out);
bool GdsSdoRead_Decode(GdsReadField field, BoardCanBus bus,
                       const BoardCanFrame *frame, float *out);
uint16_t GdsSdoRead_Endpoint(GdsReadField field);

/* Separate API: the original diagnostic enum remains position/velocity only. */
typedef enum {
    GDS_PROPERTY_POSITION,
    GDS_PROPERTY_VELOCITY,
    GDS_PROPERTY_MOTOR_CALIBRATED,
    GDS_PROPERTY_ENCODER_READY,
    GDS_PROPERTY_INPUT_TORQUE,
    GDS_PROPERTY_CONTROL_MODE,
    GDS_PROPERTY_INPUT_MODE,
    GDS_PROPERTY_COUNT
} GdsPropertyField;
typedef union { float real; uint8_t byte; } GdsPropertyValue;
bool GdsSdoRead_PropertyRequest(GdsPropertyField field, BoardCanFrame *out);
bool GdsSdoRead_PropertyDecode(GdsPropertyField field, BoardCanBus bus,
                              const BoardCanFrame *frame, GdsPropertyValue *out);
uint16_t GdsSdoRead_PropertyEndpoint(GdsPropertyField field);
#endif
