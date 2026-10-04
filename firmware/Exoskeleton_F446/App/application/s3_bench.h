#ifndef S3_BENCH_H
#define S3_BENCH_H
#include "output_gateway.h"
typedef struct {
    OutputState state;
    OutputReason reason;
    bool configured, transport_bound, output_allowed, request_valid, stop_attempted;
    OutputDriverPhase preparation;
    uint32_t arm_token;
    SafetyStopPolicy stop_policy;
    bool stop_accepted, purge_ok, stop_delivery_failed;
    bool feedback_pending;
    unsigned reader_state, reader_reason;
} S3BenchStatus;
void S3Bench_Init(void);
void S3Bench_Poll(uint32_t now_ms, uint32_t service_interval_ms);
void S3Bench_GetStatus(S3BenchStatus *out);
bool S3Bench_Prepare(uint32_t now_ms);
bool S3Bench_Arm(uint32_t now_ms);
bool S3Bench_Request(uint32_t token, unsigned joint, float motor_effort, uint32_t ttl_ms, uint32_t now_ms);
void S3Bench_RejectRequest(void);
void S3Bench_Cancel(void);
bool S3Bench_AcknowledgeFault(uint32_t now_ms);
#endif
