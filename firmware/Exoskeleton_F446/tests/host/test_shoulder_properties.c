/* Synthetic CAN replies only: no device values or physical confirmations. */
#include "test_shoulder_properties.h"
#include "shoulder_property_feedback.h"
#include "can_monitor.h"
#include "can_mock.h"
#include "can_tx_mock.h"
#include "board_can_tx.h"
#include "s3_bench.h"
#include "s3_feedback.h"
#include "s3_site_mock.h"
#include <assert.h>
#include <math.h>
#include <string.h>

static const uint16_t endpoints[] = {372U,378U,195U,366U,272U,287U,288U};
/* 3 rev, -0.125 rev/s, calibrated, encoder ready, zero target, torque/passthrough. */
static const uint32_t payload[] = {0x40400000U,0xBE000000U,1U,1U,0U,1U,1U};
static BoardCanFrame reply(unsigned field, uint32_t stamp, uint32_t bits)
{
    BoardCanFrame f = {0}; f.id = 0x025U; f.dlc = 8U; f.received_ms = stamp;
    f.data[1] = (uint8_t)endpoints[field]; f.data[2] = (uint8_t)(endpoints[field] >> 8);
    for (unsigned i = 0U; i < 4U; i++) { f.data[4U+i] = (uint8_t)(bits >> (8U*i)); }
    return f;
}
static void codec(void)
{
    BoardCanFrame request; GdsPropertyValue v;
    for (unsigned i = 0U; i < GDS_PROPERTY_COUNT; i++) {
        assert(GdsSdoRead_PropertyEndpoint((GdsPropertyField)i) == endpoints[i]);
        assert(GdsSdoRead_PropertyRequest((GdsPropertyField)i, &request));
        assert(request.id == 0x024U && request.dlc == 8U && request.data[0] == 0U);
        assert(request.data[1] == (uint8_t)endpoints[i] && request.data[2] == (uint8_t)(endpoints[i] >> 8));
        for (unsigned j = 3U; j < 8U; j++) { assert(request.data[j] == 0U); }
        BoardCanFrame f = reply(i, 10U, payload[i]);
        assert(GdsSdoRead_PropertyDecode((GdsPropertyField)i, BOARD_CAN_SHOULDER, &f, &v));
        assert(!GdsSdoRead_PropertyDecode((GdsPropertyField)i, BOARD_CAN_ELBOW, &f, &v));
        f.id = 0x045U; assert(!GdsSdoRead_PropertyDecode((GdsPropertyField)i, BOARD_CAN_SHOULDER, &f, &v));
    }
    BoardCanFrame f = reply(0U, 0U, 0x7FC00000U);
    assert(!GdsSdoRead_PropertyDecode(GDS_PROPERTY_POSITION, BOARD_CAN_SHOULDER, &f, &v));
    f = reply(2U, 0U, 2U);
    assert(!GdsSdoRead_PropertyDecode(GDS_PROPERTY_MOTOR_CALIBRATED, BOARD_CAN_SHOULDER, &f, &v));
    f = reply(5U, 0U, 0x101U);
    assert(!GdsSdoRead_PropertyDecode(GDS_PROPERTY_CONTROL_MODE, BOARD_CAN_SHOULDER, &f, &v));
    assert(!GdsSdoRead_Request((GdsReadField)2, &request)); /* Old diagnostic API stays narrow. */
    assert(!GdsSdoRead_PropertyRequest(GDS_PROPERTY_COUNT, &request));
}
static void cycle(ShoulderPropertyFeedback *r, uint32_t start)
{
    for (unsigned i = 0U; i < GDS_PROPERTY_COUNT; i++) {
        uint32_t sent = start + i * 2U;
        BoardCanFrame f = reply(i, sent + 1U, payload[i]);
        assert(ShoulderPropertyFeedback_Expect(r, (GdsPropertyField)i, sent));
        ShoulderPropertyFeedback_Receive(r, BOARD_CAN_SHOULDER, &f, sent + 1U);
    }
}
static void association(void)
{
    ShoulderPropertyFeedback r; ShoulderPropertySnapshot s;
    ShoulderPropertyFeedback_Init(&r, 0U);
    assert(!ShoulderPropertyFeedback_Expect(&r, GDS_PROPERTY_POSITION, 1U));
    ShoulderPropertyFeedback_Init(&r, 10U);
    assert(!ShoulderPropertyFeedback_Get(&r, 0U, 50U, &s));
    assert(ShoulderPropertyFeedback_Expect(&r, GDS_PROPERTY_POSITION, 1U));
    BoardCanFrame f = reply(0U, 2U, payload[0]);
    ShoulderPropertyFeedback_Receive(&r, BOARD_CAN_SHOULDER, &f, 2U);
    assert(!ShoulderPropertyFeedback_Get(&r, 2U, 50U, &s)); /* No partial snapshot. */
    f = reply(0U, 3U, payload[0]); /* Duplicate reply: dropped, not latched. */
    ShoulderPropertyFeedback_Receive(&r, BOARD_CAN_SHOULDER, &f, 3U);
    assert(!r.quarantined && ShoulderPropertyFeedback_Expect(&r, GDS_PROPERTY_VELOCITY, 4U));
    ShoulderPropertyFeedback_Init(&r, 10U); cycle(&r, 100U);
    assert(ShoulderPropertyFeedback_Get(&r, 113U, 50U, &s));
    assert(s.value[GDS_PROPERTY_POSITION].real == 3.0f && s.value[GDS_PROPERTY_VELOCITY].real == -0.125f);
    assert(s.requested_ms[GDS_PROPERTY_INPUT_TORQUE] == 108U && s.received_ms[GDS_PROPERTY_INPUT_TORQUE] == 109U);
    assert(ShoulderPropertyFeedback_Expect(&r, GDS_PROPERTY_POSITION, 120U));
    f = reply(0U, 121U, 0x40800000U);
    ShoulderPropertyFeedback_Receive(&r, BOARD_CAN_SHOULDER, &f, 121U);
    assert(ShoulderPropertyFeedback_Get(&r, 121U, 50U, &s) && s.value[0].real == 3.0f);
    assert(!ShoulderPropertyFeedback_Get(&r, 151U, 50U, &s)); /* Age of oldest request. */
    assert(ShoulderPropertyFeedback_Expect(&r, GDS_PROPERTY_VELOCITY, 152U));
    f = reply(1U, 151U, payload[1]); /* Predates its request. */
    ShoulderPropertyFeedback_Receive(&r, BOARD_CAN_SHOULDER, &f, 153U);
    assert(r.quarantined && !ShoulderPropertyFeedback_Get(&r, 153U, 1000U, &s));
    ShoulderPropertyFeedback_Init(&r, 10U);
    assert(ShoulderPropertyFeedback_Expect(&r, GDS_PROPERTY_POSITION, 10U));
    ShoulderPropertyFeedback_Poll(&r, 21U); /* First timeout only retries. */
    assert(!r.quarantined && !r.pending && r.reply_retries == 1U);
    f = reply(0U, 22U, payload[0]); /* Reply after the retry was issued. */
    ShoulderPropertyFeedback_Receive(&r, BOARD_CAN_SHOULDER, &f, 22U);
    assert(!r.quarantined && !r.has_reply && r.reply_retries == 1U);
    uint32_t retry_ms = 30U;
    for (unsigned i = 0U; i < 3U; i++) {
        assert(ShoulderPropertyFeedback_Expect(&r, GDS_PROPERTY_POSITION, retry_ms));
        ShoulderPropertyFeedback_Poll(&r, retry_ms + 11U);
        assert(!r.quarantined);
        retry_ms += 20U;
    }
    assert(ShoulderPropertyFeedback_Expect(&r, GDS_PROPERTY_POSITION, retry_ms));
    ShoulderPropertyFeedback_Poll(&r, retry_ms + 11U);
    assert(r.quarantined && !r.published.valid); /* Fifth timeout latches. */
    ShoulderPropertyFeedback_Init(&r, 10U); cycle(&r, UINT32_MAX - 5U);
    assert(ShoulderPropertyFeedback_Get(&r, 7U, 50U, &s));
    assert(!ShoulderPropertyFeedback_Get(&r, 45U, 50U, &s));
    ShoulderPropertyFeedback_Init(&r, 10U);
    assert(ShoulderPropertyFeedback_Expect(&r, GDS_PROPERTY_POSITION, 110U));
    f = reply(0U, 111U, payload[0]);
    ShoulderPropertyFeedback_Receive(&r, BOARD_CAN_SHOULDER, &f, 111U);
    assert(!ShoulderPropertyFeedback_Expect(&r, GDS_PROPERTY_VELOCITY, 105U));
    assert(r.quarantined); /* Field order alone cannot prove timestamp order. */
}
static void s5_fast_path(void)
{
    ShoulderPropertyFeedback r; ShoulderPropertySnapshot s;
    ShoulderPropertyFeedback_Init(&r, 10U);
    ShoulderPropertyFeedback_EnableS5FastPath(&r);
    assert(!ShoulderPropertyFeedback_RequestZeroProof(&r)); /* Bootstrap first. */
    cycle(&r, 100U);
    assert(ShoulderPropertyFeedback_Get(&r, 113U, 50U, &s) && s.sequence == 1U);
    assert(s.zero_proof_valid && s.mode_pair_valid);

    BoardCanFrame f = reply(GDS_PROPERTY_POSITION, 121U, 0x40800000U);
    assert(ShoulderPropertyFeedback_Expect(&r, GDS_PROPERTY_POSITION, 120U));
    ShoulderPropertyFeedback_Receive(&r, BOARD_CAN_SHOULDER, &f, 121U);
    assert(ShoulderPropertyFeedback_Get(&r, 121U, 50U, &s)
        && s.sequence == 1U && s.value[GDS_PROPERTY_POSITION].real == 3.0f);
    assert(ShoulderPropertyFeedback_RequestZeroProof(&r));
    assert(!ShoulderPropertyFeedback_RequestZeroProof(&r));
    f = reply(GDS_PROPERTY_VELOCITY, 123U, payload[GDS_PROPERTY_VELOCITY]);
    assert(ShoulderPropertyFeedback_Expect(&r, GDS_PROPERTY_VELOCITY, 122U));
    ShoulderPropertyFeedback_Receive(&r, BOARD_CAN_SHOULDER, &f, 123U);
    assert(ShoulderPropertyFeedback_Get(&r, 123U, 50U, &s)
        && s.sequence == 2U && s.value[GDS_PROPERTY_POSITION].real == 4.0f
        && !s.zero_proof_valid && r.next == GDS_PROPERTY_INPUT_TORQUE);
    f = reply(GDS_PROPERTY_INPUT_TORQUE, 125U, payload[GDS_PROPERTY_INPUT_TORQUE]);
    assert(ShoulderPropertyFeedback_Expect(&r, GDS_PROPERTY_INPUT_TORQUE, 124U));
    ShoulderPropertyFeedback_Receive(&r, BOARD_CAN_SHOULDER, &f, 125U);
    assert(ShoulderPropertyFeedback_Get(&r, 125U, 50U, &s)
        && s.sequence == 2U && s.zero_proof_valid
        && s.requested_ms[GDS_PROPERTY_INPUT_TORQUE] == 124U);

    assert(ShoulderPropertyFeedback_RequestModeProof(&r));
    f = reply(GDS_PROPERTY_POSITION, 131U, payload[GDS_PROPERTY_POSITION]);
    assert(ShoulderPropertyFeedback_Expect(&r, GDS_PROPERTY_POSITION, 130U));
    ShoulderPropertyFeedback_Receive(&r, BOARD_CAN_SHOULDER, &f, 131U);
    f = reply(GDS_PROPERTY_VELOCITY, 133U, payload[GDS_PROPERTY_VELOCITY]);
    assert(ShoulderPropertyFeedback_Expect(&r, GDS_PROPERTY_VELOCITY, 132U));
    ShoulderPropertyFeedback_Receive(&r, BOARD_CAN_SHOULDER, &f, 133U);
    assert(ShoulderPropertyFeedback_Get(&r, 133U, 50U, &s)
        && s.sequence == 3U && !s.mode_pair_valid && r.next == GDS_PROPERTY_CONTROL_MODE);
    f = reply(GDS_PROPERTY_CONTROL_MODE, 135U, payload[GDS_PROPERTY_CONTROL_MODE]);
    assert(ShoulderPropertyFeedback_Expect(&r, GDS_PROPERTY_CONTROL_MODE, 134U));
    ShoulderPropertyFeedback_Receive(&r, BOARD_CAN_SHOULDER, &f, 135U);
    assert(ShoulderPropertyFeedback_Get(&r, 135U, 50U, &s)
        && !s.mode_pair_valid && s.sequence == 3U);
    f = reply(GDS_PROPERTY_INPUT_MODE, 137U, payload[GDS_PROPERTY_INPUT_MODE]);
    assert(ShoulderPropertyFeedback_Expect(&r, GDS_PROPERTY_INPUT_MODE, 136U));
    ShoulderPropertyFeedback_Receive(&r, BOARD_CAN_SHOULDER, &f, 137U);
    assert(ShoulderPropertyFeedback_Get(&r, 137U, 50U, &s)
        && s.mode_pair_valid && s.sequence == 3U
        && s.requested_ms[GDS_PROPERTY_CONTROL_MODE] == 134U
        && s.requested_ms[GDS_PROPERTY_INPUT_MODE] == 136U);
    /* Static reads may age; live position/velocity must not. */
    f = reply(GDS_PROPERTY_POSITION, 301U, payload[GDS_PROPERTY_POSITION]);
    assert(ShoulderPropertyFeedback_Expect(&r, GDS_PROPERTY_POSITION, 300U));
    ShoulderPropertyFeedback_Receive(&r, BOARD_CAN_SHOULDER, &f, 301U);
    f = reply(GDS_PROPERTY_VELOCITY, 303U, payload[GDS_PROPERTY_VELOCITY]);
    assert(ShoulderPropertyFeedback_Expect(&r, GDS_PROPERTY_VELOCITY, 302U));
    ShoulderPropertyFeedback_Receive(&r, BOARD_CAN_SHOULDER, &f, 303U);
    assert(ShoulderPropertyFeedback_Get(&r, 303U, 50U, &s) && s.sequence == 4U);
    assert(!ShoulderPropertyFeedback_Get(&r, 351U, 50U, &s));
    ShoulderPropertyFeedback_Invalidate(&r);
    assert(!ShoulderPropertyFeedback_Get(&r, 303U, 50U, &s));
}
static void consumer(void)
{
    TestClock_Set(0U); TestS3Site_ConfigureShoulderProperties(10U);
    CanMonitor_Init(S3Site_GetProfile(), SHOULDER_READER_S3); TestCanTx_Reset(); S3Bench_Init();
    S3SiteProfile p = *S3Site_GetProfile();
    p.shoulder_node = p.elbow_id = 1U; p.safety.enabled_axes = SAFETY_SHOULDER_AXES;
    p.safety.feedback_timeout_ms = 50U;
    p.safety.joint[0].feedback_confirmed = p.safety.joint[0].protocol_confirmed = true;
    p.safety.joint[0].mapping = (JointMapping){2.0f, 1.0f, -1, true}; /* Synthetic only. */
    p.coordinates[0] = S3_ASSEMBLED_JOINT; p.rotor_reference_confirmed[0] = true;
    p.trusted_feedback_axes = SAFETY_SHOULDER_AXES;
    uint8_t heartbeat[8] = {0,0,0,0,8,0,0,1}; /* Closed-loop is not an IDLE-only reader. */
    TestClock_Set(100U);
    TestCan_InjectData(BOARD_CAN_SHOULDER, 0x021U, 8U, 0U, 0U, heartbeat);
    const uint8_t legacy_zero[8] = {0};
    TestCan_InjectData(BOARD_CAN_SHOULDER, 0x029U, 8U, 0U, 0U, legacy_zero);
    TestCan_Interrupt(BOARD_CAN_SHOULDER); CanMonitor_Poll();
    S3SiteSample s;
    assert(!S3Feedback_Read(&p, 100U, &s)); /* Legacy zero frame cannot supply S3. */
    for (unsigned i = 0U; i < GDS_PROPERTY_COUNT; i++) {
        uint32_t sent = 101U + 2U*i;
        BoardCanFrame request;
        assert(GdsSdoRead_PropertyRequest((GdsPropertyField)i, &request));
        assert(BoardCanTx_SubmitShoulderRead(&request));
        assert(CanMonitor_ExpectShoulderProperty((GdsPropertyField)i, sent));
        BoardCanFrame f = reply(i, sent+1U, payload[i]);
        TestCanTx_Complete(BOARD_CAN_SHOULDER);
        TestClock_Set(sent+1U);
        TestCan_InjectData(BOARD_CAN_SHOULDER, f.id, f.dlc, 0U, 0U, f.data);
        TestCan_Interrupt(BOARD_CAN_SHOULDER); CanMonitor_Poll();
    }
    assert(S3Feedback_Read(&p, 114U, &s) && s.joint[0].valid && s.driver[0].valid);
    assert(fabsf(s.joint[0].position_rad + 8.924778f) < 0.00001f);
    assert(fabsf(s.joint[0].velocity_rad_s - 0.3926991f) < 0.00001f);
    assert(s.joint[0].received_ms == 101U && s.shoulder_rotor.rotor_position_rad == 0.0f);
    assert(s.driver[0].calibration_verified && s.driver[0].zero_verified && s.driver[0].mode_verified);
    assert(s.driver[0].zero_verified_ms == 109U && s.driver[0].mode_verified_ms == 111U);
    assert(!s.operator_stop_clear && !s.independently_stopped); /* No fabricated stop proof. */
    p.shoulder_source = S3_SHOULDER_SOURCE_UNSET;
    assert(!S3Feedback_Read(&p, 114U, &s));
    p.shoulder_source = S3_SHOULDER_ENCODER_PROPERTIES; p.rotor_reference_confirmed[0] = false;
    assert(!S3Feedback_Read(&p, 114U, &s));
    p.rotor_reference_confirmed[0] = true; p.safety.joint[0].protocol_confirmed = false;
    assert(!S3Feedback_Read(&p, 114U, &s) && !s.driver[0].valid);
    p.safety.joint[0].protocol_confirmed = true;
    assert(!S3Feedback_Read(&p, 152U, &s) && !s.driver[0].valid);
    CanMonitor_InvalidateShoulderProperties();
    assert(!S3Feedback_Read(&p, 114U, &s));
    S3BenchStatus gate; S3Bench_GetStatus(&gate);
    assert(!gate.configured && !gate.transport_bound && !gate.output_allowed);
    assert(TestCanTx_Sent(BOARD_CAN_SHOULDER) == GDS_PROPERTY_COUNT && TestCanTx_Sent(BOARD_CAN_ELBOW) == 0U);
}
static void s5_consumer(void)
{
    TestClock_Set(0U); TestS3Site_ConfigureShoulderProperties(10U);
    S3SiteProfile p = *S3Site_GetProfile();
    p.shoulder_node = p.elbow_id = 1U; p.safety.enabled_axes = SAFETY_SHOULDER_AXES;
    p.safety.feedback_timeout_ms = 50U;
    p.safety.joint[0].feedback_confirmed = p.safety.joint[0].protocol_confirmed = true;
    p.safety.joint[0].mapping = (JointMapping){2.0f, 1.0f, -1, true};
    p.coordinates[0] = S3_ASSEMBLED_JOINT; p.rotor_reference_confirmed[0] = true;
    p.trusted_feedback_axes = SAFETY_SHOULDER_AXES;
    CanMonitor_Init(&p, SHOULDER_READER_S5); TestCanTx_Reset();
    uint8_t hb[8] = {0,0,0,0,1,0,0,1};
    TestClock_Set(100U);
    TestCan_InjectData(BOARD_CAN_SHOULDER, 0x021U, 8U, 0U, 0U, hb);
    TestCan_Interrupt(BOARD_CAN_SHOULDER); CanMonitor_Poll();
    for (unsigned i = 0U; i < GDS_PROPERTY_COUNT; i++) {
        uint32_t sent = 101U + 2U*i;
        BoardCanFrame request, f = reply(i, sent + 1U, payload[i]);
        assert(GdsSdoRead_PropertyRequest((GdsPropertyField)i, &request));
        assert(BoardCanTx_SubmitShoulderRead(&request));
        assert(CanMonitor_ExpectShoulderProperty((GdsPropertyField)i, sent));
        TestCanTx_Complete(BOARD_CAN_SHOULDER);
        TestClock_Set(sent + 1U);
        TestCan_InjectData(BOARD_CAN_SHOULDER, f.id, f.dlc, 0U, 0U, f.data);
        TestCan_Interrupt(BOARD_CAN_SHOULDER); CanMonitor_Poll();
    }
    S3SiteSample s;
    assert(S3Feedback_Read(&p, 114U, &s) && s.driver[0].valid);
    hb[4] = 8U; hb[7] = 2U;
    TestClock_Set(300U);
    TestCan_InjectData(BOARD_CAN_SHOULDER, 0x021U, 8U, 0U, 0U, hb);
    TestCan_Interrupt(BOARD_CAN_SHOULDER); CanMonitor_Poll();
    for (unsigned i = 0U; i < 2U; i++) {
        uint32_t sent = 301U + 2U*i;
        BoardCanFrame request, f = reply(i, sent + 1U, payload[i]);
        assert(GdsSdoRead_PropertyRequest((GdsPropertyField)i, &request));
        assert(BoardCanTx_SubmitShoulderRead(&request));
        assert(CanMonitor_ExpectShoulderProperty((GdsPropertyField)i, sent));
        TestCanTx_Complete(BOARD_CAN_SHOULDER);
        TestClock_Set(sent + 1U);
        TestCan_InjectData(BOARD_CAN_SHOULDER, f.id, f.dlc, 0U, 0U, f.data);
        TestCan_Interrupt(BOARD_CAN_SHOULDER); CanMonitor_Poll();
    }
    assert(S3Feedback_Read(&p, 304U, &s) && s.driver[0].valid
        && s.driver[0].calibration_verified && s.driver[0].zero_verified
        && s.driver[0].mode_verified && s.shoulder_properties.sequence == 2U);
    assert(s.shoulder_properties.requested_ms[GDS_PROPERTY_MOTOR_CALIBRATED] == 105U);
    assert(s.shoulder_properties.requested_ms[GDS_PROPERTY_POSITION] == 301U);
    assert(s.shoulder_properties.requested_ms[GDS_PROPERTY_VELOCITY] == 303U);
}
void TestShoulderProperties_Run(const char *name)
{
    if (!strcmp(name, "s3_properties_codec")) { codec(); }
    else if (!strcmp(name, "s3_properties_association")) { association(); s5_fast_path(); }
    else { assert(!strcmp(name, "s3_properties_consumer")); consumer(); s5_consumer(); }
}
