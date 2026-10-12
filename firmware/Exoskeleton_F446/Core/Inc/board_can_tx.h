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
/* HAL_OK means accepted into a mailbox; only SENT proves TXOK. ALST is the
 * sole read-only outcome eligible for a bounded application retry. */
typedef enum {
    BOARD_CAN_READ_TX_NONE,
    BOARD_CAN_READ_TX_PENDING,
    BOARD_CAN_READ_TX_SENT,
    BOARD_CAN_READ_TX_ALST,
    BOARD_CAN_READ_TX_FAILED
} BoardCanReadTxResult;
BoardCanReadTxResult BoardCanTx_ShoulderReadResult(void);
/* Abort is asynchronous in bxCAN. A read-only stop polls this result without
 * treating an accepted but still-pending abort as a completed cancellation. */
typedef enum {
    BOARD_CAN_READ_CANCEL_DONE,
    BOARD_CAN_READ_CANCEL_PENDING,
    BOARD_CAN_READ_CANCEL_FAILED
} BoardCanReadCancelResult;
BoardCanReadCancelResult BoardCanTx_CancelShoulderReadStep(void);
bool BoardCanTx_CancelShoulderRead(void);
bool BoardCanTx_PurgeOutputAxes(uint8_t axes_mask);
/* A successful abort request can remain pending until bxCAN releases its
 * mailbox. These queries never submit or abort a frame. */
bool BoardCanTx_OutputPurgePending(uint8_t axes_mask);
bool BoardCanTx_OutputAxesPending(uint8_t axes_mask);
/* Positive completion of the latest shoulder normal-output mailbox. Used by
 * S4 after its accepted zero-Nm frame, before purging and sending IDLE. */
bool BoardCanTx_ShoulderOutputDrained(void);
/* True only when the last owned normal output completed without TXOK.
 * A deliberate output purge clears this evidence, so it is not a TX fault. */
bool BoardCanTx_ShoulderOutputFailed(void);
/* Latest owned normal output, selected per bus. True drained means TXOK on
 * every selected bus; an accepted HAL enqueue alone is not delivery. */
bool BoardCanTx_OutputAxesDrained(uint8_t axes_mask);
bool BoardCanTx_OutputAxesFailed(uint8_t axes_mask);
bool BoardCanTx_SubmitOutputAxes(const BoardCanFrame frames[2], uint8_t axes_mask, bool stop);
/* Compatibility entry points select both axes (mask 3). */
bool BoardCanTx_Purge(void);
bool BoardCanTx_Submit(const BoardCanFrame frames[2], bool stop);
#endif
