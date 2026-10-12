#include "s5_follow.h"
#include "s3_feedback.h"
#include "can_monitor.h"
#include "output_can_transport.h"
#include "board_console.h"
#include "board_runtime.h"
#include "shoulder_standstill.h"
#include "s2_config.h"
#include <math.h>
#include <stddef.h>
#include <string.h>

static S5SiteProfile session_site;
static const S5SiteProfile *site;
static S5FollowStatus status;
static OutputGateway gateway;
static ShoulderStandstill shoulder_still;
static uint32_t prepare_started_ms, last_elapsed_ms;
static uint32_t shoulder_sequence_floor, first_sequence, first_request_ms;
static uint32_t zero_shoulder_sequence, zero_shoulder_request_ms;
static uint32_t last_sent_sequence[SAFETY_JOINT_COUNT];
static float first_rev;
static bool first_seen, preparation_submitted, selected;

static bool newer(uint32_t value, uint32_t before)
{
    uint32_t delta = value - before;
    return delta != 0U && delta < UINT32_C(0x80000000);
}

static bool dual(void)
{ return site != NULL && site->s3.safety.enabled_axes == SAFETY_ALL_AXES; }

static bool bus_healthy(BoardCanBus bus)
{
    BoardCanStats can;
    /* Historical protocol errors are diagnostic; live feedback proves recovery. */
    return BoardCan_GetStats(bus, &can) && can.started
        && can.start_error == BOARD_CAN_START_OK && !can.bus_off
        && can.dropped == 0U
        && can.fifo_overruns == 0U && can.invalid == 0U;
}

static bool read_inputs(uint32_t now_ms, uint32_t elapsed_ms,
                        SafetyInputs *in, S3SiteSample *sample)
{
    memset(in, 0, sizeof(*in));
    memset(sample, 0, sizeof(*sample));
    bool complete = S3Feedback_Read(&session_site.s3, now_ms, sample);
    memcpy(in->joint, sample->joint, sizeof(in->joint));
    in->operator_stop_clear = sample->operator_stop_clear;
    in->independently_stopped = sample->independently_stopped;
    in->feedback_stationary = sample->feedback_stationary;
    if (CanMonitor_GetElbowStandstill(now_ms).stationary) {
        in->stationary_axes |= SAFETY_ELBOW_AXES;
    }
    if (dual() && ShoulderStandstill_Read(&shoulder_still, now_ms).stationary) {
        in->stationary_axes |= SAFETY_SHOULDER_AXES;
    }
    in->drivers_ready = OutputGateway_DriverReady(&gateway, sample->driver, now_ms);
    in->transport_healthy = elapsed_ms > 0U
        && elapsed_ms <= site->s3.safety.service_limit_ms
        && !BoardConsole_HasFault() && bus_healthy(BOARD_CAN_ELBOW)
        && (!dual() || bus_healthy(BOARD_CAN_SHOULDER));
    memcpy(status.joint, sample->joint, sizeof(status.joint));
    return complete;
}

static void clear_session(void)
{
    S3Feedback_EndElbowSession();
    status.elbow_zero_valid = false;
    status.shoulder_zero_valid = false;
    status.model_valid = false;
    status.target = (S5FollowEffort){0};
    status.sample = (S5FollowSample){0};
    session_site.s3.rotor_reference_confirmed[0] = false;
    session_site.s3.rotor_reference_confirmed[1] = false;
    if (dual()) { (void)S3ShoulderService_Stop(); }
    status.reader = S3ShoulderService_Get();
}

static void fail_follow(void)
{
    if (gateway.state != OUTPUT_FAULT && gateway.state != OUTPUT_SAFE_OFF) {
        OutputGateway_RejectRequest(&gateway);
    }
    status.phase = S5_FOLLOW_FAULT;
    clear_session();
}

/* The elbow mapper already publishes a zero-relative joint angle. This latch
 * confirms the RAM origin exists without adding another mapping offset. */
