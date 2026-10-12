/* L4: one cold-start S4 reader and one bounded FOLLOW output session. */
#include "s4_follow.h"
#include "output_can_transport.h"
#include "shoulder_standstill.h"
#include "board_console.h"
#include "board_runtime.h"
#include "s1_config.h"
#include <math.h>
#include <stddef.h>
#include <string.h>

static S4SiteProfile session_site;
static const S4SiteProfile *site;
static S4FollowStatus status;
static OutputGateway gateway;
static ShoulderStandstill prepare_standstill;
static uint32_t prepare_started_ms, last_elapsed_ms;
static uint32_t prepare_sequence_floor, zero_first_sequence, zero_first_request_ms;
static float zero_first_rev;
static bool zero_first_seen;
static bool selected, preparation_submitted;

static bool strictly_after(uint32_t later, uint32_t earlier)
{
    uint32_t step = later - earlier;
    return step != 0U && step < UINT32_C(0x80000000);
}

static void clear_model(void)
{
    status.new_model_sample = false;
    status.model_valid = false;
    status.model_joint_torque_nm = 0.0f;
    status.model_sample_ms = 0U;
}

static void update_observation(uint32_t now_ms)
{
    S3ShoulderObservation sample;
    if (!S3Feedback_ReadShoulderObservation(&site->s3, now_ms, &sample)) { return; }
    status.observation = sample;
    if ((!site->session_zero_on_prepare || status.session_zero_valid)
        && S4Site_ObservationReady(site) && sample.joint.valid
        && sample.properties.sequence != 0U) {
        float model_nm;
        if (GravityCompensation_Compute(&site->gravity, &sample.joint, &model_nm)) {
            status.new_model_sample = sample.properties.sequence != status.model_sequence;
            status.model_joint_torque_nm = model_nm;
            status.model_sequence = sample.properties.sequence;
            status.model_sample_ms = sample.joint.received_ms;
            status.model_valid = true;
        }
    }
}

static bool read_inputs(uint32_t now_ms, uint32_t elapsed_ms,
                        SafetyInputs *in, S3SiteSample *sample)
{
    memset(in, 0, sizeof(*in));
    memset(sample, 0, sizeof(*sample));
    bool complete = S3Feedback_Read(&site->s3, now_ms, sample);
    memcpy(in->joint, sample->joint, sizeof(in->joint));
    in->operator_stop_clear = sample->operator_stop_clear;
    in->independently_stopped = sample->independently_stopped;
    in->feedback_stationary = sample->feedback_stationary;
    in->drivers_ready = OutputGateway_DriverReady(&gateway, sample->driver, now_ms);
    BoardCanStats can;
    in->transport_healthy = elapsed_ms > 0U
        && elapsed_ms <= site->s3.safety.service_limit_ms
        && !BoardConsole_HasFault()
        && BoardCan_GetStats(BOARD_CAN_SHOULDER, &can)
        && can.started && can.start_error == BOARD_CAN_START_OK && !can.bus_off
        && can.errors == 0U && can.dropped == 0U
        && can.fifo_overruns == 0U && can.invalid == 0U;
    return complete;
}

static void fail_follow(void)
{
    if (gateway.state != OUTPUT_FAULT && gateway.state != OUTPUT_SAFE_OFF) {
        OutputGateway_RejectRequest(&gateway);
    }
    S3ReaderState reader = S3ShoulderService_Get().state;
    if (reader != S3_READER_OBSERVING && reader != S3_READER_COMPLETE) {
        (void)S3ShoulderService_Stop();
    }
    status.reader = S3ShoulderService_Get();
    status.phase = S4_FOLLOW_FAULT;
    status.session_zero_valid = false;
    clear_model();
}

/* An explicit followprepare fences every read. Two distinct complete cycles
 * must agree on a stationary vertical origin before output preparation. */
