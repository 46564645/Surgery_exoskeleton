#ifndef ENCODER_STREAM_H
#define ENCODER_STREAM_H
#include "gds_sdo_read.h"
/* Bench observation budgets, not motion-control timing guarantees. */
#define ENCODER_STREAM_DURATION_MS 30000U
#define ENCODER_STREAM_PERIOD_MS 50U
#define ENCODER_STREAM_REPLY_MS 50U
#define ENCODER_STREAM_FRESH_MS 100U
/* Manufacturer rev2.2 periodic-message table: default heartbeat100ms.
 * Allow heartbeat scheduling jitter separately from the100ms encoder pair age.
 * Bench read-only budget; it does not relax the S3 permit's freshness rules. */
#define ENCODER_STREAM_HEARTBEAT_MS 250U
#define ENCODER_STREAM_MAX_REQUESTS 1200U
typedef enum {
    ENCODER_STREAM_NONE, ENCODER_STREAM_USER, ENCODER_STREAM_LIMIT,
    ENCODER_STREAM_OWNER, ENCODER_STREAM_HEARTBEAT, ENCODER_STREAM_CAN,
    ENCODER_STREAM_TX, ENCODER_STREAM_TIMEOUT, ENCODER_STREAM_REPLY,
    ENCODER_STREAM_STALE, ENCODER_STREAM_CONSOLE
} EncoderStreamReason;
typedef struct {
    bool used, active, pending, has_pair, valid;
    EncoderStreamReason reason;
    uint32_t started_ms, stopped_ms, elapsed_ms, requests, pairs, rejected;
    uint32_t position_ms, velocity_ms, age_ms;
    float position_rev, velocity_rev_s, position_rad, velocity_rad_s;
    BoardCanFrame position_frame, velocity_frame, last_sdo;
} EncoderStreamStatus;
/* Main-loop only. Manual start, one bounded session per MCU boot, mutually
 * exclusive with one-shot diagnostic reads. No automatic start/restart/retry.
 * CAN SDO has no transaction token: isolated single master and in-order replies
 * are prerequisites; local pair count is not a device transaction identifier. */
void EncoderStream_Init(void);
bool EncoderStream_Start(uint32_t now_ms);
void EncoderStream_Stop(uint32_t now_ms);
void EncoderStream_Receive(BoardCanBus bus, const BoardCanFrame *frame, uint32_t now_ms);
void EncoderStream_Poll(uint32_t now_ms);
void EncoderStream_GetStatus(uint32_t now_ms, EncoderStreamStatus *out);
#endif
