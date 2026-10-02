#ifndef CAN_MONITOR_H
#define CAN_MONITOR_H
/* L4: raw observations and receive-only C620 rotor freshness; no joint inference. */
#include "board_can.h"
#include "c620_feedback.h"
void CanMonitor_Init(void);
void CanMonitor_Poll(void);
bool CanMonitor_GetLast(BoardCanBus bus, BoardCanFrame *out);
bool CanMonitor_GetElbowFeedback(C620Feedback *out);
#endif