static bool capture_elbow_zero(void)
{
    if (status.elbow_zero_valid) { return true; }
    if (S3Feedback_ElbowSessionPhase() == ELBOW_SESSION_FAULT) { fail_follow(); return false; }
    if (S3Feedback_ElbowSessionPhase() != ELBOW_SESSION_ACTIVE) { return false; }
    float rotor_zero_rad;
    if (!S3Feedback_ElbowSessionRotorZeroRad(&rotor_zero_rad)
        || !isfinite(rotor_zero_rad)
        || !OutputGateway_SetElbowFollowReference(&gateway, 0.0f)) {
        fail_follow(); return false;
    }
    session_site.s3.rotor_reference_confirmed[1] = true;
    status.elbow_zero_valid = true;
    return true;
}

/* Dual mode needs two distinct complete post-command SDO cycles to establish
 * a quiet shoulder origin. The first cycle is never reused as in-range proof. */
static bool capture_shoulder_zero(const S3SiteSample *sample,
                                  const SafetyInputs *in, uint32_t now_ms)
{
    if (status.shoulder_zero_valid) { return true; }
    const ShoulderPropertySnapshot *p = &sample->shoulder_properties;
    if (!p->valid || p->sequence == 0U
        || !newer(p->sequence, shoulder_sequence_floor)) { return false; }
    if (!in->transport_healthy || !sample->driver[0].valid
        || sample->driver[0].axis_state != 1U
        || !sample->driver[0].calibration_verified
        || !sample->driver[0].zero_verified) { fail_follow(); return false; }
    for (unsigned i = 0U; i < GDS_PROPERTY_COUNT; i++) {
        if (!newer(p->requested_ms[i], prepare_started_ms)
            || !newer(p->received_ms[i], prepare_started_ms)) {
            return false; /* A cycle begun before Prepare is only historical. */
        }
        if ((uint32_t)(p->received_ms[i] - p->requested_ms[i])
                > site->s3.shoulder_property_reply_ms
            || (uint32_t)(now_ms - p->requested_ms[i])
                > site->s3.safety.feedback_timeout_ms) {
            fail_follow(); return false;
        }
    }
    float rev = p->value[GDS_PROPERTY_POSITION].real;
    float speed = p->value[GDS_PROPERTY_VELOCITY].real;
    if (!isfinite(rev) || !isfinite(speed)
        || fabsf(speed) > site->s3.shoulder_standstill.max_velocity_rev_s) {
        fail_follow(); return false;
    }
    uint32_t request_ms = p->requested_ms[GDS_PROPERTY_POSITION];
    if (!first_seen) {
        first_seen = true; first_rev = rev;
        first_sequence = p->sequence; first_request_ms = request_ms;
        return false;
    }
    if (p->sequence == first_sequence) { return false; }
    if (!newer(p->sequence, first_sequence) || !newer(request_ms, first_request_ms)
        || fabsf(rev - first_rev) > site->s3.shoulder_standstill.span_rev) {
        fail_follow(); return false;
    }
    if ((uint32_t)(request_ms - first_request_ms) < site->shoulder_zero_dwell_ms) {
        return false;
    }
    float zero_rad = rev * 6.2831853071795864769f;
    if (!isfinite(zero_rad)
        || !OutputGateway_SetFollowReference(&gateway, zero_rad)) {
        fail_follow(); return false;
    }
    session_site.s3.safety.joint[0].mapping.rotor_zero_rad = zero_rad;
    session_site.s3.rotor_reference_confirmed[0] = true;
    status.shoulder_zero_valid = true;
    zero_shoulder_sequence = p->sequence;
    zero_shoulder_request_ms = request_ms;
    ShoulderStandstill_After(&shoulder_still, now_ms);
    return true;
}

static bool compute_target(const S3SiteSample *sample, S5FollowEffort *effort)
{
    *effort = (S5FollowEffort){0};
    if (!S5ElbowSupport_Compute(&site->elbow_support,
                                &sample->joint[1], &effort->elbow_current_a)) { return false; }
    if (dual()) {
        float joint_nm;
        if (!GravityCompensation_Compute(&site->shoulder_gravity,
                                         &sample->joint[0], &joint_nm)) { return false; }
        effort->shoulder_rotor_nm = joint_nm * site->shoulder_gain
            / site->s3.safety.joint[0].mapping.rotor_per_joint;
    }
    return isfinite(effort->shoulder_rotor_nm)
        && isfinite(effort->elbow_current_a);
}

