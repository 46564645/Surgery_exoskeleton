#include "s5_follow.h"
#include "s3_feedback.h"
#include "can_monitor.h"
#include "output_can_transport.h"
#include "board_console.h"
#include "board_runtime.h"
#include "shoulder_standstill.h"
#include "s2_config.h"
#include "s5_assist_compose.h"
#include "hx711.h"
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
static float assist_scale[SAFETY_JOINT_COUNT];
static bool assist_quiet[SAFETY_JOINT_COUNT], assist_paused[SAFETY_JOINT_COUNT];
static uint32_t assist_quiet_ms[SAFETY_JOINT_COUNT], assist_scale_ms[SAFETY_JOINT_COUNT];
static ShoulderAmplify amplify;
static ShoulderAmplify elbow_amplify;
static ForceAssistState force_state;
static bool assist_enabled = true;

static bool newer(uint32_t value, uint32_t before)
{
    uint32_t delta = value - before;
    return delta != 0U && delta < UINT32_C(0x80000000);
}

static bool dual(void)
{ return site != NULL && site->s3.safety.enabled_axes == SAFETY_ALL_AXES; }

static bool soft_speed_enabled(void)
{ return dual() && site->soft_speed_rad_s[0] > 0.0f && site->soft_speed_rad_s[1] > 0.0f; }

static void reset_assist_pause(uint32_t now_ms)
{
    status.assist_paused = false;
    status.speed_pauses = 0U;
    status.peak_speed_rad_s[0] = status.peak_speed_rad_s[1] = 0.0f;
    for (unsigned axis = 0U; axis < SAFETY_JOINT_COUNT; axis++) {
        assist_paused[axis] = assist_quiet[axis] = false;
        assist_scale[axis] = 1.0f;
        assist_quiet_ms[axis] = assist_scale_ms[axis] = now_ms;
    }
    ShoulderAmplify_Reset(&amplify, now_ms);
    ShoulderAmplify_Reset(&elbow_amplify, now_ms);
}

/* ExoKit AmplifyReaction::runLoop() owns one joint: the joint that exceeds its
 * own speed limit drops its torque for that loop while another joint's
 * reaction keeps running. Each axis here follows the same rule with its own
 * soft speed, half-speed resume threshold, quiet time and ramp, so a fast
 * elbow no longer withdraws the shoulder baseline. No FAULT here;
 * SafetySupervisor keeps the hard limit, feedback, range and transport checks
 * for the whole arm. */
static float assist_gain_axis(unsigned axis, const S3SiteSample *sample, uint32_t now_ms)
{
    float speed = fabsf(sample->joint[axis].velocity_rad_s);
    if (speed > status.peak_speed_rad_s[axis]) { status.peak_speed_rad_s[axis] = speed; }
    if (!soft_speed_enabled()) { return 1.0f; }
    if (speed > site->soft_speed_rad_s[axis]) {
        if (!assist_paused[axis] && status.speed_pauses < UINT16_MAX) { status.speed_pauses++; }
        assist_paused[axis] = true;
        assist_scale[axis] = 0.0f;
        assist_quiet[axis] = false;
        return 0.0f;
    }
    if (assist_paused[axis]) {
        if (speed >= 0.5f * site->soft_speed_rad_s[axis]) {
            assist_quiet[axis] = false;
            return 0.0f;
        }
        if (!assist_quiet[axis]) {
            assist_quiet[axis] = true;
            assist_quiet_ms[axis] = now_ms;
            return 0.0f;
        }
        if ((uint32_t)(now_ms - assist_quiet_ms[axis]) < site->resume_quiet_ms) { return 0.0f; }
        assist_paused[axis] = false;
        assist_scale_ms[axis] = now_ms;
        return 0.0f; /* Ramp starts from zero on the next poll. */
    }
    if (assist_scale[axis] < 1.0f) {
        uint32_t elapsed = now_ms - assist_scale_ms[axis];
        assist_scale[axis] += (float)elapsed / (float)site->resume_ramp_ms;
        if (assist_scale[axis] > 1.0f) { assist_scale[axis] = 1.0f; }
    }
    assist_scale_ms[axis] = now_ms;
    return assist_scale[axis];
}

static void assist_gains(const S3SiteSample *sample, uint32_t now_ms,
                         float gain[SAFETY_JOINT_COUNT])
{
    for (unsigned axis = 0U; axis < SAFETY_JOINT_COUNT; axis++) {
        gain[axis] = assist_gain_axis(axis, sample, now_ms);
    }
    status.assist_paused = assist_paused[0] || assist_paused[1];
}