static bool capture_session_zero(const S3SiteSample *sample, const SafetyInputs *in,
                                 uint32_t now_ms)
{
    const ShoulderPropertySnapshot *p = &sample->shoulder_properties;
    if (!p->valid || p->sequence == 0U
        || !strictly_after(p->sequence, prepare_sequence_floor)) { return false; }
    if (gateway.state != OUTPUT_SAFE_OFF || !in->transport_healthy
        || !status.observation.heartbeat_healthy || !sample->driver[0].valid
        || sample->driver[0].axis_state != 1U
        || !sample->driver[0].calibration_verified || !sample->driver[0].zero_verified) {
        fail_follow(); return false;
    }
    for (unsigned i = 0U; i < GDS_PROPERTY_COUNT; i++) {
        uint32_t request_ms = p->requested_ms[i], received_ms = p->received_ms[i];
        if (!strictly_after(request_ms, prepare_started_ms)
            || !strictly_after(received_ms, prepare_started_ms)
            || (uint32_t)(received_ms - request_ms) > site->s3.shoulder_property_reply_ms
            || (uint32_t)(now_ms - request_ms) > site->s3.safety.feedback_timeout_ms
            || (uint32_t)(now_ms - received_ms) > site->s3.safety.feedback_timeout_ms) {
            fail_follow(); return false;
        }
    }
    float position_rev = p->value[GDS_PROPERTY_POSITION].real;
    float velocity_rev_s = p->value[GDS_PROPERTY_VELOCITY].real;
    const ShoulderStandstillConfig *still = &site->s3.shoulder_standstill;
    if (!isfinite(position_rev) || !isfinite(velocity_rev_s)
        || fabsf(velocity_rev_s) > still->max_velocity_rev_s) {
        fail_follow(); return false;
    }
    if (!zero_first_seen) {
        zero_first_seen = true;
        zero_first_rev = position_rev;
        zero_first_sequence = p->sequence;
        zero_first_request_ms = p->requested_ms[GDS_PROPERTY_POSITION];
        return false;
    }
    if (p->sequence == zero_first_sequence) { return false; }
    if (!strictly_after(p->sequence, zero_first_sequence)
        || !strictly_after(p->requested_ms[GDS_PROPERTY_POSITION], zero_first_request_ms)
        || (uint32_t)(now_ms - zero_first_request_ms) > site->s3.safety.feedback_timeout_ms
        || fabsf(position_rev - zero_first_rev) > still->span_rev) {
        fail_follow(); return false;
    }
    if ((uint32_t)(p->requested_ms[GDS_PROPERTY_POSITION] - zero_first_request_ms)
        < site->session_zero_dwell_ms) { return false; }
    float zero_rad = position_rev * 6.2831853071795864769f;
    if (!isfinite(zero_rad)) { fail_follow(); return false; }
    session_site.s3.safety.joint[0].mapping.rotor_zero_rad = zero_rad;
    session_site.s3.rotor_reference_confirmed[0] = true;
    session_site.gravity.gravity_zero_rad = 0.0f;
    if (!S4Site_ProfileReady(site)
        || !OutputGateway_SetFollowReference(&gateway, zero_rad)
        || !OutputGateway_ConfigureFollow(&gateway, site->send_interval_ms,
            site->session_ms, site->ramp_max_ms, site->release_slew_nm_s)) {
        fail_follow(); return false;
    }
    status.session_zero_rev = position_rev;
    status.session_zero_sample_ms = p->requested_ms[GDS_PROPERTY_POSITION];
    status.session_zero_sequence = p->sequence;
    status.session_zero_valid = true;
    /* The vertical zero cycle cannot count toward in-range settling. Only
     * property requests made after this event may prepare the output. */
    ShoulderStandstill_After(&prepare_standstill, now_ms);
    update_observation(now_ms);
    return true;
}