void S5Follow_Init(const S5SiteProfile *profile)
{
    site = profile;
    selected = profile != NULL;
    memset(&status, 0, sizeof(status));
    status.phase = S5Site_ReadReady(profile) ? S5_FOLLOW_IDLE : S5_FOLLOW_DISABLED;
    status.axes = profile == NULL ? 0U : profile->s3.safety.enabled_axes;
    status.output_bound = status.phase == S5_FOLLOW_IDLE;
    prepare_started_ms = last_elapsed_ms = 0U;
    shoulder_sequence_floor = first_sequence = first_request_ms = 0U;
    zero_shoulder_sequence = zero_shoulder_request_ms = 0U;
    last_sent_sequence[0] = last_sent_sequence[1] = 0U;
    first_rev = 0.0f;
    first_seen = preparation_submitted = false;
    if (profile != NULL) {
        session_site = *profile;
        S3Feedback_Init();
    } else { memset(&session_site, 0, sizeof(session_site)); }
    OutputTransport port = {0};
    if (status.output_bound) { port = OutputCanTransport_Create(); }
    OutputGateway_Init(&gateway, profile == NULL ? NULL : &profile->s3.safety,
                       status.output_bound ? &port : NULL,
                       profile == NULL ? 0U : profile->s3.shoulder_node,
                       profile == NULL ? 0U : profile->s3.elbow_id);
    ShoulderStandstill_Init(&shoulder_still,
        dual() ? &profile->s3.shoulder_standstill : NULL);
    status.reader = S3ShoulderService_Get();
}

bool S5Follow_IsSelected(void) { return selected; }

bool S5Follow_Prepare(uint32_t now_ms)
{
    if (status.phase != S5_FOLLOW_IDLE || !status.output_bound
        || gateway.state != OUTPUT_SAFE_OFF || !S5Site_ReadReady(site)
        || (dual() && (S3ShoulderService_Get().state != S3_READER_IDLE
            || !S3ShoulderService_Start(now_ms)))) { return false; }
    CanMonitor_ResetElbowStandstill(now_ms);
    if (!S3Feedback_BeginElbowSession(now_ms)) { fail_follow(); return false; }
    prepare_started_ms = now_ms;
    shoulder_sequence_floor = 0U;
    if (dual()) {
        S3ShoulderObservation before = {0};
        (void)S3Feedback_ReadShoulderObservation(&session_site.s3, now_ms, &before);
        shoulder_sequence_floor = before.properties.sequence;
    }
    first_seen = preparation_submitted = false;
    status.elbow_zero_valid = false;
    status.shoulder_zero_valid = false;
    status.model_valid = false;
    if (dual()) { ShoulderStandstill_After(&shoulder_still, now_ms); }
    status.reader = S3ShoulderService_Get();
    status.phase = S5_FOLLOW_PREPARING;
    return true;
}

bool S5Follow_Start(uint32_t now_ms)
{
    if (status.phase != S5_FOLLOW_READY || gateway.state != OUTPUT_READY
        || !S5Site_ProfileReady(&session_site)) { return false; }
    SafetyInputs in; S3SiteSample sample;
    if (!read_inputs(now_ms, last_elapsed_ms, &in, &sample) || !in.drivers_ready
        || SafetySupervisor_Check(&session_site.s3.safety, &in, now_ms) != SAFETY_OK
        || !OutputGateway_Arm(&gateway, &in, now_ms)) { return false; }
    status.phase = S5_FOLLOW_RUNNING;
    last_sent_sequence[0] = last_sent_sequence[1] = 0U;
    return true;
}

