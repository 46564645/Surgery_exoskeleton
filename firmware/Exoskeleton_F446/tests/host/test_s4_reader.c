#include "test_s4_reader.h"
#include "shoulder_read_diagnostic.h"
#include "s3_shoulder_service.h"
#include "s3_bench.h"
#include "s4_follow.h"
#include "can_monitor.h"
#include "can_mock.h"
#include "can_tx_mock.h"
#include <assert.h>
#include <string.h>

static void claim(void)
{
    ShoulderRead_Init();
    assert(ShoulderRead_ClaimS4());
    ShoulderReadStatus s;
    ShoulderRead_GetStatus(&s);
    assert(s.phase == SHOULDER_READ_S4_OWNED && s.used_fields == 3U);
    assert(!ShoulderRead_ClaimS4());
    assert(!ShoulderRead_ClaimS3());
    assert(!ShoulderRead_ClaimStream());
    assert(!ShoulderRead_ClaimQualification());
    assert(!ShoulderRead_Start(GDS_READ_POSITION, 0U));
    ShoulderRead_GetStatus(&s);
    assert(s.phase == SHOULDER_READ_S4_OWNED && s.sequence == 0U);

    ShoulderRead_Init(); /* Separate simulated cold boot. */
    assert(ShoulderRead_ClaimS3());
    assert(!ShoulderRead_ClaimS4());
}

static void read_mode(void)
{
    /* Synthetic timing only; enabled_axes=0 gives no output permit. */
    S3SiteProfile p = {0};
    p.shoulder_source = S3_SHOULDER_ENCODER_PROPERTIES;
    p.shoulder_node = 1U;
    p.safety.feedback_timeout_ms = 100U;
    p.shoulder_property_reply_ms = 20U;
    p.shoulder_property_period_ms = 50U;
    p.shoulder_property_window_ms = 500U;
    assert(S3ShoulderService_ReadProfileReady(&p));
    assert(!S3ShoulderService_ProfileReady(&p));
    TestClock_Set(100U);
    CanMonitor_Init(&p, SHOULDER_READER_S4);
    S3Bench_Init();
    TestCanTx_Reset();
    S3ReaderStatus reader = S3ShoulderService_Get();
    assert(reader.state == S3_READER_IDLE);
    uint8_t heartbeat[8] = {0};
    heartbeat[4] = 1U; heartbeat[7] = 1U;
    TestCan_InjectData(BOARD_CAN_SHOULDER, 0x021U, 8U, 0U, 0U, heartbeat);
    TestCan_Interrupt(BOARD_CAN_SHOULDER);
    CanMonitor_Poll();
    assert(S3ShoulderService_Start(100U));
    ShoulderReadStatus owner;
    ShoulderRead_GetStatus(&owner);
    assert(owner.phase == SHOULDER_READ_S4_OWNED);
    assert(!ShoulderRead_Start(GDS_READ_POSITION, 100U));
    assert(!ShoulderRead_ClaimS3() && !ShoulderRead_ClaimStream());
    S3ShoulderService_Poll(101U);
    assert(TestCanTx_Sent(BOARD_CAN_SHOULDER) == 1U);
    assert(TestCanTx_Last(BOARD_CAN_SHOULDER)->id == 0x024U);
    assert(S3ShoulderService_Stop());
    assert(!S3ShoulderService_Start(102U));
    assert(!ShoulderRead_ClaimS4());
}

static void follow_read_only(void)
{
    S4SiteProfile p = {0};
    S4FollowStatus follow;
    S4Follow_Init(&p);
    S4Follow_GetStatus(&follow);
    assert(follow.phase == S4_FOLLOW_DISABLED);
    assert(!S4Follow_Observe(100U));

    p.s3.shoulder_source = S3_SHOULDER_ENCODER_PROPERTIES;
    p.s3.shoulder_node = 1U;
    p.s3.safety.feedback_timeout_ms = 100U;
    p.s3.shoulder_property_reply_ms = 20U;
    p.s3.shoulder_property_period_ms = 50U;
    p.s3.shoulder_property_window_ms = 500U;
    assert(!p.output_enabled && p.s3.safety.enabled_axes == 0U);
    TestClock_Set(100U);
    CanMonitor_Init(&p.s3, SHOULDER_READER_S4);
    S3Bench_Init();
    TestCanTx_Reset();
    S4Follow_Init(&p);
    S4Follow_GetStatus(&follow);
    assert(follow.phase == S4_FOLLOW_IDLE && follow.reader.state == S3_READER_IDLE);
    uint8_t heartbeat[8] = {0};
    heartbeat[4] = 1U; heartbeat[7] = 1U;
    TestCan_InjectData(BOARD_CAN_SHOULDER, 0x021U, 8U, 0U, 0U, heartbeat);
    TestCan_Interrupt(BOARD_CAN_SHOULDER);
    CanMonitor_Poll();
    assert(S4Follow_Observe(100U));
    S4Follow_Poll(101U, 1U);
    S4Follow_GetStatus(&follow);
    assert(follow.phase == S4_FOLLOW_OBSERVING && follow.reader.requests == 1U);
    assert(!follow.model_valid && !follow.new_model_sample && !follow.observation.joint.valid);
    assert(TestCanTx_Sent(BOARD_CAN_SHOULDER) == 1U);
    assert(TestCanTx_Last(BOARD_CAN_SHOULDER)->id == 0x024U);
    S4Follow_Cancel();
    S4Follow_GetStatus(&follow);
    assert(follow.phase == S4_FOLLOW_STOPPING && !follow.model_valid
        && !S4Follow_Observe(102U));
    TestClock_Set(102U);
    S4Follow_Poll(102U, 1U);
    S4Follow_GetStatus(&follow);
    assert(follow.phase == S4_FOLLOW_STOPPED && follow.reader.state == S3_READER_STOPPED);
    S3BenchStatus bench;
    S3Bench_GetStatus(&bench);
    assert(bench.state == OUTPUT_SAFE_OFF && !bench.output_allowed);
}

void TestS4Reader_Run(const char *scenario)
{
    if (strcmp(scenario, "s4_reader_claim") == 0) { claim(); }
    else if (strcmp(scenario, "s4_reader_mode") == 0) { read_mode(); }
    else if (strcmp(scenario, "s4_follow_read") == 0) { follow_read_only(); }
    else { assert(!"Unknown S4 reader scenario"); }
}