void S4Follow_Init(const S4SiteProfile *profile)
{
    if (profile != NULL && profile->session_zero_on_prepare) {
        session_site = *profile;
        site = &session_site;
    } else { site = profile; }
    selected = site != NULL;
    preparation_submitted = false;
    zero_first_seen = false;
    prepare_sequence_floor = zero_first_sequence = zero_first_request_ms = 0U;
    zero_first_rev = 0.0f;
    prepare_started_ms = last_elapsed_ms = 0U;
    memset(&status, 0, sizeof(status));
    status.phase = S4Site_ReadReady(site) ? S4_FOLLOW_IDLE : S4_FOLLOW_DISABLED;
    status.reader = S3ShoulderService_Get();
    S3Feedback_Init();
    OutputTransport port = {0};
    status.output_bound = site != NULL && (site->session_zero_on_prepare
        ? S4Site_SessionZeroReady(site) : S4Site_ProfileReady(site));
    if (status.output_bound) { port = OutputCanTransport_Create(); }
    OutputGateway_Init(&gateway, site == NULL ? NULL : &site->s3.safety,
                       status.output_bound ? &port : NULL,
                       site == NULL ? 0U : site->s3.shoulder_node,
                       site == NULL ? 0U : site->s3.elbow_id);
    ShoulderStandstill_Init(&prepare_standstill,
                            site == NULL ? NULL : &site->s3.shoulder_standstill);
}

bool S4Follow_IsSelected(void) { return selected; }

bool S4Follow_Observe(uint32_t now_ms)
{
    if (status.phase != S4_FOLLOW_IDLE || site == NULL
        || S3ShoulderService_Get().state != S3_READER_IDLE
        || !S3ShoulderService_Start(now_ms)) { return false; }
    status.phase = S4_FOLLOW_OBSERVING;
    status.reader = S3ShoulderService_Get();
    return true;
}

bool S4Follow_Prepare(uint32_t now_ms)
{
    if (status.phase != S4_FOLLOW_IDLE || !status.output_bound
        || S3ShoulderService_Get().state != S3_READER_IDLE
        || (site->session_zero_on_prepare
            ? !S4Site_SessionZeroReady(site) || gateway.state != OUTPUT_SAFE_OFF
            : !S4Site_ProfileReady(site) || !OutputGateway_ConfigureFollow(&gateway,
                site->send_interval_ms, site->session_ms, site->ramp_max_ms,
                site->release_slew_nm_s))
        || !S3ShoulderService_Start(now_ms)) { return false; }
    status.phase = S4_FOLLOW_PREPARING;
    status.reader = S3ShoulderService_Get();
    prepare_started_ms = now_ms;
    prepare_sequence_floor = status.observation.properties.sequence;
    zero_first_seen = false;
    preparation_submitted = false;
    ShoulderStandstill_Init(&prepare_standstill, &site->s3.shoulder_standstill);
    return true;
}

bool S4Follow_Start(uint32_t now_ms)
{
    if (status.phase != S4_FOLLOW_READY || gateway.state != OUTPUT_READY
        || !status.output_bound || !S4Site_ProfileReady(site)
        || (site->session_zero_on_prepare && !status.session_zero_valid)) { return false; }
    SafetyInputs in; S3SiteSample sample;
    if (!read_inputs(now_ms, last_elapsed_ms, &in, &sample) || !in.drivers_ready
        || SafetySupervisor_Check(&site->s3.safety, &in, now_ms) != SAFETY_OK
        || !OutputGateway_Arm(&gateway, &in, now_ms)) { return false; }
    status.phase = S4_FOLLOW_RUNNING;
    status.warning_due = status.warning_queued = status.warning_failed = false;
    return true;
}

static void try_warning(uint32_t now_ms)
{
    if (status.warning_queued || status.warning_failed || status.phase != S4_FOLLOW_RUNNING) { return; }
    uint32_t ramp_in = site->session_ms - site->ramp_max_ms;
    if (ramp_in <= S4_WARNING_LEAD_MS) { return; } /* Start reply is the warning. */
    uint32_t elapsed = now_ms - gateway.armed_ms;
    if (elapsed < ramp_in - S4_WARNING_LEAD_MS) { return; }
    status.warning_due = true;
    if (elapsed >= ramp_in) { status.warning_failed = true; return; }
    static const char warning[] = "WARN FOLLOW ending soon; support arm; torque will ramp down\r\n";
    if (BoardConsole_TxFree() >= S1_REPLY_SIZE + sizeof(warning) - 1U
        && BoardConsole_Write((const uint8_t *)warning, sizeof(warning) - 1U)) {
        status.warning_queued = true;
    }
}

