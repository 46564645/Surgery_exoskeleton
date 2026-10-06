#ifndef CAN_BLACKBOX_RECORDER_H
#define CAN_BLACKBOX_RECORDER_H
#include <stdbool.h>
#include <stdint.h>

enum { BB_CAPACITY = 128, BB_POST_EVENTS = 16, BB_PRE_EVENTS = BB_CAPACITY - BB_POST_EVENTS };
typedef enum { BB_IRQ = 1, BB_RX, BB_TX_BEGIN, BB_TX_RESULT, BB_HAL_ERROR,
               BB_ABORT_BEGIN, BB_ABORT_RESULT, BB_MAILBOX } BbKind;
typedef struct {
    uint32_t seq, ms, esr, tsr, aux, id;
    uint8_t data[8];
    uint8_t kind, dlc, flags, status;
} BbEvent;
typedef struct {
    BbEvent events[BB_CAPACITY];
    uint32_t next_seq, overwritten, suppressed, trigger_seq;
    uint16_t head, count, post_left;
    bool triggered, frozen;
} BbRecorder;
/* Caller serializes all accesses. Zero initialization starts recording.
 * push copies the event; ring sequence is observation order, not wire order. */
void Bb_Push(BbRecorder *r, const BbEvent *event, bool fault);
bool Bb_Read(const BbRecorder *r, uint16_t index, BbEvent *out);
void Bb_Freeze(BbRecorder *r);
#endif
