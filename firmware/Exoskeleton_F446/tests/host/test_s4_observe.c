/* Synthetic read-only evidence; no physical mapping or motor permission. */
#include "test_s4_observe.h"
#include "s3_feedback.h"
#include "s3_shoulder_service.h"
#include "can_monitor.h"
#include "can_mock.h"
#include "can_tx_mock.h"
#include "board_can_tx.h"
#include "gds_sdo_read.h"
#include "s2_config.h"
#include <assert.h>
#include <math.h>
#include <string.h>

static const uint16_t endpoint[GDS_PROPERTY_COUNT] =
    {372U, 378U, 195U, 366U, 272U, 287U, 288U};
static const uint32_t value_bits[GDS_PROPERTY_COUNT] =
    {0x40400000U, 0xBE000000U, 1U, 1U, 0U, 1U, 1U};

static S3SiteProfile read_profile(void)
{
    S3SiteProfile p = {0};
    p.shoulder_source = S3_SHOULDER_ENCODER_PROPERTIES;
    p.shoulder_node = S2_GDS_NODE;
    p.safety.feedback_timeout_ms = 100U;
    p.shoulder_property_reply_ms = 20U;
    p.shoulder_property_period_ms = 50U;
    p.shoulder_property_window_ms = 500U;
    p.coordinates[0] = S3_ASSEMBLED_JOINT;
    p.rotor_reference_confirmed[0] = true;
    p.trusted_feedback_axes = SAFETY_SHOULDER_AXES;
    p.safety.joint[0].feedback_confirmed = true;
    p.safety.joint[0].protocol_confirmed = true;
    p.safety.joint[0].mapping = (JointMapping){2.0f, 1.0f, -1, true};
    return p;
}

static void inject_heartbeat(uint32_t now_ms, uint32_t axis_error, uint8_t life)
{
    uint8_t data[8] = {0};
    for (unsigned i = 0U; i < 4U; i++) { data[i] = (uint8_t)(axis_error >> (8U*i)); }
    data[4] = 8U; data[7] = life;
    TestClock_Set(now_ms);
    TestCan_InjectData(BOARD_CAN_SHOULDER, 0x021U, 8U, 0U, 0U, data);
    TestCan_Interrupt(BOARD_CAN_SHOULDER);
    CanMonitor_Poll();
}

static void inject_property(unsigned field, uint32_t sent_ms)
{
    uint8_t data[8] = {0};
    uint32_t bits = value_bits[field];
    data[1] = (uint8_t)endpoint[field];
    data[2] = (uint8_t)(endpoint[field] >> 8);
    for (unsigned i = 0U; i < 4U; i++) { data[i + 4U] = (uint8_t)(bits >> (8U*i)); }
    BoardCanFrame request;
    assert(GdsSdoRead_PropertyRequest((GdsPropertyField)field, &request));
    assert(BoardCanTx_SubmitShoulderRead(&request));
    assert(CanMonitor_ExpectShoulderProperty((GdsPropertyField)field, sent_ms));
    TestCanTx_Complete(BOARD_CAN_SHOULDER);
    TestClock_Set(sent_ms + 1U);
    TestCan_InjectData(BOARD_CAN_SHOULDER, 0x025U, 8U, 0U, 0U, data);
    TestCan_Interrupt(BOARD_CAN_SHOULDER);
    CanMonitor_Poll();
}