void S4Follow_Poll(uint32_t now_ms, uint32_t elapsed_ms)
{
    clear_model();
    last_elapsed_ms = elapsed_ms;
    if (status.phase == S4_FOLLOW_FAULT) {
        if (S3ShoulderService_Get().state == S3_READER_OBSERVING) {
            S3ShoulderService_Poll(now_ms);
            status.reader = S3ShoulderService_Get();
        }
        return;
    }
    if (status.phase != S4_FOLLOW_OBSERVING && status.phase != S4_FOLLOW_PREPARING
        && status.phase != S4_FOLLOW_READY && status.phase != S4_FOLLOW_RUNNING
        && status.phase != S4_FOLLOW_RAMPING_DOWN && status.phase != S4_FOLLOW_STOPPING) { return; }
    bool reader_only = status.phase == S4_FOLLOW_OBSERVING
        || (status.phase == S4_FOLLOW_STOPPING
            && S3ShoulderService_Get().state == S3_READER_CANCELLING);
    if (site == NULL
        || (reader_only
            ? elapsed_ms >= site->s3.safety.feedback_timeout_ms
            : (elapsed_ms == 0U || elapsed_ms > site->s3.safety.service_limit_ms))) {
        fail_follow(); return;
    }
    S3ShoulderService_Poll(now_ms);
    status.reader = S3ShoulderService_Get();
    if (status.phase == S4_FOLLOW_STOPPING) {
        if (status.reader.state == S3_READER_COMPLETE
            || status.reader.state == S3_READER_STOPPED) { status.phase = S4_FOLLOW_STOPPED; }
        else if (status.reader.state == S3_READER_FAULT) { status.phase = S4_FOLLOW_FAULT; }
        if (status.phase != S4_FOLLOW_STOPPING) { status.session_zero_valid = false; }
        return;
    }
    if (status.reader.state != S3_READER_ACTIVE) { fail_follow(); return; }
    update_observation(now_ms);
    if (status.phase == S4_FOLLOW_OBSERVING) {
        if (status.observation.heartbeat_healthy) { return; }
        fail_follow(); return;
    }
    SafetyInputs in; S3SiteSample sample;
    bool complete = read_inputs(now_ms, elapsed_ms, &in, &sample);
    if (status.phase == S4_FOLLOW_PREPARING && !preparation_submitted) {
        uint32_t prepare_elapsed = now_ms - prepare_started_ms;
        if (!in.transport_healthy || prepare_elapsed >= site->s3.prepare_timeout_ms) {
            fail_follow(); return;
        }
        if (site->session_zero_on_prepare && !status.session_zero_valid) {
            if (!capture_session_zero(&sample, &in, now_ms)) { return; }
            complete = read_inputs(now_ms, elapsed_ms, &in, &sample);
        }
        if (!complete) {
            if (site->session_zero_on_prepare) {
                ShoulderStandstill_Observe(&prepare_standstill, NULL, now_ms);
            }
            return;
        }
        if (site->session_zero_on_prepare) {
            /* The vertical reference may sit beyond the one-sided motion
             * envelope. No motor frame is permitted there. Wait for NEW
             * in-range feedback to remain stationary before preparation. */
            if (!sample.shoulder_properties.valid
                || !strictly_after(sample.shoulder_properties.sequence,
                    status.session_zero_sequence)
                || !strictly_after(sample.shoulder_properties.requested_ms[GDS_PROPERTY_POSITION],
                    status.session_zero_sample_ms)) {
                ShoulderStandstill_Observe(&prepare_standstill, NULL, now_ms);
                return;
            }
            SafetyReason ready = SafetySupervisor_CheckBeforePrepare(&site->s3.safety,
                                                                      &in, now_ms);
            if (ready == SAFETY_RANGE || ready == SAFETY_SPEED) {
                ShoulderStandstill_Observe(&prepare_standstill, NULL, now_ms);
                return;
            }
            if (ready != SAFETY_OK) { fail_follow(); return; }
            ShoulderStandstill_Observe(&prepare_standstill, &sample.shoulder_properties, now_ms);
            if (!ShoulderStandstill_Read(&prepare_standstill, now_ms).stationary) { return; }
        }
        if (!OutputGateway_Prepare(&gateway, &in, sample.driver,
            site->s3.prepare_timeout_ms - prepare_elapsed, now_ms)) { fail_follow(); return; }
        preparation_submitted = true;
    }
    if (status.phase == S4_FOLLOW_PREPARING || status.phase == S4_FOLLOW_READY) {
        OutputGateway_PollPreparation(&gateway, &in, sample.driver, now_ms);
        if (gateway.state == OUTPUT_FAULT) { fail_follow(); return; }
        if (gateway.state == OUTPUT_READY) { status.phase = S4_FOLLOW_READY; }
        return;
    }
    if (status.phase == S4_FOLLOW_RUNNING) {
        if (!complete || !status.model_valid) { fail_follow(); return; }
        /* A cycle begun before manual Arm can finish afterward. Wait for its
         * position request to be strictly newer than this permit. */
        uint32_t after_arm = status.model_sample_ms - gateway.armed_ms;
        if (status.new_model_sample && after_arm != 0U
            && after_arm < UINT32_C(0x80000000)) {
            float effort_nm = status.model_joint_torque_nm * site->assist_gain
                / site->s3.safety.joint[0].mapping.rotor_per_joint;
            if (!isfinite(effort_nm)) { fail_follow(); return; }
            if (!OutputGateway_RefreshFollow(&gateway, effort_nm,
                status.model_sample_ms, status.model_sequence, now_ms)
                && gateway.state != OUTPUT_RAMPING_DOWN) { fail_follow(); return; }
        }
        try_warning(now_ms);
    }
    OutputGateway_Step(&gateway, &in, now_ms);
    if (gateway.state == OUTPUT_FAULT) { fail_follow(); return; }
    if (gateway.state == OUTPUT_RAMPING_DOWN) {
        if (status.warning_due && !status.warning_queued) { status.warning_failed = true; }
        status.phase = S4_FOLLOW_RAMPING_DOWN;
    } else if (gateway.state == OUTPUT_SAFE_OFF) {
        status.phase = S4_FOLLOW_STOPPING;
        status.session_zero_valid = false;
        status.reader = S3ShoulderService_Get();
    }
}