static bool bus_healthy(BoardCanBus bus)
{
    BoardCanStats can;
    if (!BoardCan_GetStats(bus, &can) || !can.started
        || can.start_error != BOARD_CAN_START_OK || can.bus_off) { return false; }
    /* For dual FOLLOW, fresh validated feedback proves recovery from an
     * earlier queue/protocol error. These cumulative counters remain visible. */
    return dual() || (can.dropped == 0U
        && can.fifo_overruns == 0U && can.invalid == 0U);
}

#define S5_SPEED_DEBOUNCE_POLLS 3U
static uint8_t speed_over_count[SAFETY_JOINT_COUNT];

/* One corrupted C620 frame can report a physically impossible speed. Industry
 * practice (WPILib Debouncer, Tinymovr) requires the condition to persist, so
 * the latched hard limit needs three consecutive 1 ms polls. The raw peak is
 * still reported; only the safety input is filtered. */
static void debounce_speed(SafetyInputs *in)
{
    for (unsigned axis = 0U; axis < SAFETY_JOINT_COUNT; axis++) {
        const SafetyJointConfig *j = &session_site.s3.safety.joint[axis];
        float limit = j->max_speed_rad_s;
        float v = in->joint[axis].velocity_rad_s;
        if (!isfinite(limit) || limit <= 0.0f || !isfinite(v)
            || (v <= limit && v >= -limit)) {
            speed_over_count[axis] = 0U;
            continue;
        }
        if (speed_over_count[axis] < UINT8_MAX) { speed_over_count[axis]++; }
        if (speed_over_count[axis] < S5_SPEED_DEBOUNCE_POLLS) {
            in->joint[axis].velocity_rad_s = v > 0.0f ? limit : -limit;
        }
    }
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
#if defined(S5_R5_DUAL_FOLLOW)
    /* The two quiet position/velocity pairs used for the RAM zero also prove
     * preparation standstill. Do not start another 200 ms observation window. */
    if (dual() && status.shoulder_zero_valid && sample->joint[0].valid
        && (uint32_t)(now_ms - sample->joint[0].received_ms)
            <= site->s3.safety.feedback_timeout_ms
        && sample->shoulder_properties.valid
        && isfinite(sample->shoulder_properties.value[GDS_PROPERTY_VELOCITY].real)
        && fabsf(sample->shoulder_properties.value[GDS_PROPERTY_VELOCITY].real)
            <= site->s3.shoulder_standstill.max_velocity_rev_s) {
        in->stationary_axes |= SAFETY_SHOULDER_AXES;
    }
#else
    if (dual() && ShoulderStandstill_Read(&shoulder_still, now_ms).stationary) {
        in->stationary_axes |= SAFETY_SHOULDER_AXES;
    }
#endif
    in->drivers_ready = OutputGateway_DriverReady(&gateway, sample->driver, now_ms);
    in->transport_healthy = elapsed_ms > 0U
        && elapsed_ms <= site->s3.safety.service_limit_ms
        && !BoardConsole_HasFault() && bus_healthy(BOARD_CAN_ELBOW)
        && (!dual() || bus_healthy(BOARD_CAN_SHOULDER));
    debounce_speed(in);
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

/* Dual mode needs two distinct post-command position/velocity pairs to
 * establish a quiet shoulder origin. The initial full read proves the five
 * static attributes; subsequent pairs need only refresh position and speed. */
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
            return false; /* A field read before Prepare is only historical. */
        }
        if ((uint32_t)(p->received_ms[i] - p->requested_ms[i])
                > site->s3.shoulder_property_reply_ms) {
            fail_follow(); return false;
        }
    }
    if ((uint32_t)(now_ms - p->requested_ms[GDS_PROPERTY_POSITION])
            > site->s3.safety.feedback_timeout_ms
        || (uint32_t)(now_ms - p->requested_ms[GDS_PROPERTY_VELOCITY])
            > site->s3.safety.feedback_timeout_ms) { return false; }
    float rev = p->value[GDS_PROPERTY_POSITION].real;
    float speed = p->value[GDS_PROPERTY_VELOCITY].real;
    if (!isfinite(rev) || !isfinite(speed)) { fail_follow(); return false; }
    if (fabsf(speed) > site->s3.shoulder_standstill.max_velocity_rev_s) {
        first_seen = false;
        return false; /* Wait for a quiet pair while output is still disabled. */
    }
    uint32_t request_ms = p->requested_ms[GDS_PROPERTY_POSITION];
    if (!first_seen) {
        first_seen = true; first_rev = rev;
        first_sequence = p->sequence; first_request_ms = request_ms;
        return false;
    }
    if (p->sequence == first_sequence) { return false; }
    if (!newer(p->sequence, first_sequence) || !newer(request_ms, first_request_ms)) {
        fail_follow(); return false;
    }
#if !defined(S5_R5_DUAL_FOLLOW)
    if (fabsf(rev - first_rev) > site->s3.shoulder_standstill.span_rev) {
        return false; /* Keep the original physical 0-degree anchor. */
    }