void S5Follow_Poll(uint32_t now_ms, uint32_t elapsed_ms)
{
    last_elapsed_ms = elapsed_ms;
    if (status.phase != S5_FOLLOW_PREPARING && status.phase != S5_FOLLOW_READY
        && status.phase != S5_FOLLOW_RUNNING && status.phase != S5_FOLLOW_RAMPING_DOWN
        && status.phase != S5_FOLLOW_STOPPING) { return; }
    if (status.phase == S5_FOLLOW_STOPPING) {
        if (dual()) {
            S3ShoulderService_Poll(now_ms);
            status.reader = S3ShoulderService_Get();
            if (status.reader.state == S3_READER_FAULT) { fail_follow(); return; }
            if (status.reader.state != S3_READER_STOPPED
                && status.reader.state != S3_READER_COMPLETE) { return; }
        }
        status.phase = S5_FOLLOW_STOPPED;
        clear_session();
        return;
    }
    if (site == NULL || elapsed_ms == 0U
        || elapsed_ms > site->s3.safety.service_limit_ms) { fail_follow(); return; }
    if (dual()) {
        S3ShoulderService_Poll(now_ms);
        status.reader = S3ShoulderService_Get();
        if (status.reader.state != S3_READER_ACTIVE) { fail_follow(); return; }
    }
    SafetyInputs in; S3SiteSample sample;
    bool complete = read_inputs(now_ms, elapsed_ms, &in, &sample);
    if (dual() && status.shoulder_zero_valid
        && (status.phase == S5_FOLLOW_PREPARING || status.phase == S5_FOLLOW_READY)) {
        const ShoulderPropertySnapshot *p = &sample.shoulder_properties;
        bool after_zero = p->valid && newer(p->sequence, zero_shoulder_sequence)
            && newer(p->requested_ms[GDS_PROPERTY_POSITION], zero_shoulder_request_ms);
        ShoulderStandstill_Observe(&shoulder_still, after_zero ? p : NULL, now_ms);
        complete = read_inputs(now_ms, elapsed_ms, &in, &sample);
    }
    if (!in.transport_healthy) { fail_follow(); return; }
    if (status.phase == S5_FOLLOW_PREPARING && !preparation_submitted) {
        uint32_t elapsed = now_ms - prepare_started_ms;
        if (elapsed >= site->s3.prepare_timeout_ms) { fail_follow(); return; }
        if (!capture_elbow_zero()) { return; }
        if (dual() && !capture_shoulder_zero(&sample, &in, now_ms)) { return; }
        /* A fresh complete cycle after zero, not the zero cycle, must prove
         * the chosen low-pose envelope and standstill before 0-output Prepare. */
        if (dual() && !ShoulderStandstill_Read(&shoulder_still, now_ms).stationary) {
            return;
        }
        complete = read_inputs(now_ms, elapsed_ms, &in, &sample);
        if (!complete || !SafetySupervisor_StopObserved(&session_site.s3.safety, &in)) { return; }
        SafetyReason reason = SafetySupervisor_CheckBeforePrepare(&session_site.s3.safety,
                                                                   &in, now_ms);
        if (reason == SAFETY_RANGE || reason == SAFETY_SPEED) { return; }
        if (reason != SAFETY_OK || !S5Site_ProfileReady(&session_site)
            || !OutputGateway_ConfigureAxesFollow(&gateway, site->send_interval_ms,
                site->session_ms, site->ramp_max_ms, site->release_slew_per_s)
            || !OutputGateway_Prepare(&gateway, &in, sample.driver,
                site->s3.prepare_timeout_ms - elapsed, now_ms)) {
            fail_follow(); return;
        }
        preparation_submitted = true;
    }
    if (status.phase == S5_FOLLOW_PREPARING || status.phase == S5_FOLLOW_READY) {
        OutputGateway_PollPreparation(&gateway, &in, sample.driver, now_ms);
        if (gateway.state == OUTPUT_FAULT) { fail_follow(); return; }
        if (gateway.state == OUTPUT_READY) { status.phase = S5_FOLLOW_READY; }
        return;
    }
    if (!complete) { fail_follow(); return; }
    if (status.phase == S5_FOLLOW_RUNNING) {
        S5FollowEffort effort;
        if (!compute_target(&sample, &effort)) { fail_follow(); return; }
        status.target = effort;
        status.model_valid = true;
        S5FollowSample feedback = {0};
        feedback.sequence[1] = S3Feedback_ElbowSessionSequence();
        feedback.sample_ms[1] = sample.joint[1].received_ms;
        if (dual()) {
            feedback.sequence[0] = sample.shoulder_properties.sequence;
            feedback.sample_ms[0] = sample.joint[0].received_ms;
        }
        bool new_pair = newer(feedback.sequence[1], last_sent_sequence[1])
            && newer(feedback.sample_ms[1], gateway.armed_ms);
        if (dual()) {
            new_pair = new_pair && newer(feedback.sequence[0], last_sent_sequence[0])
                && newer(feedback.sample_ms[0], gateway.armed_ms);
        }
        if (new_pair) {
            if (!OutputGateway_RefreshAxesFollow(&gateway, effort, feedback, now_ms)
                && gateway.state != OUTPUT_RAMPING_DOWN) { fail_follow(); return; }
            last_sent_sequence[0] = feedback.sequence[0];
            last_sent_sequence[1] = feedback.sequence[1];
            status.sample = feedback;
        }
    }
    OutputGateway_Step(&gateway, &in, now_ms);
    if (gateway.state == OUTPUT_FAULT) { fail_follow(); return; }
    if (gateway.state == OUTPUT_RAMPING_DOWN) {
        status.phase = S5_FOLLOW_RAMPING_DOWN;
        status.model_valid = false;
    } else if (gateway.state == OUTPUT_SAFE_OFF) {
        status.phase = S5_FOLLOW_STOPPING;
        status.model_valid = false;
    }
}