static void raw_and_mapping(void)
{
    S3SiteProfile p = read_profile();
    assert(p.safety.enabled_axes == 0U);
    assert(S3ShoulderService_ReadProfileReady(&p));
    assert(!S3ShoulderService_ProfileReady(&p));
    TestClock_Set(100U);
    CanMonitor_Init(&p, SHOULDER_READER_S4);
    TestCanTx_Reset();
    S3ShoulderObservation o;
    assert(!S3Feedback_ReadShoulderObservation(NULL, 100U, &o));
    assert(!S3Feedback_ReadShoulderObservation(&p, 100U, NULL));
    assert(S3Feedback_ReadShoulderObservation(&p, 100U, &o));
    assert(!o.has_heartbeat && !o.wire_valid && !o.heartbeat_healthy);
    assert(!o.properties_fresh && !o.joint.valid && o.properties.sequence == 0U);
    S3SiteProfile invalid = p;
    invalid.shoulder_source = S3_SHOULDER_SOURCE_UNSET;
    assert(!S3Feedback_ReadShoulderObservation(&invalid, 100U, &o));
    assert(!o.has_heartbeat && !o.properties_fresh && !o.joint.valid);

    inject_heartbeat(100U, 0U, 1U);
    assert(S3Feedback_ReadShoulderObservation(&p, 100U, &o));
    assert(o.has_heartbeat && o.heartbeat_healthy && !o.wire_valid);
    assert(!o.properties_fresh && !o.joint.valid);
    inject_property(0U, 101U);
    assert(S3Feedback_ReadShoulderObservation(&p, 102U, &o));
    assert(!o.properties_fresh && o.properties.sequence == 0U && !o.joint.valid);
    for (unsigned i = 1U; i < GDS_PROPERTY_COUNT; i++) {
        inject_property(i, 101U + 2U*i);
    }
    assert(S3Feedback_ReadShoulderObservation(&p, 114U, &o));
    assert(o.properties_fresh && o.properties.valid && o.properties.sequence == 1U);
    assert(o.properties.requested_ms[GDS_PROPERTY_POSITION] == 101U);
    assert(o.properties.received_ms[GDS_PROPERTY_INPUT_MODE] == 114U);
    assert(o.joint.valid && o.joint.received_ms == 101U);
    assert(fabsf(o.joint.position_rad + 8.924778f) < 0.00001f);
    assert(fabsf(o.joint.velocity_rad_s - 0.3926991f) < 0.00001f);

    S3SiteProfile unconfirmed = p;
    unconfirmed.rotor_reference_confirmed[0] = false;
    assert(S3Feedback_ReadShoulderObservation(&unconfirmed, 114U, &o));
    assert(o.properties_fresh && !o.joint.valid);
    unconfirmed = p; unconfirmed.safety.joint[0].protocol_confirmed = false;
    assert(S3Feedback_ReadShoulderObservation(&unconfirmed, 114U, &o));
    assert(o.properties_fresh && !o.joint.valid);
    unconfirmed = p; unconfirmed.coordinates[0] = S3_BENCH_ROTOR;
    assert(S3Feedback_ReadShoulderObservation(&unconfirmed, 114U, &o));
    assert(o.properties_fresh && !o.joint.valid);
    unconfirmed = p; unconfirmed.safety.joint[0].mapping.direction = 0;
    assert(S3Feedback_ReadShoulderObservation(&unconfirmed, 114U, &o));
    assert(o.properties_fresh && !o.joint.valid);
    inject_heartbeat(115U, 1U, 2U);
    assert(S3Feedback_ReadShoulderObservation(&p, 115U, &o));
    assert(o.properties_fresh && !o.heartbeat_healthy && !o.joint.valid);
    assert(TestCanTx_Sent(BOARD_CAN_SHOULDER) == GDS_PROPERTY_COUNT);
    assert(TestCanTx_Sent(BOARD_CAN_ELBOW) == 0U);
}

static void age_and_fault(void)
{
    S3SiteProfile p = read_profile();
    TestClock_Set(100U);
    CanMonitor_Init(&p, SHOULDER_READER_S4);
    TestCanTx_Reset();
    inject_heartbeat(100U, 0U, 1U);
    for (unsigned i = 0U; i < GDS_PROPERTY_COUNT; i++) {
        inject_property(i, 101U + 2U*i);
    }
    S3ShoulderObservation o;
    TestClock_Set(200U);
    assert(S3Feedback_ReadShoulderObservation(&p, 200U, &o));
    assert(o.heartbeat_healthy && o.properties_fresh && o.joint.valid);
    TestClock_Set(201U);
    assert(S3Feedback_ReadShoulderObservation(&p, 201U, &o));
    assert(!o.heartbeat_healthy && o.properties_fresh && !o.joint.valid);
    TestClock_Set(202U);
    assert(S3Feedback_ReadShoulderObservation(&p, 202U, &o));
    assert(!o.properties_fresh && o.properties.sequence == 1U && !o.joint.valid);
    /* A current heartbeat does not rescue a quarantined property cycle. */
    inject_heartbeat(203U, 1U, 2U);
    assert(S3Feedback_ReadShoulderObservation(&p, 203U, &o));
    assert(!o.heartbeat_healthy && !o.joint.valid);
    CanMonitor_InvalidateShoulderProperties();
    TestClock_Set(204U);
    assert(S3Feedback_ReadShoulderObservation(&p, 204U, &o));
    assert(!o.properties_fresh && !o.properties.valid && !o.joint.valid);
    assert(TestCanTx_Sent(BOARD_CAN_SHOULDER) == GDS_PROPERTY_COUNT);
    assert(TestCanTx_Sent(BOARD_CAN_ELBOW) == 0U);
}

void TestS4Observe_Run(const char *scenario)
{
    if (strcmp(scenario, "s4_observe_raw_mapping") == 0) { raw_and_mapping(); }
    else if (strcmp(scenario, "s4_observe_age_fault") == 0) { age_and_fault(); }
    else { assert(!"Unknown S4 observation scenario"); }
}