#endif
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
#if !defined(S5_R5_DUAL_FOLLOW)
    ShoulderStandstill_After(&shoulder_still, now_ms);
#endif
    return true;
}

static bool compute_target(const S3SiteSample *sample, uint32_t now_ms,
                           S5FollowEffort *effort)
{
    *effort = (S5FollowEffort){0};
    const bool dual_session = dual();
    /* The composition itself lives in App/control so the MuJoCo bridge runs the
     * same code; this function only gathers the site numbers and the force
     * sample. Values and order are unchanged from the previous inline body. */
    S5AssistComposeConfig config = {
        .dual = dual_session,
        .shoulder_gravity = &site->shoulder_gravity,
        .elbow_gravity = &site->elbow_gravity,
        .elbow_support = &site->elbow_support,
        .shoulder_amplify = &site->shoulder_amplify,
        .elbow_amplify = &site->elbow_amplify,
        .force_assist = &site->force_assist,
        .shoulder_gain = site->shoulder_gain,
        .elbow_motor_kt_nm_per_a = site->elbow_motor_kt_nm_per_a,
        .shoulder_rotor_per_joint = site->s3.safety.joint[0].mapping.rotor_per_joint,
        .shoulder_rotor_cap_nm = site->s3.safety.joint[0].max_motor_command,
        .elbow_current_cap_a = site->s3.safety.joint[1].max_motor_command
    };
    /* Force never gates safety: a missing sample only drops the increment. */
    bool force_valid = false;
    float force_n = 0.0f;
    if (dual_session) {
        Hx711Sample force_sample;
        Hx711_Get(&force_sample);
        /* No assist means no boost: F0 is only valid with the assist on. */
        force_valid = assist_enabled && force_sample.has_sample && !force_sample.saturated
            && (uint32_t)(now_ms - force_sample.received_ms) <= site->force_assist.freshness_ms;
        if (force_valid) { force_valid = Hx711_GetForce(now_ms, &force_n); }
    }
    S5AssistComposeOutput command;
    if (!S5AssistCompose_Update(&config, &sample->joint[0], &sample->joint[1],
                                assist_enabled, force_valid, force_n,
                                &amplify, &elbow_amplify, &force_state, now_ms,
                                &command)) { return false; }
    effort->shoulder_rotor_nm = command.shoulder_rotor_nm;
    effort->elbow_current_a = command.elbow_current_a;
    return true;
}

