#ifndef S3_STOP_INPUT_H
#define S3_STOP_INPUT_H
#include "board_stop_input.h"

typedef struct {
    bool available;
    bool operator_stop_clear;
    bool independently_stopped;
} S3StopObservation;

/* Two different explicitly selected pins. Static validity does not confirm
 * circuit polarity/bias, resource ownership or physical stop completion. */
bool S3StopInput_ConfigReady(const BoardStopInputConfig *operator_input,
                           const BoardStopInputConfig *independent_input);
/* Operator active means stop requested. Independent active must mean actual
 * stop completion established by the selected physical source. Neither IDLE,
 * a zero target, an operator button nor software withdrawal substitutes for it.
 * Every call reads both inputs anew; no cache/latch. Gateway owns FAULT latch. */
S3StopObservation S3StopInput_Read(const BoardStopInputConfig *operator_input,
                                 const BoardStopInputConfig *independent_input);
#endif
