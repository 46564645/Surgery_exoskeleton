#ifndef BOARD_CAN_TX_H
#define BOARD_CAN_TX_H
/* L1: optional main-loop-only, nonblocking dual-bus TX. Not bound by S2 app.
 * Owns all TX mailboxes if enabled; no other producer may bypass the gateway. */
#include "board_can.h"
#include <stdint.h>
/* Bit 0 selects shoulder/CAN1; bit 1 selects elbow/CAN2. Zero/other bits are
 * invalid. Inactive buses cause no HAL operations. */
bool BoardCanTx_PurgeAxes(uint8_t axes_mask);
/* Frames[0]=shoulder, frames[1]=elbow. Only selected frames are inspected/sent.
 * False includes partial acceptance. No software backlog; every normal batch
 * requires selected buses' old mailboxes empty. Stop attempts ALL selected
 * buses even if one fails, without waiting or retrying. */
bool BoardCanTx_SubmitAxes(const BoardCanFrame frames[2], uint8_t axes_mask, bool stop);
/* S3 coordinator only: one read mailbox may coexist with one output frame.
 * PurgeOutput never removes that owned read; Stop must CancelShoulderRead. */
bool BoardCanTx_ShoulderReadReady(void);
bool BoardCanTx_SubmitShoulderRead(const BoardCanFrame *frame);
bool BoardCanTx_CancelShoulderRead(void);
bool BoardCanTx_PurgeOutputAxes(uint8_t axes_mask);
bool BoardCanTx_SubmitOutputAxes(const BoardCanFrame frames[2], uint8_t axes_mask, bool stop);
/* Compatibility entry points select both axes (mask 3). */
bool BoardCanTx_Purge(void);
bool BoardCanTx_Submit(const BoardCanFrame frames[2], bool stop);
#endif
