/* Real receive snapshots, separate from test site and physical confirmations. */
#include "s3_feedback.h"
#include "s3_shoulder_service.h"
#include "can_monitor.h"
#include "s2_config.h"
#include <math.h>
#include <stddef.h>
#include <string.h>
static S3RelativeAnchor anchor;
void S3Feedback_Init(void) { anchor = (S3RelativeAnchor){0}; }
bool S3Feedback_BeginRelative(uint32_t now_ms)
{
    if (anchor.phase != S3_RELATIVE_UNSET) { return false; }
    anchor.phase = S3_RELATIVE_WAITING; anchor.begin_ms = now_ms;
    return true;
}
void S3Feedback_EndRelative(void)
{
    if (anchor.phase == S3_RELATIVE_WAITING || anchor.phase == S3_RELATIVE_ACTIVE) {
        anchor.phase = S3_RELATIVE_RETIRED;
    }
}
S3RelativeAnchor S3Feedback_GetAnchor(void) { return anchor; }
static bool relative_sample(const S3SiteProfile *p, const ShoulderPropertySnapshot *s,
                            const OutputDriverStatus *d, uint32_t now_ms, JointState *out)
{
    const SafetyJointConfig *j = &p->safety.joint[0];
    if (!j->relative_native_envelope || p->rotor_reference_confirmed[0]
        || !SafetySupervisor_ConfigReady(&p->safety)) { return false; }
    if (anchor.phase == S3_RELATIVE_WAITING) {
        if (!s->valid || s->sequence == 0U || !d->valid || d->axis_state != 1U
            || !d->calibration_verified || !d->zero_verified) { return false; }
        /* Full new cycle after this Prepare, including timestamp wrap.
         * Published historical data can never establish a new origin. */
        for (unsigned i = 0U; i < GDS_PROPERTY_COUNT; i++) {
            uint32_t q = s->requested_ms[i], r = s->received_ms[i];
            if ((uint32_t)(q - anchor.begin_ms) == 0U
                || (uint32_t)(q - anchor.begin_ms) >= UINT32_C(0x80000000)
                || (uint32_t)(r - anchor.begin_ms) == 0U
                || (uint32_t)(r - anchor.begin_ms) >= UINT32_C(0x80000000)
                || (uint32_t)(r - q) > p->shoulder_property_reply_ms
                || (uint32_t)(now_ms - q) > p->safety.feedback_timeout_ms
                || (uint32_t)(now_ms - r) > p->safety.feedback_timeout_ms) { return false; }
        }
        float origin = s->value[GDS_PROPERTY_POSITION].real;
        if (!isfinite(origin)) { return false; }
        anchor.origin_rev = origin; anchor.sequence = s->sequence;
        anchor.request_ms = s->requested_ms[GDS_PROPERTY_POSITION];
        anchor.reply_ms = s->received_ms[GDS_PROPERTY_POSITION];
        anchor.phase = S3_RELATIVE_ACTIVE;
    }
    if (anchor.phase != S3_RELATIVE_ACTIVE) { return false; }
    const float tau = 6.2831853071795864769f;
    /* Subtract in native rev first; do not lose small movement by subtracting
     * independently converted large absolute angles. Velocity is unchanged. */
    return JointState_FromRotor(&j->mapping,
        (s->value[GDS_PROPERTY_POSITION].real - anchor.origin_rev) * tau,
        s->value[GDS_PROPERTY_VELOCITY].real * tau,
        s->requested_ms[GDS_PROPERTY_POSITION], true, out);
}
static bool elbow_bench_sector(const S3SiteProfile *p, const C620Feedback *s, JointState *out)
{
    const SafetyJointConfig *c = &p->safety.joint[1]; const JointMapping *m = &c->mapping;
    if (p->coordinates[1] != S3_BENCH_ROTOR || !p->bench_sector_confirmed[1]
        || (p->trusted_feedback_axes & SAFETY_ELBOW_AXES) == 0U
        || p->elbow_id != S2_C620_ID || !s->valid
        || !c->feedback_confirmed || !c->protocol_confirmed || !m->confirmed
        || !c->limits_confirmed || m->rotor_per_joint != 1.0f || m->rotor_zero_rad != 0.0f
        || (m->direction != 1 && m->direction != -1)) { return false; }
    /* Fixed encoder origin, explicitly scoped single-turn bench sector.
     * Never labels modulo position continuous or an assembled joint zero.
     * Confirmed range must stay away from the encoder wrap on either sign. */
    float a = (float)m->direction * c->lower_rad;
    float z = (float)m->direction * c->upper_rad;
    float lower = a < z ? a : z, upper = a < z ? z : a;
    if (!(lower > 0.0f && upper < 6.2831853071795864769f && lower < upper)) { return false; }
    *out = (JointState){(float)m->direction * s->rotor_position_rad,
        (float)m->direction * s->rotor_velocity_rad_s, s->received_ms, true};
    return true;
}
bool S3Feedback_Read(const S3SiteProfile *p, uint32_t now_ms, S3SiteSample *out)
{
    if (p == NULL || out == NULL) { return false; }
    memset(out, 0, sizeof(*out));
    bool wire = false;
    if (CanMonitor_GetShoulderObservation(&out->shoulder_rotor, &wire)) {
        const GdsFeedback *s = &out->shoulder_rotor;
        if (s->has_estimate || s->has_heartbeat) { out->observed_axes |= SAFETY_SHOULDER_AXES; }
        if (wire) { out->wire_valid_axes |= SAFETY_SHOULDER_AXES; }
        OutputDriverStatus *d = &out->driver[0];
        d->node = S2_GDS_NODE; d->received_ms = s->heartbeat_ms;
        d->axis_error = s->axis_error; d->axis_state = s->axis_state; d->fault_flags = s->fault_flags;
        ShoulderPropertySnapshot *properties = &out->shoulder_properties;
        bool fresh = p->shoulder_source == S3_SHOULDER_ENCODER_PROPERTIES
            && CanMonitor_GetShoulderProperties(now_ms, p->safety.feedback_timeout_ms, properties);
        bool heartbeat = s->has_heartbeat && s->life_contiguous && s->axis_error == 0U
            && s->fault_flags == 0U
            && (uint32_t)(now_ms - s->heartbeat_ms) <= p->safety.feedback_timeout_ms;
        d->valid = fresh && heartbeat && p->shoulder_node == S2_GDS_NODE
            && p->safety.joint[0].feedback_confirmed && p->safety.joint[0].protocol_confirmed;
        if (fresh) {
            const GdsPropertyValue *v = properties->value;
            d->calibration_verified = v[GDS_PROPERTY_MOTOR_CALIBRATED].byte == 1U
                && v[GDS_PROPERTY_ENCODER_READY].byte == 1U;
            /* Device-reported target/mode only, not actual torque or stop. */
            d->zero_verified = v[GDS_PROPERTY_INPUT_TORQUE].real == 0.0f;
            d->mode_verified = v[GDS_PROPERTY_CONTROL_MODE].byte == 1U
                && v[GDS_PROPERTY_INPUT_MODE].byte == 1U;
            d->zero_verified_ms = properties->requested_ms[GDS_PROPERTY_INPUT_TORQUE];
            /* Fixed cycle order makes control_mode the older of the two reads,
             * including tick wrap. A late reply cannot certify a newer write. */
            d->mode_verified_ms = properties->requested_ms[GDS_PROPERTY_CONTROL_MODE];
        }
        if (p->coordinates[0] == S3_BENCH_RELATIVE_ROTOR) {
            if (d->valid && (p->trusted_feedback_axes & SAFETY_SHOULDER_AXES) != 0U) {
                (void)relative_sample(p, properties, d, now_ms, &out->joint[0]);
            } else if (anchor.phase == S3_RELATIVE_ACTIVE) { S3Feedback_EndRelative(); }
        } else if (p->coordinates[0] != S3_COORDINATE_UNSET && p->rotor_reference_confirmed[0]
            && (p->trusted_feedback_axes & SAFETY_SHOULDER_AXES) != 0U && d->valid) {
            const float tau = 6.2831853071795864769f;
            (void)JointState_FromRotor(&p->safety.joint[0].mapping,
                properties->value[GDS_PROPERTY_POSITION].real * tau,
                properties->value[GDS_PROPERTY_VELOCITY].real * tau,
                properties->requested_ms[GDS_PROPERTY_POSITION], true, &out->joint[0]);
        }
    }
    if (CanMonitor_GetElbowFeedback(&out->elbow_rotor)) {
        const C620Feedback *s = &out->elbow_rotor;
        if (s->has_sample) { out->observed_axes |= SAFETY_ELBOW_AXES; }
        if (s->valid) { out->wire_valid_axes |= SAFETY_ELBOW_AXES; }
        out->driver[1].node = S2_C620_ID; out->driver[1].received_ms = s->received_ms;
        /* No C620 axis-state/error/command echo is invented from feedback. */
        out->driver[1].valid = s->valid && p->elbow_id == S2_C620_ID
            && p->safety.joint[1].feedback_confirmed && p->safety.joint[1].protocol_confirmed
            && p->operational_confirmed[1]; /* Actual LED/commissioning evidence, default false. */
        /* Bench sector has an explicit factory encoder origin; assembled mode
         * still has no trusted multi-turn/reference provider. */
        (void)elbow_bench_sector(p, s, &out->joint[1]);
    }
    bool complete = p->safety.enabled_axes != 0U;
    for (unsigned axis = 0U; axis < 2U; axis++) {
        if (SafetySupervisor_AxisEnabled(&p->safety, axis)
            && (!out->joint[axis].valid || (uint32_t)(now_ms - out->joint[axis].received_ms)
                > p->safety.feedback_timeout_ms)) { complete = false; }
    }
    C620StandstillStatus still = CanMonitor_GetElbowStandstill(now_ms);
    out->feedback_stationary = p->safety.stop_policy == SAFETY_STOP_SUPERVISED_SHOULDER_BENCH
        ? S3ShoulderService_GetStandstill(now_ms).stationary : still.stationary;
    if (p->stop_input_confirmed && p->safety.stop_policy == SAFETY_STOP_EXTERNAL_INPUT) {
        S3StopObservation stop = S3StopInput_Read(&p->operator_stop_input, &p->independent_stop_input);
        out->operator_stop_clear = stop.available && stop.operator_stop_clear;
        out->independently_stopped = stop.available && stop.independently_stopped;
    }
    return complete;
}
