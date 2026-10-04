/* L2: DJI C620 manual / SteadyWin rev2.2 SIMPLE; see docs/s3_output_preparation.md. */
#include "motor_command.h"
#include <float.h>
#include <math.h>
#include <string.h>

_Static_assert(sizeof(float) == 4 && FLT_RADIX == 2 && FLT_MANT_DIG == 24
               && FLT_MAX_EXP == 128, "GDS requires binary32 float");

bool MotorCommand_C620(uint8_t motor_id, float current_a, BoardCanFrame *out)
{
    if (out == NULL || motor_id < 1U || motor_id > 8U || !isfinite(current_a)
        || current_a < -20.0f || current_a > 20.0f) { return false; }
    BoardCanFrame frame = {0};
    frame.id = motor_id <= 4U ? 0x200U : 0x1FFU;
    frame.dlc = 8U;
    /* Protocol full scale, NOT a project runtime limit. Truncate toward zero. */
    int16_t raw = (int16_t)(current_a * (16384.0f / 20.0f));
    unsigned slot = (unsigned)((motor_id - 1U) % 4U) * 2U;
    uint16_t bits = (uint16_t)raw;
    frame.data[slot] = (uint8_t)(bits >> 8);
    frame.data[slot + 1U] = (uint8_t)bits;
    *out = frame;
    return true;
}

static bool gds_frame(uint8_t node_id, uint16_t command, BoardCanFrame *out)
{
    /* This vendor rev2.2 manual specifies node IDs 0..63 (section3.1.5).
     * Do not import another firmware family's broadcast convention. */
    if (out == NULL || node_id > 63U) { return false; }
    BoardCanFrame frame = {0};
    frame.id = (uint16_t)(((uint16_t)node_id << 5) | command);
    frame.dlc = 8U;
    *out = frame;
    return true;
}

bool MotorCommand_GdsV22Torque(uint8_t node_id, float rotor_torque_nm, BoardCanFrame *out)
{
    if (!isfinite(rotor_torque_nm) || !gds_frame(node_id, 0x00EU, out)) { return false; }
    uint32_t bits;
    memcpy(&bits, &rotor_torque_nm, sizeof(bits));
    for (unsigned i = 0U; i < 4U; i++) { out->data[i] = (uint8_t)(bits >> (8U * i)); }
    return true;
}

bool MotorCommand_GdsV22Idle(uint8_t node_id, BoardCanFrame *out)
{
    if (!gds_frame(node_id, 0x007U, out)) { return false; }
    out->data[0] = 1U; /* requested_state=IDLE; this is NOT a physical emergency stop. */
    return true;
}

bool MotorCommand_GdsV22TorqueMode(uint8_t node_id, BoardCanFrame *out)
{
    if (!gds_frame(node_id, 0x00BU, out)) { return false; }
    out->data[0] = 1U; /* torque control; rev2.2 section3.1.7 / 4.1.2 */
    out->data[4] = 1U; /* input_mode=1, vendor's documented torque example */
    return true;
}

bool MotorCommand_GdsV22ClosedLoop(uint8_t node_id, BoardCanFrame *out)
{
    if (!gds_frame(node_id, 0x007U, out)) { return false; }
    out->data[0] = 8U; /* requested_state=closed loop; never invoked by S2 app */
    return true;
}