void S5Follow_Init(const S5SiteProfile *profile)
{
    site = profile;
    selected = profile != NULL;
    memset(&status, 0, sizeof(status));
    assist_enabled = true;
    ForceAssist_Reset(&force_state, 0U);
    status.phase = S5Site_ReadReady(profile) ? S5_FOLLOW_IDLE : S5_FOLLOW_DISABLED;
    status.axes = profile == NULL ? 0U : profile->s3.safety.enabled_axes;
    status.output_bound = status.phase == S5_FOLLOW_IDLE;
    prepare_started_ms = last_elapsed_ms = 0U;
    shoulder_sequence_floor = first_sequence = first_request_ms = 0U;
    zero_shoulder_sequence = zero_shoulder_request_ms = 0U;
    last_sent_sequence[0] = last_sent_sequence[1] = 0U;
    first_rev = 0.0f;
    first_seen = preparation_submitted = false;
    speed_over_count[0] = speed_over_count[1] = 0U;
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

void S5Follow_SetAssist(bool enabled) { assist_enabled = enabled; }
bool S5Follow_AssistEnabled(void) { return assist_enabled; }

bool S5Follow_Prepare(uint32_t now_ms)
{
    if (status.phase != S5_FOLLOW_IDLE || !status.output_bound
        || gateway.state != OUTPUT_SAFE_OFF || !S5Site_ReadReady(site)
        || (dual() && (S3ShoulderService_Get().state != S3_READER_IDLE
            || !S3ShoulderService_Start(now_ms)))) { return false; }
    CanMonitor_ResetElbowStandstill(now_ms);
    if (!(dual() ? S3Feedback_BeginElbowSessionForDual(now_ms)
                 : S3Feedback_BeginElbowSession(now_ms))) {
        fail_follow(); return false;
    }
    prepare_started_ms = now_ms;
    shoulder_sequence_floor = 0U;
    if (dual()) {
        S3ShoulderObservation before = {0};
        (void)S3Feedback_ReadShoulderObservation(&session_site.s3, now_ms, &before);
        shoulder_sequence_floor = before.properties.sequence;
    }
    first_seen = preparation_submitted = false;
    speed_over_count[0] = speed_over_count[1] = 0U;
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
    reset_assist_pause(now_ms);
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
#if !defined(S5_R5_DUAL_FOLLOW)
    if (dual() && status.shoulder_zero_valid
        && (status.phase == S5_FOLLOW_PREPARING || status.phase == S5_FOLLOW_READY)) {
        const ShoulderPropertySnapshot *p = &sample.shoulder_properties;
        bool after_zero = p->valid && newer(p->sequence, zero_shoulder_sequence)
            && newer(p->requested_ms[GDS_PROPERTY_POSITION], zero_shoulder_request_ms);
        ShoulderStandstill_Observe(&shoulder_still, after_zero ? p : NULL, now_ms);
        complete = read_inputs(now_ms, elapsed_ms, &in, &sample);
    }
#endif
    if (!in.transport_healthy) { fail_follow(); return; }
    if (status.phase == S5_FOLLOW_PREPARING && !preparation_submitted) {
        uint32_t elapsed = now_ms - prepare_started_ms;
        if (elapsed >= site->s3.prepare_timeout_ms) { fail_follow(); return; }
        if (!capture_elbow_zero()) { return; }
        if (dual() && !capture_shoulder_zero(&sample, &in, now_ms)) { return; }
        /* R5 uses the two quiet pairs already captured for the RAM zero.
         * Other paths retain their post-zero observer. */
#if !defined(S5_R5_DUAL_FOLLOW)
        if (dual() && !ShoulderStandstill_Read(&shoulder_still, now_ms).stationary) {
            return;
        }
#endif
        complete = read_inputs(now_ms, elapsed_ms, &in, &sample);
        if (!complete || !SafetySupervisor_StopObserved(&session_site.s3.safety, &in)) { return; }
        SafetyReason reason = SafetySupervisor_CheckBeforePrepare(&session_site.s3.safety,
                                                                   &in, now_ms);
        if (reason == SAFETY_RANGE || reason == SAFETY_SPEED) { return; }
        if (reason != SAFETY_OK || !S5Site_ProfileReady(&session_site)
            || !OutputGateway_ConfigureAxesFollow(&gateway, site->send_interval_ms,
                site->session_ms, site->ramp_max_ms, site->release_slew_per_s)) {
            fail_follow(); return;
        }
        if (!OutputGateway_Prepare(&gateway, &in, sample.driver,
            site->s3.prepare_timeout_ms - elapsed, now_ms)
            || (dual() && !S3ShoulderService_RequestZeroProof())) {
            fail_follow(); return;
        }
        preparation_submitted = true;
    }
    if (status.phase == S5_FOLLOW_PREPARING || status.phase == S5_FOLLOW_READY) {
        OutputDriverPhase before = gateway.driver_phase;
        OutputGateway_PollPreparation(&gateway, &in, sample.driver, now_ms);
        if (gateway.state == OUTPUT_FAULT) { fail_follow(); return; }
        if (dual() && before != DRIVER_WAIT_MODE
            && gateway.driver_phase == DRIVER_WAIT_MODE
            && !S3ShoulderService_RequestModeProof()) { fail_follow(); return; }
        if (gateway.state == OUTPUT_READY) { status.phase = S5_FOLLOW_READY; }
        return;
    }
    if (!complete) { fail_follow(); return; }
    if (status.phase == S5_FOLLOW_RUNNING) {
        S5FollowEffort effort;
        if (!compute_target(&sample, now_ms, &effort)) { fail_follow(); return; }
        float gain[SAFETY_JOINT_COUNT];
        assist_gains(&sample, now_ms, gain);
        effort.shoulder_rotor_nm *= gain[0];
        effort.elbow_current_a *= gain[1];
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
        bool complete = read_inputs(now_ms, last_elapsed_ms, &in, &sample);
        SafetyReason reason = complete
            ? SafetySupervisor_Check(&session_site.s3.safety, &in, now_ms) : SAFETY_FEEDBACK;
        /* A person moving the arm while asking to stop must still get the
         * normal ramp. Feedback, transport and driver checks keep their FAULT. */
        bool motion_only = soft_speed_enabled()
            && (reason == SAFETY_SPEED || reason == SAFETY_RANGE);
        if (reason != SAFETY_OK && !motion_only) {
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
    out->force_active = force_state.active;
    out->force_error_n = force_state.last_error_n;
    out->force_triggers = force_state.triggers;
    out->output_allowed = status.phase == S5_FOLLOW_RUNNING
        && (gateway.state == OUTPUT_ARMED || gateway.state == OUTPUT_ACTIVE);
    out->remaining_ms = 0U;
    if (site != NULL && (out->output_allowed || gateway.state == OUTPUT_RAMPING_DOWN)) {
        uint32_t elapsed = BoardRuntime_NowMs() - gateway.armed_ms;
        out->remaining_ms = elapsed < site->session_ms ? site->session_ms - elapsed : 0U;
    }
}
