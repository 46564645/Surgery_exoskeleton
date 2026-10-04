#ifndef CAN_MONITOR_H
#define CAN_MONITOR_H
/* L4: raw observations and receive-only C620 rotor freshness; no joint inference. */
#include "board_can.h"
#include "c620_feedback.h"
#include "gds_feedback.h"
#include "c620_standstill.h"
#include "shoulder_property_feedback.h"
void CanMonitor_Init(void);
void CanMonitor_Poll(void);
void CanMonitor_ResetElbowStandstill(uint32_t after_ms);
C620StandstillStatus CanMonitor_GetElbowStandstill(uint32_t now_ms);
bool CanMonitor_GetLast(BoardCanBus bus, BoardCanFrame *out);
bool CanMonitor_GetElbowFeedback(C620Feedback *out);
/* Archived >=0.5.13 SIMPLE wire observation only; never grants compatibility. */
bool CanMonitor_GetShoulderObservation(GdsFeedback *out, bool *wire_valid);
/* S3 exclusive TX owner arms correlation only after an accepted read. */
bool CanMonitor_ExpectShoulderProperty(GdsPropertyField field, uint32_t submitted_ms);
void CanMonitor_InvalidateShoulderProperties(void);
bool CanMonitor_NextShoulderProperty(GdsPropertyField *field, bool *pending);
bool CanMonitor_GetShoulderProperties(uint32_t now_ms, uint32_t freshness_ms, ShoulderPropertySnapshot *out);
#endif