bool S4Follow_Cancel(void)
{
    uint32_t now_ms = BoardRuntime_NowMs();
    status.session_zero_valid = false;
    if (status.phase == S4_FOLLOW_RUNNING) {
        SafetyInputs in; S3SiteSample sample;
        if (!read_inputs(now_ms, last_elapsed_ms, &in, &sample)
            || SafetySupervisor_Check(&site->s3.safety, &in, now_ms) != SAFETY_OK) {
            fail_follow(); return false;
        }
        bool ramp = OutputGateway_BeginNormalStop(&gateway, OUTPUT_REASON_CANCEL, now_ms);
        if (ramp) {
            status.phase = S4_FOLLOW_RAMPING_DOWN;
            if (status.warning_due && !status.warning_queued) { status.warning_failed = true; }
            return true;
        }
        fail_follow(); return false;
    }
    if (status.phase == S4_FOLLOW_PREPARING && !preparation_submitted
        && gateway.state == OUTPUT_SAFE_OFF) {
        status.phase = S3ShoulderService_BeginReadOnlyStop(now_ms)
            ? S4_FOLLOW_STOPPING : S4_FOLLOW_FAULT;
    } else if (status.phase == S4_FOLLOW_PREPARING || status.phase == S4_FOLLOW_READY) {
        OutputGateway_Cancel(&gateway); /* Zero-only preparation has no assist to ramp. */
        if (gateway.state == OUTPUT_FAULT) { fail_follow(); return false; }
        status.reader = S3ShoulderService_Get();
        status.phase = status.reader.state == S3_READER_OBSERVING
            ? S4_FOLLOW_STOPPING : S4_FOLLOW_STOPPED;
        if (status.phase == S4_FOLLOW_STOPPED && !S3ShoulderService_Stop()) { fail_follow(); }
    } else if (status.phase == S4_FOLLOW_OBSERVING) {
        status.phase = S3ShoulderService_BeginReadOnlyStop(now_ms)
            ? S4_FOLLOW_STOPPING : S4_FOLLOW_FAULT;
    } else if (status.phase == S4_FOLLOW_IDLE) {
        status.phase = S4_FOLLOW_STOPPED;
    }
    clear_model();
    status.reader = S3ShoulderService_Get();
    return false;
}