bool S5Follow_Cancel(void)
{
    uint32_t now_ms = BoardRuntime_NowMs();
    if (status.phase == S5_FOLLOW_RUNNING) {
        SafetyInputs in; S3SiteSample sample;
        if (!read_inputs(now_ms, last_elapsed_ms, &in, &sample)
            || SafetySupervisor_Check(&session_site.s3.safety, &in, now_ms) != SAFETY_OK) {
            fail_follow(); return false;
        }
        if (!OutputGateway_BeginNormalStop(&gateway, OUTPUT_REASON_CANCEL, now_ms)) {
            fail_follow(); return false;
        }
        status.phase = S5_FOLLOW_RAMPING_DOWN;
        status.model_valid = false;
        return true;
    }
    if (status.phase == S5_FOLLOW_PREPARING || status.phase == S5_FOLLOW_READY) {
        if (preparation_submitted || gateway.state != OUTPUT_SAFE_OFF) {
            OutputGateway_Cancel(&gateway);
            if (gateway.state == OUTPUT_FAULT) { fail_follow(); return false; }
        } else if (dual() && !S3ShoulderService_BeginReadOnlyStop(now_ms)) {
            fail_follow(); return false;
        }
        status.reader = S3ShoulderService_Get();
        status.phase = dual() && status.reader.state != S3_READER_STOPPED
            && status.reader.state != S3_READER_COMPLETE
            ? S5_FOLLOW_STOPPING : S5_FOLLOW_STOPPED;
        if (status.phase == S5_FOLLOW_STOPPED) { clear_session(); }
    } else if (status.phase == S5_FOLLOW_IDLE) {
        status.phase = S5_FOLLOW_STOPPED;
    }
    return false;
}

void S5Follow_GetStatus(S5FollowStatus *out)
{
    if (out == NULL) { return; }
    *out = status;
    out->output_state = gateway.state;
    out->output_reason = gateway.reason;
    out->safety_reason = gateway.safety_reason;
    out->stop_attempted = gateway.stop_attempted;
    out->stop_accepted = gateway.stop_accepted;
    out->stop_delivery_failed = gateway.stop_delivery_failed;
    out->output_allowed = status.phase == S5_FOLLOW_RUNNING
        && (gateway.state == OUTPUT_ARMED || gateway.state == OUTPUT_ACTIVE);
    out->remaining_ms = 0U;
    if (site != NULL && (out->output_allowed || gateway.state == OUTPUT_RAMPING_DOWN)) {
        uint32_t elapsed = BoardRuntime_NowMs() - gateway.armed_ms;
        out->remaining_ms = elapsed < site->session_ms ? site->session_ms - elapsed : 0U;
    }
}
