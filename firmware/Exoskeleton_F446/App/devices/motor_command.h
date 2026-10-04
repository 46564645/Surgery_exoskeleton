#ifndef MOTOR_COMMAND_H
#define MOTOR_COMMAND_H
/* L2: pure command encoding; never sends or enables a driver. */
#include "board_can.h"
#include <stdbool.h>

/* DJI C620 manual: group slots not selected here are always zero. */
bool MotorCommand_C620(uint8_t motor_id, float current_a, BoardCanFrame *out);
/* SteadyWin rev2.2 SIMPLE profile, NOT verified against actual FW0.6.5.
 * Always DLC8, explicitly padded. Torque is ROTOR Nm, not output-shaft Nm.
 * The caller must confirm firmware compatibility before using these frames. */
bool MotorCommand_GdsV22Torque(uint8_t node_id, float rotor_torque_nm, BoardCanFrame *out);
bool MotorCommand_GdsV22Idle(uint8_t node_id, BoardCanFrame *out);
/* Research-profile preparation frames only; no production call path. */
bool MotorCommand_GdsV22TorqueMode(uint8_t node_id, BoardCanFrame *out);
bool MotorCommand_GdsV22ClosedLoop(uint8_t node_id, BoardCanFrame *out);
#endif