void S4Follow_GetStatus(S4FollowStatus *out)
{
    if (out == NULL) { return; }
    S4FollowStatus result = status;
    if (site != NULL && site->session_zero_on_prepare && !result.session_zero_valid) {
        result.observation.joint.valid = false;
        result.model_valid = false;
    }
    result.reader = S3ShoulderService_Get();
    result.output_state = gateway.state;
    result.output_reason = gateway.reason;
    result.safety_reason = gateway.safety_reason;
    result.stop_attempted = gateway.stop_attempted;
    result.stop_accepted = gateway.stop_accepted;
    result.stop_delivery_failed = gateway.stop_delivery_failed;
    result.ramping_down = gateway.state == OUTPUT_RAMPING_DOWN;
    result.output_allowed = status.phase == S4_FOLLOW_RUNNING
        && (gateway.state == OUTPUT_ARMED || gateway.state == OUTPUT_ACTIVE);
    result.motor_effort_active = gateway.state == OUTPUT_ACTIVE
        || gateway.state == OUTPUT_RAMPING_DOWN;
    uint32_t now_ms = BoardRuntime_NowMs();
    result.sample_asof_ms = now_ms;
    result.sample_age_ms = 0U;
    result.sample_fresh_now = false;
    if (site != NULL && result.observation.properties.sequence != 0U) {
        uint32_t sample_ms = result.observation.properties.requested_ms[GDS_PROPERTY_POSITION];
        uint32_t heartbeat_age_ms = now_ms - result.observation.heartbeat.heartbeat_ms;
        result.sample_age_ms = now_ms - sample_ms;
        bool reading = result.phase == S4_FOLLOW_OBSERVING
            || result.phase == S4_FOLLOW_PREPARING || result.phase == S4_FOLLOW_READY
            || result.phase == S4_FOLLOW_RUNNING || result.phase == S4_FOLLOW_RAMPING_DOWN;
        result.sample_fresh_now = reading && result.reader.state == S3_READER_ACTIVE
            && result.observation.properties.valid && result.observation.properties_fresh
            && result.observation.heartbeat_healthy
            && result.sample_age_ms <= site->s3.safety.feedback_timeout_ms
            && heartbeat_age_ms <= site->s3.safety.feedback_timeout_ms;
    }
    result.remaining_ms = result.ramp_in_ms = result.ramp_remaining_ms = 0U;
    if (site != NULL && (result.output_allowed || result.ramping_down)) {
        uint32_t elapsed = now_ms - gateway.armed_ms;
        result.remaining_ms = elapsed < site->session_ms ? site->session_ms - elapsed : 0U;
        uint32_t until_ramp = site->session_ms - site->ramp_max_ms;
        if (result.output_allowed && elapsed < until_ramp) {
            result.ramp_in_ms = until_ramp - elapsed;
        }
        if (result.ramping_down) {
            uint32_t ramp_elapsed = now_ms - gateway.ramp_begin_ms;
            uint32_t ramp_budget = gateway.ramp_deadline_ms - gateway.ramp_begin_ms;
            result.ramp_remaining_ms = ramp_elapsed < ramp_budget
                ? ramp_budget - ramp_elapsed : 0U;
        }
    }
    *out = result;
}
