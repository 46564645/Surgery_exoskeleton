/* L4: ExoKit replace/cancel semantics without dynamic actions; unique gateway. */
#include "output_gateway.h"
#include <math.h>
#include <stddef.h>
#include <string.h>

static bool transport_ready(const OutputGateway *g)
{
    return g->transport.purge != NULL && g->transport.submit != NULL
        && g->shoulder_node <= 63U && g->elbow_id >= 1U && g->elbow_id <= 8U;
}

static bool supervised_bench(SafetyStopPolicy policy)
{
    return policy == SAFETY_STOP_SUPERVISED_C620_BENCH
        || policy == SAFETY_STOP_SUPERVISED_SHOULDER_BENCH
        || policy == SAFETY_STOP_SUPERVISED_SHOULDER_FOLLOW
        || policy == SAFETY_STOP_SUPERVISED_ELBOW_FOLLOW
        || policy == SAFETY_STOP_SUPERVISED_DUAL_FOLLOW;
}

static bool s4_follow(const OutputGateway *g)
{ return g->config.stop_policy == SAFETY_STOP_SUPERVISED_SHOULDER_FOLLOW; }
static bool axes_follow(const OutputGateway *g)
{ return g->config.stop_policy == SAFETY_STOP_SUPERVISED_ELBOW_FOLLOW
    || g->config.stop_policy == SAFETY_STOP_SUPERVISED_DUAL_FOLLOW; }
static bool follow(const OutputGateway *g)
{ return s4_follow(g) || axes_follow(g); }

static bool duration(uint32_t ms)
{ return ms > 0U && ms < UINT32_C(0x80000000); }

static bool s3_elbow_async(const OutputGateway *g)
{
    return g->config.stop_policy == SAFETY_STOP_SUPERVISED_C620_BENCH
        && g->config.enabled_axes == SAFETY_ELBOW_AXES
        && g->transport.purge_pending != NULL && g->transport.pending != NULL
        && g->transport.drained != NULL && g->transport.failed != NULL;
}

static void s3_elbow_stop_failed(OutputGateway *g)
{
    g->stop_pending_purge = false;
    g->stop_pending_tx = false;
    g->stop_delivery_failed = true;
    g->state = OUTPUT_FAULT;
    g->reason = OUTPUT_REASON_TRANSPORT;
}

static void progress_s3_elbow_stop(OutputGateway *g, uint32_t now_ms)
{
    uint8_t axes = g->config.enabled_axes;
    if (!g->stop_pending_purge && !g->stop_pending_tx) { return; }
    if (g->stop_pending_purge) {
        if (!g->transport.purge(g->transport.context, axes)) {
            if (!g->transport.purge_pending(g->transport.context, axes)
                || (uint32_t)(now_ms - g->stop_started_ms) > g->config.service_limit_ms) {
                s3_elbow_stop_failed(g);
            }
            return;
        }
        g->stop_pending_purge = false;
        g->purge_ok = true;
        BoardCanFrame frames[2] = {0};
        if (!MotorCommand_C620(g->elbow_id, 0.0f, &frames[1])
            || !g->transport.submit(g->transport.context, frames, axes, true)) {
            s3_elbow_stop_failed(g);
            return;
        }
        g->stop_accepted = true;
        g->stop_pending_tx = true;
        return;
    }
    if (g->transport.failed(g->transport.context, axes)) {
        s3_elbow_stop_failed(g);
    } else if (g->transport.drained(g->transport.context, axes)) {
        g->stop_pending_tx = false;
    } else if ((uint32_t)(now_ms - g->stop_started_ms) > g->config.service_limit_ms) {
        s3_elbow_stop_failed(g);
    }
}

static void withdraw(OutputGateway *g, OutputReason reason, bool fault)
{
    bool was_live = g->state == OUTPUT_ARMED || g->state == OUTPUT_ACTIVE
        || g->state == OUTPUT_PREPARING || g->state == OUTPUT_READY
        || g->state == OUTPUT_RAMPING_DOWN;
    /* Revoke software permission BEFORE touching any transport state. */
    g->state = fault ? OUTPUT_FAULT : OUTPUT_SAFE_OFF;
    g->reason = reason;
    g->request_valid = false;
    g->driver_phase = DRIVER_NOT_PREPARED;
    g->target = 0.0f;
    g->applied = 0.0f;
    g->axes_target = (S5FollowEffort){0};
    g->axes_applied = (S5FollowEffort){0};
    g->follow_target_pending = false;
    g->follow_zero_pending = false;
    g->follow_output_pending = false;
    if (!was_live) { return; }
    if (s3_elbow_async(g)) {
        g->stop_attempted = true;
        g->stop_accepted = false;
        g->purge_ok = false;
        g->stop_pending_purge = true;
        g->stop_pending_tx = false;
        g->stop_started_ms = g->last_step_ms;
        progress_s3_elbow_stop(g, g->last_step_ms);
        return;
    }
    BoardCanFrame frames[2] = {0};
    g->stop_attempted = true;
    g->purge_ok = g->transport.purge(g->transport.context, g->config.enabled_axes);
    bool encoded = (!SafetySupervisor_AxisEnabled(&g->config, 0U)
        || MotorCommand_GdsV22Idle(g->shoulder_node, &frames[0]))
        && (!SafetySupervisor_AxisEnabled(&g->config, 1U)
        || MotorCommand_C620(g->elbow_id, 0.0f, &frames[1]));
    g->stop_accepted = encoded && g->transport.submit(g->transport.context, frames, g->config.enabled_axes, true);
    if (!g->purge_ok || !g->stop_accepted) {
        g->stop_delivery_failed = true;
        g->state = OUTPUT_FAULT;
        g->reason = OUTPUT_REASON_TRANSPORT;
    }
}

void OutputGateway_Init(OutputGateway *g, const SafetyConfig *config,
                        const OutputTransport *transport, uint8_t shoulder_node, uint8_t elbow_id)
{
    /* Cold boot/reset only. For live reconfiguration call Cancel first. */
    memset(g, 0, sizeof(*g));
    if (config != NULL) { g->config = *config; }
    if (transport != NULL) { g->transport = *transport; }
    g->shoulder_node = shoulder_node;
    g->elbow_id = elbow_id;
    g->state = OUTPUT_SAFE_OFF;
    g->reason = OUTPUT_REASON_CONFIG;
}

bool OutputGateway_SetFollowReference(OutputGateway *g, float rotor_zero_rad)
{
    if (g == NULL || !(s4_follow(g)
        || g->config.stop_policy == SAFETY_STOP_SUPERVISED_DUAL_FOLLOW)
        || g->state != OUTPUT_SAFE_OFF
        || g->reason != OUTPUT_REASON_CONFIG || g->follow_reference_set
        || g->follow_configured || g->follow_session_used || g->stop_attempted
        || g->stop_delivery_failed || !transport_ready(g)
        || !isfinite(rotor_zero_rad)) { return false; }
    SafetyConfig updated = g->config;
    updated.joint[0].mapping.rotor_zero_rad = rotor_zero_rad;
    if (!SafetySupervisor_ConfigReady(&updated)) { return false; }
    g->config = updated;
    g->follow_reference_set = true;
    return true;
}

bool OutputGateway_SetElbowFollowReference(OutputGateway *g, float relative_zero_rad)
{
    if (g == NULL || !axes_follow(g) || g->state != OUTPUT_SAFE_OFF
        || g->reason != OUTPUT_REASON_CONFIG || g->elbow_reference_set
        || g->follow_configured || g->follow_session_used || g->stop_attempted
        || g->stop_delivery_failed || !transport_ready(g)
        || !isfinite(relative_zero_rad) || relative_zero_rad != 0.0f
        || g->config.joint[1].mapping.rotor_zero_rad != 0.0f) { return false; }
    /* The S5 elbow mapper has already expressed JointState relative to its
     * RAM session zero. Gateway mapping must remain that relative coordinate. */
    if (!SafetySupervisor_ConfigReady(&g->config)) { return false; }
    g->elbow_reference_set = true;
    return true;
}

bool OutputGateway_Arm(OutputGateway *g, const SafetyInputs *inputs, uint32_t now_ms)
{
    if ((g->state != OUTPUT_SAFE_OFF && g->state != OUTPUT_READY)
        || g->stop_pending_purge || g->stop_pending_tx) { return false; }
    if ((g->config.stop_policy == SAFETY_STOP_SUPERVISED_SHOULDER_BENCH && g->shoulder_attempted)
        || (follow(g) && (g->follow_session_used || !g->follow_configured))) {
        return false;
    }
    if (supervised_bench(g->config.stop_policy) && g->state != OUTPUT_READY) {
        return false; /* Bench always starts with an explicit zero preparation. */
    }
    if (!transport_ready(g) || !SafetySupervisor_ConfigReady(&g->config)) {
        g->reason = OUTPUT_REASON_CONFIG;
        return false;
    }
    if (follow(g) && (uint32_t)(now_ms - g->last_step_ms) > g->config.service_limit_ms) {
        withdraw(g, OUTPUT_REASON_SERVICE, true);
        return false;
    }
    /* A command between service polls cannot revive an expired READY phase. */
    if (g->state == OUTPUT_READY
        && (uint32_t)(now_ms - g->prepare_ms) >= g->prepare_timeout_ms) {
        withdraw(g, OUTPUT_REASON_EXPIRED, true);
        return false;
    }
    if (g->state == OUTPUT_READY && SafetySupervisor_AxisEnabled(&g->config, 1U)
        && !SafetySupervisor_StopObserved(&g->config, inputs)) {
        g->reason = OUTPUT_REASON_HEALTH; return false;
    }
    g->safety_reason = SafetySupervisor_Check(&g->config, inputs, now_ms);
    if (g->safety_reason != SAFETY_OK) { g->reason = OUTPUT_REASON_HEALTH; return false; }
    /* Fresh manual permit cannot inherit any transport backlog. */
    if (!g->transport.purge(g->transport.context, g->config.enabled_axes)) {
        g->state = OUTPUT_FAULT;
        g->stop_delivery_failed = true;
        g->reason = OUTPUT_REASON_TRANSPORT;
        return false;
    }
    g->state = OUTPUT_ARMED;
    g->reason = OUTPUT_REASON_NONE;
    g->last_step_ms = now_ms;
    g->armed_ms = now_ms;
    g->request_valid = false;
    g->applied = 0.0f;
    g->axes_applied = (S5FollowEffort){0};
    g->axes_target = (S5FollowEffort){0};
    g->stop_attempted = false;
    g->stop_accepted = false;
    g->purge_ok = true;
    if (follow(g)) {
        g->follow_session_used = true;
        g->session_deadline_ms = now_ms + g->session_ms;
        g->ramp_start_ms = now_ms + g->session_ms - g->ramp_max_ms;
        g->last_sent_ms = now_ms;
        g->follow_sequence_valid = false;
        g->follow_target_pending = false;
        g->follow_zero_pending = false;
        g->follow_output_pending = false;
        for (unsigned axis = 0U; axis < SAFETY_JOINT_COUNT; axis++) {
            g->axes_sequence_valid[axis] = false;
            g->axes_last_sequence[axis] = 0U;
            g->axes_last_sample_ms[axis] = 0U;
        }
    }
    return true;
}

bool OutputGateway_Replace(OutputGateway *g, unsigned joint, float value,
                           uint32_t ttl_ms, uint32_t now_ms)
{
    if (follow(g)) { return false; } /* FOLLOW never inherits the S3 one-shot API. */
    if (g->state != OUTPUT_ARMED && g->state != OUTPUT_ACTIVE) { return false; }
    if ((supervised_bench(g->config.stop_policy) && g->state == OUTPUT_ACTIVE)
        || !SafetySupervisor_AxisEnabled(&g->config, joint) || !isfinite(value) || ttl_ms == 0U
        || ttl_ms > g->config.max_request_ms
        || fabsf(value) > g->config.joint[joint].max_motor_command
        || (g->request_valid && joint != g->selected_joint)) {
        withdraw(g, OUTPUT_REASON_REQUEST, true);
        return false;
    }
    /* The old request expires before replacement too; delayed commands cannot
     * revive it without another manual arm, even when Step has not run yet. */
    if ((uint32_t)(now_ms - g->last_step_ms) > g->config.service_limit_ms) {
        withdraw(g, OUTPUT_REASON_SERVICE, true);
        return false;
    }
    if ((!g->request_valid && (uint32_t)(now_ms - g->armed_ms) >= g->config.max_request_ms)
        || (g->request_valid && (uint32_t)(now_ms - g->request_ms) >= g->ttl_ms)) {
        withdraw(g, OUTPUT_REASON_EXPIRED, true);
        return false;
    }
    if (!g->transport.purge(g->transport.context, g->config.enabled_axes)) {
        withdraw(g, OUTPUT_REASON_TRANSPORT, true);
        return false;
    }
    g->selected_joint = (uint8_t)joint;
    g->target = value;
    g->request_ms = now_ms;
    g->ttl_ms = ttl_ms;
    g->request_valid = true;
    if (g->config.stop_policy == SAFETY_STOP_SUPERVISED_SHOULDER_BENCH) {
        g->shoulder_attempted = true; /* Accepted intent; not a claim of wire delivery. */
    }
    g->state = OUTPUT_ACTIVE;
    return true;
}

bool OutputGateway_ConfigureFollow(OutputGateway *g, uint32_t send_interval_ms,
                                   uint32_t session_ms, uint32_t ramp_max_ms,
                                   float release_slew_nm_s)
{
    if (g == NULL || !s4_follow(g) || g->state != OUTPUT_SAFE_OFF || g->follow_session_used
        || g->transport.drained == NULL || g->transport.failed == NULL
        || !SafetySupervisor_ConfigReady(&g->config)
        || !duration(send_interval_ms) || !duration(session_ms) || !duration(ramp_max_ms)
        || session_ms <= ramp_max_ms || !isfinite(release_slew_nm_s)
        || release_slew_nm_s <= 0.0f
        || send_interval_ms >= g->config.service_limit_ms
        || send_interval_ms >= g->config.max_request_ms
        || (uint64_t)ramp_max_ms <= 2U * (uint64_t)send_interval_ms) { return false; }
    /* Reserve two send slots for zero Nm and IDLE. Site checks the additional
     * measured transport budget before binding this gateway in production. */
    uint64_t available_ms = ramp_max_ms - 2U * (uint64_t)send_interval_ms;
    if ((double)available_ms * (double)release_slew_nm_s / 1000.0
        < (double)g->config.joint[0].max_motor_command) { return false; }
    g->send_interval_ms = send_interval_ms;
    g->session_ms = session_ms;
    g->ramp_max_ms = ramp_max_ms;
    g->release_slew_nm_s = release_slew_nm_s;
    g->follow_configured = true;
    return true;
}

bool OutputGateway_ConfigureAxesFollow(OutputGateway *g, uint32_t send_interval_ms,
                                       uint32_t session_ms, uint32_t ramp_max_ms,
                                       S5FollowEffort release_slew_per_s)
{
    if (g == NULL || !axes_follow(g) || g->state != OUTPUT_SAFE_OFF || g->follow_session_used
        || g->transport.drained == NULL || g->transport.failed == NULL
        || !g->elbow_reference_set
        || (SafetySupervisor_AxisEnabled(&g->config, 0U) && !g->follow_reference_set)
        || !SafetySupervisor_ConfigReady(&g->config)
        || !duration(send_interval_ms) || !duration(session_ms) || !duration(ramp_max_ms)
        || session_ms <= ramp_max_ms
        || send_interval_ms >= g->config.service_limit_ms
        || send_interval_ms >= g->config.max_request_ms
        || (uint64_t)ramp_max_ms <= 2U * (uint64_t)send_interval_ms) { return false; }
    const float slope[SAFETY_JOINT_COUNT] = {
        release_slew_per_s.shoulder_rotor_nm, release_slew_per_s.elbow_current_a
    };
    uint64_t available_ms = ramp_max_ms - 2U * (uint64_t)send_interval_ms;
    for (unsigned axis = 0U; axis < SAFETY_JOINT_COUNT; axis++) {
        if (!isfinite(slope[axis])) { return false; }
        if (!SafetySupervisor_AxisEnabled(&g->config, axis)) {
            if (slope[axis] != 0.0f) { return false; }
            continue;
        }
        if (slope[axis] <= 0.0f || (double)available_ms * (double)slope[axis] / 1000.0
            < (double)g->config.joint[axis].max_motor_command) { return false; }
    }
    g->send_interval_ms = send_interval_ms;
    g->session_ms = session_ms;
    g->ramp_max_ms = ramp_max_ms;
    g->axes_release_slew_per_s = release_slew_per_s;
    g->follow_configured = true;
    return true;
}

/* A successful HAL enqueue is not proof that bxCAN sent the frame. A FOLLOW
 * output remains owned until TXOK, explicit failure, or a bounded timeout.
 * The caller may wait while pending, but must never purge it as a refresh. */
static bool follow_delivery_ready(OutputGateway *g, uint32_t now_ms, bool *ready)
{
    *ready = !g->follow_output_pending;
    if (*ready) { return true; }
    if (g->transport.failed(g->transport.context, g->config.enabled_axes)) {
        withdraw(g, OUTPUT_REASON_TRANSPORT, true);
        return false;
    }
    if (g->transport.drained(g->transport.context, g->config.enabled_axes)) {
        g->follow_output_pending = false;
        *ready = true;
        return true;
    }
    if ((uint32_t)(now_ms - g->follow_output_submit_ms) >= g->config.service_limit_ms) {
        withdraw(g, OUTPUT_REASON_TRANSPORT, true);
        return false;
    }
    return true;
}

bool OutputGateway_BeginNormalStop(OutputGateway *g, OutputReason reason, uint32_t now_ms)
{
    if (g == NULL || !follow(g) || !g->follow_configured
        || (g->state != OUTPUT_ARMED && g->state != OUTPUT_ACTIVE)
        || (reason != OUTPUT_REASON_CANCEL && reason != OUTPUT_REASON_EXPIRED)) { return false; }
    bool previous_ready;
    if (!follow_delivery_ready(g, now_ms, &previous_ready)) { return false; }
    uint32_t age = now_ms - g->armed_ms;
    bool sample_expired = !g->request_valid && age >= g->config.max_request_ms;
    if (g->request_valid && s4_follow(g)) {
        sample_expired = (uint32_t)(now_ms - g->request_ms) >= g->ttl_ms;
    } else if (g->request_valid && axes_follow(g)) {
        for (unsigned axis = 0U; axis < SAFETY_JOINT_COUNT; axis++) {
            if (SafetySupervisor_AxisEnabled(&g->config, axis)
                && (uint32_t)(now_ms - g->axes_last_sample_ms[axis])
                    >= g->config.max_request_ms) { sample_expired = true; }
        }
    }
    if (g->safety_reason != SAFETY_OK
        || (uint32_t)(now_ms - g->last_step_ms) > g->config.service_limit_ms
        || age >= g->session_ms || sample_expired) {
        withdraw(g, OUTPUT_REASON_HEALTH, true);
        return false;
    }
    uint32_t remaining = g->session_ms - age;
    uint32_t budget = remaining < g->ramp_max_ms ? remaining : g->ramp_max_ms;
    if (budget <= 2U * g->send_interval_ms) {
        withdraw(g, OUTPUT_REASON_EXPIRED, true);
        return false;
    }
    g->state = OUTPUT_RAMPING_DOWN;
    g->reason = reason;
    g->request_valid = false;
    g->follow_target_pending = false;
    g->target = 0.0f;
    g->axes_target = (S5FollowEffort){0};
    g->ramp_begin_ms = now_ms;
    g->ramp_deadline_ms = now_ms + budget;
    g->last_sent_ms = now_ms;
    g->follow_zero_pending = false;
    /* applied is the last torque actually accepted by the transport. */
    return true;
}

bool OutputGateway_RefreshFollow(OutputGateway *g, float motor_effort_nm,
                                 uint32_t sample_ms, uint32_t sequence, uint32_t now_ms)
{
    if (g == NULL || !s4_follow(g) || !g->follow_configured
        || (g->state != OUTPUT_ARMED && g->state != OUTPUT_ACTIVE)) { return false; }
    bool previous_ready;
    if (!follow_delivery_ready(g, now_ms, &previous_ready)) { return false; }
    uint32_t age = now_ms - g->armed_ms;
    if (age >= g->session_ms - g->ramp_max_ms) {
        (void)OutputGateway_BeginNormalStop(g, OUTPUT_REASON_EXPIRED, now_ms);
        return false;
    }
    uint32_t sample_age = now_ms - sample_ms;
    uint32_t sample_after_arm = sample_ms - g->armed_ms;
    uint32_t sequence_step = sequence - g->last_sequence;
    if ((uint32_t)(now_ms - g->last_step_ms) > g->config.service_limit_ms
        || (g->request_valid && (uint32_t)(now_ms - g->request_ms) >= g->ttl_ms)
        || (g->state == OUTPUT_ARMED && age >= g->config.max_request_ms)
        || !isfinite(motor_effort_nm)
        || fabsf(motor_effort_nm) > g->config.joint[0].max_motor_command
        || sequence == 0U || (g->follow_sequence_valid
            && (sequence_step == 0U || sequence_step >= UINT32_C(0x80000000)))
        || sample_age >= g->config.max_request_ms || sample_age >= UINT32_C(0x80000000)
        || sample_after_arm == 0U || sample_after_arm >= UINT32_C(0x80000000)
        || (g->follow_sequence_valid && (uint32_t)(sample_ms - g->last_sample_ms) >= UINT32_C(0x80000000))) {
        withdraw(g, OUTPUT_REASON_REQUEST, true);
        return false;
    }
    g->target = motor_effort_nm;
    g->request_ms = sample_ms;
    g->ttl_ms = g->config.max_request_ms;
    g->request_valid = true;
    g->follow_target_pending = true;
    g->last_sequence = sequence;
    g->last_sample_ms = sample_ms;
    g->follow_sequence_valid = true;
    g->state = OUTPUT_ACTIVE;
    return true;
}

bool OutputGateway_RefreshAxesFollow(OutputGateway *g, S5FollowEffort effort,
                                     S5FollowSample sample, uint32_t now_ms)
{
    if (g == NULL || !axes_follow(g) || !g->follow_configured
        || (g->state != OUTPUT_ARMED && g->state != OUTPUT_ACTIVE)) { return false; }
    bool previous_ready;
    if (!follow_delivery_ready(g, now_ms, &previous_ready)) { return false; }
    uint32_t age = now_ms - g->armed_ms;
    if (age >= g->session_ms - g->ramp_max_ms) {
        (void)OutputGateway_BeginNormalStop(g, OUTPUT_REASON_EXPIRED, now_ms);
        return false;
    }
    const float value[SAFETY_JOINT_COUNT] = { effort.shoulder_rotor_nm, effort.elbow_current_a };
    bool invalid = (uint32_t)(now_ms - g->last_step_ms) > g->config.service_limit_ms
        || (!g->request_valid && age >= g->config.max_request_ms);
    for (unsigned axis = 0U; axis < SAFETY_JOINT_COUNT; axis++) {
        bool selected = SafetySupervisor_AxisEnabled(&g->config, axis);
        if (!selected) {
            invalid |= value[axis] != 0.0f || sample.sequence[axis] != 0U
                || sample.sample_ms[axis] != 0U;
            continue;
        }
        uint32_t sample_age = now_ms - sample.sample_ms[axis];
        uint32_t after_arm = sample.sample_ms[axis] - g->armed_ms;
        uint32_t sequence_step = sample.sequence[axis] - g->axes_last_sequence[axis];
        invalid |= !isfinite(value[axis])
            || fabsf(value[axis]) > g->config.joint[axis].max_motor_command
            || sample.sequence[axis] == 0U
            || (g->axes_sequence_valid[axis]
                && (sequence_step == 0U || sequence_step >= UINT32_C(0x80000000)))
            || sample_age >= g->config.max_request_ms
            || sample_age >= UINT32_C(0x80000000)
            || after_arm == 0U || after_arm >= UINT32_C(0x80000000);
        if (g->request_valid && (uint32_t)(now_ms - g->axes_last_sample_ms[axis])
            >= g->config.max_request_ms) { invalid = true; }
    }
    if (invalid) { withdraw(g, OUTPUT_REASON_REQUEST, true); return false; }
    g->axes_target = effort;
    g->request_valid = true;
    g->follow_target_pending = true;
    for (unsigned axis = 0U; axis < SAFETY_JOINT_COUNT; axis++) {
        if (!SafetySupervisor_AxisEnabled(&g->config, axis)) { continue; }
        g->axes_last_sequence[axis] = sample.sequence[axis];
        g->axes_last_sample_ms[axis] = sample.sample_ms[axis];
        g->axes_sequence_valid[axis] = true;
    }
    g->state = OUTPUT_ACTIVE;
    return true;
}

static bool submit_follow_torque(OutputGateway *g, float applied)
{
    BoardCanFrame frames[2] = {0};
    float motor_nm = applied * (float)g->config.joint[0].mapping.direction;
    return MotorCommand_GdsV22Torque(g->shoulder_node, motor_nm, &frames[0])
        && g->transport.purge(g->transport.context, SAFETY_SHOULDER_AXES)
        && g->transport.submit(g->transport.context, frames, SAFETY_SHOULDER_AXES, false);
}

static void finish_follow_stop(OutputGateway *g)
{
    BoardCanFrame frames[2] = {0};
    uint8_t axes = g->config.enabled_axes;
    g->stop_attempted = true;
    g->purge_ok = g->transport.purge(g->transport.context, axes);
    bool encoded = (!SafetySupervisor_AxisEnabled(&g->config, 0U)
        || MotorCommand_GdsV22Idle(g->shoulder_node, &frames[0]))
        && (!SafetySupervisor_AxisEnabled(&g->config, 1U)
        || MotorCommand_C620(g->elbow_id, 0.0f, &frames[1]));
    g->stop_accepted = encoded && g->transport.submit(g->transport.context, frames, axes, true);
    if (!g->purge_ok || !g->stop_accepted) {
        withdraw(g, OUTPUT_REASON_TRANSPORT, true);
        return;
    }
    g->state = OUTPUT_SAFE_OFF;
    g->driver_phase = DRIVER_NOT_PREPARED;
    g->applied = 0.0f;
    g->axes_applied = (S5FollowEffort){0};
    g->follow_zero_pending = false;
    g->follow_output_pending = false;
}

static bool submit_axes_follow(OutputGateway *g, S5FollowEffort applied)
{
    BoardCanFrame frames[2] = {0};
    bool encoded = (!SafetySupervisor_AxisEnabled(&g->config, 0U)
        || MotorCommand_GdsV22Torque(g->shoulder_node,
            applied.shoulder_rotor_nm * (float)g->config.joint[0].mapping.direction, &frames[0]))
        && (!SafetySupervisor_AxisEnabled(&g->config, 1U)
        || MotorCommand_C620(g->elbow_id,
            applied.elbow_current_a * (float)g->config.joint[1].mapping.direction, &frames[1]));
    /* The two buses accept separately. A partial submit is a transport fault;
     * withdraw then purges both mailboxes and submits both stop frames. */
    return encoded && g->transport.purge(g->transport.context, g->config.enabled_axes)
        && g->transport.submit(g->transport.context, frames, g->config.enabled_axes, false);
}

static bool axes_samples_fresh(const OutputGateway *g, uint32_t now_ms)
{
    if (!g->request_valid) {
        return (uint32_t)(now_ms - g->armed_ms) < g->config.max_request_ms;
    }
    for (unsigned axis = 0U; axis < SAFETY_JOINT_COUNT; axis++) {
        if (SafetySupervisor_AxisEnabled(&g->config, axis)
            && (uint32_t)(now_ms - g->axes_last_sample_ms[axis])
                >= g->config.max_request_ms) { return false; }
    }
    return true;
}

static void step_axes_follow(OutputGateway *g, uint32_t now_ms)
{
    bool ramp = g->state == OUTPUT_RAMPING_DOWN;
    uint32_t since_arm = now_ms - g->armed_ms;
    if (since_arm > g->session_ms) { withdraw(g, OUTPUT_REASON_EXPIRED, true); return; }
    if (!ramp && since_arm >= g->session_ms - g->ramp_max_ms) {
        if (!OutputGateway_BeginNormalStop(g, OUTPUT_REASON_EXPIRED, now_ms)) { return; }
        ramp = true;
    }
    if (ramp && (uint32_t)(now_ms - g->ramp_begin_ms)
        > (uint32_t)(g->ramp_deadline_ms - g->ramp_begin_ms)) {
        withdraw(g, OUTPUT_REASON_EXPIRED, true); return;
    }
    bool deadline_reached = ramp && (since_arm >= g->session_ms
        || (uint32_t)(now_ms - g->ramp_begin_ms)
            >= (uint32_t)(g->ramp_deadline_ms - g->ramp_begin_ms));
    bool previous_ready;
    if (!follow_delivery_ready(g, now_ms, &previous_ready)) { return; }
    if (!previous_ready) {
        if (deadline_reached) { withdraw(g, OUTPUT_REASON_EXPIRED, true); }
        return;
    }
    if (ramp && g->follow_zero_pending) { finish_follow_stop(g); return; }
    if (!ramp && !axes_samples_fresh(g, now_ms)) {
        withdraw(g, OUTPUT_REASON_EXPIRED, true); return;
    }
    uint32_t send_elapsed = now_ms - g->last_sent_ms;
    if (send_elapsed < g->send_interval_ms) {
        if (deadline_reached) { withdraw(g, OUTPUT_REASON_EXPIRED, true); }
        return;
    }
    if (!ramp && !g->request_valid) { return; }
    float prior[SAFETY_JOINT_COUNT] = {
        g->axes_applied.shoulder_rotor_nm, g->axes_applied.elbow_current_a
    };
    float target[SAFETY_JOINT_COUNT] = {
        g->axes_target.shoulder_rotor_nm, g->axes_target.elbow_current_a
    };
    const float release[SAFETY_JOINT_COUNT] = {
        g->axes_release_slew_per_s.shoulder_rotor_nm,
        g->axes_release_slew_per_s.elbow_current_a
    };
    float next[SAFETY_JOINT_COUNT] = {0.0f, 0.0f};
    bool all_zero = true;
    for (unsigned axis = 0U; axis < SAFETY_JOINT_COUNT; axis++) {
        if (!SafetySupervisor_AxisEnabled(&g->config, axis)) { continue; }
        float slew = ramp ? release[axis] : g->config.joint[axis].motor_slew_per_s;
        float step = slew * ((float)(ramp ? send_elapsed : g->send_interval_ms) / 1000.0f);
        if (!isfinite(step) || step <= 0.0f) {
            withdraw(g, OUTPUT_REASON_CONFIG, true); return;
        }
        if (ramp) {
            float magnitude = fabsf(prior[axis]);
            float next_magnitude = magnitude > step ? magnitude - step : 0.0f;
            if (next_magnitude >= magnitude && magnitude > 0.0f) {
                withdraw(g, OUTPUT_REASON_CONFIG, true); return;
            }
            next[axis] = copysignf(next_magnitude, prior[axis]);
        } else {
            float delta = target[axis] - prior[axis];
            if (delta > step) { delta = step; }
            if (delta < -step) { delta = -step; }
            next[axis] = prior[axis] + delta;
        }
        if (!isfinite(next[axis])
            || fabsf(next[axis]) > g->config.joint[axis].max_motor_command) {
            withdraw(g, OUTPUT_REASON_REQUEST, true); return;
        }
        all_zero &= next[axis] == 0.0f;
    }
    S5FollowEffort applied = {next[0], next[1]};
    if (!submit_axes_follow(g, applied)) {
        withdraw(g, OUTPUT_REASON_TRANSPORT, true); return;
    }
    g->axes_applied = applied;
    g->last_sent_ms = now_ms;
    g->follow_output_submit_ms = now_ms;
    g->follow_output_pending = true;
    g->follow_target_pending = false;
    if (ramp && all_zero) {
        g->follow_zero_pending = true;
        if (deadline_reached) { withdraw(g, OUTPUT_REASON_EXPIRED, true); }
    } else if (deadline_reached) {
        withdraw(g, OUTPUT_REASON_EXPIRED, true);
    }
}

static void step_follow(OutputGateway *g, uint32_t now_ms)
{
    bool ramp = g->state == OUTPUT_RAMPING_DOWN;
    uint32_t since_arm = now_ms - g->armed_ms;
    if (since_arm > g->session_ms) { withdraw(g, OUTPUT_REASON_EXPIRED, true); return; }
    if (!ramp && since_arm >= g->session_ms - g->ramp_max_ms) {
        if (!OutputGateway_BeginNormalStop(g, OUTPUT_REASON_EXPIRED, now_ms)) { return; }
        ramp = true;
    }
    if (ramp && (uint32_t)(now_ms - g->ramp_begin_ms)
        > (uint32_t)(g->ramp_deadline_ms - g->ramp_begin_ms)) {
        withdraw(g, OUTPUT_REASON_EXPIRED, true);
        return;
    }
    bool deadline_reached = ramp && (since_arm >= g->session_ms
        || (uint32_t)(now_ms - g->ramp_begin_ms)
            >= (uint32_t)(g->ramp_deadline_ms - g->ramp_begin_ms));
    bool previous_ready;
    if (!follow_delivery_ready(g, now_ms, &previous_ready)) { return; }
    if (!previous_ready) {
        if (deadline_reached) { withdraw(g, OUTPUT_REASON_EXPIRED, true); }
        return;
    }
    if (ramp && g->follow_zero_pending) {
        finish_follow_stop(g);
        return;
    }
    if (!ramp && ((!g->request_valid && since_arm >= g->config.max_request_ms)
        || (g->request_valid && (uint32_t)(now_ms - g->request_ms) >= g->ttl_ms))) {
        withdraw(g, OUTPUT_REASON_EXPIRED, true);
        return;
    }
    uint32_t send_elapsed = now_ms - g->last_sent_ms;
    if (send_elapsed < g->send_interval_ms) {
        if (deadline_reached) { withdraw(g, OUTPUT_REASON_EXPIRED, true); }
        return;
    }
    if (!ramp && (!g->request_valid || (!g->follow_target_pending && g->target == g->applied))) {
        return;
    }
    float slew = ramp ? g->release_slew_nm_s : g->config.joint[0].motor_slew_per_s;
    /* A settled target may have left the wire idle for a long time. That
     * idle time must not become one large step toward a newly refreshed goal. */
    float step = slew * ((float)(ramp ? send_elapsed : g->send_interval_ms) / 1000.0f);
    if (!isfinite(step) || step <= 0.0f) { withdraw(g, OUTPUT_REASON_CONFIG, true); return; }
    float next;
    if (ramp) {
        float magnitude = fabsf(g->applied);
        float next_magnitude = magnitude > step ? magnitude - step : 0.0f;
        if (next_magnitude >= magnitude && magnitude > 0.0f) {
            withdraw(g, OUTPUT_REASON_CONFIG, true); return;
        }
        next = copysignf(next_magnitude, g->applied);
    } else {
        float delta = g->target - g->applied;
        if (delta > step) { delta = step; }
        if (delta < -step) { delta = -step; }
        next = g->applied + delta;
    }
    if (!isfinite(next) || fabsf(next) > g->config.joint[0].max_motor_command
        || !submit_follow_torque(g, next)) {
        withdraw(g, OUTPUT_REASON_TRANSPORT, true); return;
    }
    g->applied = next;
    g->last_sent_ms = now_ms;
    g->follow_output_submit_ms = now_ms;
    g->follow_output_pending = true;
    g->follow_target_pending = false;
    if (ramp && next == 0.0f) {
        g->follow_zero_pending = true;
        if (deadline_reached) { withdraw(g, OUTPUT_REASON_EXPIRED, true); }
    }
    else if (deadline_reached) { withdraw(g, OUTPUT_REASON_EXPIRED, true); }
}

void OutputGateway_Step(OutputGateway *g, const SafetyInputs *inputs, uint32_t now_ms)
{
    if (g->stop_pending_purge || g->stop_pending_tx) {
        progress_s3_elbow_stop(g, now_ms);
        return;
    }
    if (g->state != OUTPUT_ARMED && g->state != OUTPUT_ACTIVE
        && g->state != OUTPUT_RAMPING_DOWN) { return; }
    g->safety_reason = SafetySupervisor_Check(&g->config, inputs, now_ms);
    if (g->safety_reason != SAFETY_OK) { withdraw(g, OUTPUT_REASON_HEALTH, true); return; }
    uint32_t elapsed = now_ms - g->last_step_ms;
    if (elapsed > g->config.service_limit_ms) { withdraw(g, OUTPUT_REASON_SERVICE, true); return; }
    if (s4_follow(g)) {
        g->last_step_ms = now_ms;
        step_follow(g, now_ms);
        return;
    }
    if (axes_follow(g)) {
        g->last_step_ms = now_ms;
        step_axes_follow(g, now_ms);
        return;
    }
    if ((!g->request_valid && (uint32_t)(now_ms - g->armed_ms) >= g->config.max_request_ms)
        || (g->request_valid && (uint32_t)(now_ms - g->request_ms) >= g->ttl_ms)) {
        withdraw(g, OUTPUT_REASON_EXPIRED, true);
        return;
    }
    if (elapsed == 0U) { return; }
    g->last_step_ms = now_ms;
    if (!g->request_valid) { return; }
    if (s3_elbow_async(g)) {
        uint8_t axes = g->config.enabled_axes;
        if (g->transport.failed(g->transport.context, axes)
            || (g->transport.pending(g->transport.context, axes)
                && (uint32_t)(now_ms - g->s3_output_submit_ms) > g->config.service_limit_ms)) {
            withdraw(g, OUTPUT_REASON_TRANSPORT, true);
            return;
        }
        if (g->transport.pending(g->transport.context, axes)) { return; }
    }
    const SafetyJointConfig *j = &g->config.joint[g->selected_joint];
    float step = j->motor_slew_per_s * ((float)elapsed / 1000.0f);
    if (!isfinite(step)) { withdraw(g, OUTPUT_REASON_CONFIG, true); return; }
    float delta = g->target - g->applied;
    if (delta > step) { delta = step; }
    if (delta < -step) { delta = -step; }
    float applied = g->applied + delta;
    if (!isfinite(applied) || fabsf(applied) > j->max_motor_command) {
        withdraw(g, OUTPUT_REASON_REQUEST, true);
        return;
    }
    float shoulder = 0.0f, elbow = 0.0f;
    if (g->selected_joint == 0U) { shoulder = applied * (float)j->mapping.direction; }
    else { elbow = applied * (float)j->mapping.direction; }
    BoardCanFrame frames[2] = {0};
    if ((SafetySupervisor_AxisEnabled(&g->config, 0U) && !MotorCommand_GdsV22Torque(g->shoulder_node, shoulder, &frames[0]))
        || (SafetySupervisor_AxisEnabled(&g->config, 1U) && !MotorCommand_C620(g->elbow_id, elbow, &frames[1]))
        || !g->transport.purge(g->transport.context, g->config.enabled_axes)
        || !g->transport.submit(g->transport.context, frames, g->config.enabled_axes, false)) {
        withdraw(g, OUTPUT_REASON_TRANSPORT, true);
        return;
    }
    g->applied = applied;
    if (s3_elbow_async(g)) { g->s3_output_submit_ms = now_ms; }
}

void OutputGateway_CancelAt(OutputGateway *g, uint32_t now_ms)
{
    if (g->stop_pending_purge || g->stop_pending_tx) { return; }
    /* Cancel from FAULT must preserve the latch and evidence. */
    if (g->state == OUTPUT_FAULT) { return; }
    if (follow(g) && (g->state == OUTPUT_ARMED || g->state == OUTPUT_ACTIVE)) {
        (void)OutputGateway_BeginNormalStop(g, OUTPUT_REASON_CANCEL, now_ms);
        return;
    }
    if (follow(g) && g->state == OUTPUT_RAMPING_DOWN) { return; }
    g->last_step_ms = now_ms;
    withdraw(g, OUTPUT_REASON_CANCEL, false);
}
void OutputGateway_Cancel(OutputGateway *g)
{ OutputGateway_CancelAt(g, g->last_step_ms); }

bool OutputGateway_AcknowledgeFault(OutputGateway *g, const SafetyInputs *inputs,
                                    uint32_t now_ms, bool manual_acknowledged)
{
    if (g->state != OUTPUT_FAULT || !manual_acknowledged || !transport_ready(g)
        || g->stop_pending_purge || g->stop_pending_tx
        || follow(g)
        || (g->config.stop_policy == SAFETY_STOP_SUPERVISED_SHOULDER_BENCH && g->shoulder_attempted)
        || !SafetySupervisor_StopObserved(&g->config, inputs)
        || (supervised_bench(g->config.stop_policy) && g->stop_delivery_failed)
        || SafetySupervisor_CheckBeforePrepare(&g->config, inputs, now_ms) != SAFETY_OK
        || !g->transport.purge(g->transport.context, g->config.enabled_axes)) { return false; }
    g->state = OUTPUT_SAFE_OFF;
    g->reason = OUTPUT_REASON_NONE;
    g->safety_reason = SAFETY_OK;
    g->request_valid = false;
    g->applied = 0.0f;
    return true;
}

static bool driver_healthy(const OutputGateway *g, const OutputDriverStatus *d,
                           unsigned axis, uint32_t now_ms)
{
    return d != NULL && d->valid && d->node == (axis == 0U ? g->shoulder_node : g->elbow_id)
        && (axis != 0U || (d->calibration_verified && d->axis_error == 0U && d->fault_flags == 0U))
        && (uint32_t)(now_ms - d->received_ms) <= g->config.feedback_timeout_ms;
}
static bool all_drivers_healthy(const OutputGateway *g, const OutputDriverStatus d[2], uint32_t now_ms)
{
    for (unsigned axis = 0U; axis < 2U; axis++) {
        if (SafetySupervisor_AxisEnabled(&g->config, axis)
            && (d == NULL || !driver_healthy(g, &d[axis], axis, now_ms))) { return false; }
    }
    return true;
}
static bool new_proof(uint32_t stamp, uint32_t request_ms, uint32_t now_ms, uint32_t timeout_ms)
{
    uint32_t after = stamp - request_ms;
    return after > 0U && after < UINT32_C(0x80000000)
        && (uint32_t)(now_ms - stamp) <= timeout_ms;
}
static bool prepare_submit(OutputGateway *g, unsigned step)
{
    BoardCanFrame frames[2] = {0}; bool encoded = true;
    if (SafetySupervisor_AxisEnabled(&g->config, 1U)) {
        encoded = MotorCommand_C620(g->elbow_id, 0.0f, &frames[1]);
    }
    if (SafetySupervisor_AxisEnabled(&g->config, 0U)) {
        if (step == 0U) { encoded = encoded && MotorCommand_GdsV22Torque(g->shoulder_node, 0.0f, &frames[0]); }
        else if (step == 1U) { encoded = encoded && MotorCommand_GdsV22TorqueMode(g->shoulder_node, &frames[0]); }
        else { encoded = encoded && MotorCommand_GdsV22ClosedLoop(g->shoulder_node, &frames[0]); }
    }
    return encoded && g->transport.purge(g->transport.context, g->config.enabled_axes)
        && g->transport.submit(g->transport.context, frames, g->config.enabled_axes, false);
}
bool OutputGateway_Prepare(OutputGateway *g, const SafetyInputs *in,
                          const OutputDriverStatus d[2], uint32_t timeout_ms, uint32_t now_ms)
{
    if (g->state != OUTPUT_SAFE_OFF || g->stop_pending_purge || g->stop_pending_tx) { return false; }
    if ((g->config.stop_policy == SAFETY_STOP_SUPERVISED_SHOULDER_BENCH && g->shoulder_attempted)
        || (follow(g) && (g->follow_session_used || !g->follow_configured))) {
        return false;
    }
    if (!transport_ready(g) || !SafetySupervisor_ConfigReady(&g->config)
        || timeout_ms == 0U || timeout_ms >= UINT32_C(0x80000000)) {
        g->reason = OUTPUT_REASON_CONFIG; return false;
    }
    g->safety_reason = SafetySupervisor_CheckBeforePrepare(&g->config, in, now_ms);
    if (g->safety_reason != SAFETY_OK || !all_drivers_healthy(g, d, now_ms)
        || (SafetySupervisor_AxisEnabled(&g->config, 0U) && d[0].axis_state != 1U)
        || (SafetySupervisor_AxisEnabled(&g->config, 1U) && !SafetySupervisor_StopObserved(&g->config, in))) {
        g->reason = OUTPUT_REASON_HEALTH; return false;
    }
    g->state = OUTPUT_PREPARING; g->reason = OUTPUT_REASON_NONE;
    g->driver_phase = DRIVER_WAIT_ZERO; g->prepare_ms = g->prepare_phase_ms = now_ms;
    g->prepare_timeout_ms = timeout_ms; g->last_step_ms = now_ms;
    g->request_valid = false; g->applied = g->target = 0.0f;
    g->stop_attempted = g->stop_accepted = false;
    if (!prepare_submit(g, 0U)) { withdraw(g, OUTPUT_REASON_TRANSPORT, true); return false; }
    return true;
}
static bool zero_targets_held(const OutputGateway *g, const OutputDriverStatus d[2])
{
    for (unsigned axis = 0U; axis < 2U; axis++) {
        if (axis == 0U && SafetySupervisor_AxisEnabled(&g->config, axis)
            && (d == NULL || !d[axis].zero_verified)) { return false; }
    }
    return true;
}
bool OutputGateway_DriverReady(const OutputGateway *g, const OutputDriverStatus d[2], uint32_t now_ms)
{
    return g->driver_phase == DRIVER_READY && all_drivers_healthy(g, d, now_ms)
        && (g->state == OUTPUT_ACTIVE || g->state == OUTPUT_RAMPING_DOWN
            || zero_targets_held(g, d))
        && (!SafetySupervisor_AxisEnabled(&g->config, 0U)
            || (d[0].axis_state == 8U && d[0].mode_verified));
}
void OutputGateway_PollPreparation(OutputGateway *g, const SafetyInputs *in,
                                   const OutputDriverStatus d[2], uint32_t now_ms)
{
    if (g->state != OUTPUT_PREPARING && g->state != OUTPUT_READY) { return; }
    g->safety_reason = SafetySupervisor_CheckBeforePrepare(&g->config, in, now_ms);
    bool shoulder = SafetySupervisor_AxisEnabled(&g->config, 0U);
    bool state_valid = !shoulder || (d != NULL && ((g->driver_phase == DRIVER_WAIT_CLOSED_LOOP
        && (d[0].axis_state == 1U || d[0].axis_state == 8U))
        || (g->driver_phase == DRIVER_READY && d[0].axis_state == 8U)
        || ((g->driver_phase == DRIVER_WAIT_ZERO || g->driver_phase == DRIVER_WAIT_MODE)
            && d[0].axis_state == 1U)));
    bool revoked = (g->driver_phase != DRIVER_WAIT_ZERO && !zero_targets_held(g, d))
        || (shoulder && d != NULL && ((g->driver_phase == DRIVER_WAIT_CLOSED_LOOP
        && !d[0].mode_verified) || (g->driver_phase == DRIVER_READY && !d[0].mode_verified)));
    if (g->safety_reason != SAFETY_OK || !all_drivers_healthy(g, d, now_ms) || !state_valid || revoked) {
        withdraw(g, OUTPUT_REASON_HEALTH, true); return;
    }
    if ((uint32_t)(now_ms - g->prepare_ms) >= g->prepare_timeout_ms) {
        withdraw(g, OUTPUT_REASON_EXPIRED, true); return;
    }
    if ((uint32_t)(now_ms - g->last_step_ms) > g->config.service_limit_ms) {
        withdraw(g, OUTPUT_REASON_SERVICE, true); return;
    }
    g->last_step_ms = now_ms;
    if (g->driver_phase == DRIVER_WAIT_ZERO) {
        for (unsigned axis = 0U; axis < 2U; axis++) {
            if (SafetySupervisor_AxisEnabled(&g->config, axis)
                && ((axis == 0U && (!d[axis].zero_verified || !new_proof(d[axis].zero_verified_ms,
                    g->prepare_phase_ms, now_ms, g->config.feedback_timeout_ms)))
                    || (axis == 1U && !new_proof(d[axis].received_ms, g->prepare_phase_ms,
                        now_ms, g->config.feedback_timeout_ms)))) { return; }
        }
        if (!shoulder) { g->driver_phase = DRIVER_READY; g->state = OUTPUT_READY; return; }
        if (!prepare_submit(g, 1U)) { withdraw(g, OUTPUT_REASON_TRANSPORT, true); return; }
        g->driver_phase = DRIVER_WAIT_MODE; g->prepare_phase_ms = now_ms;
    } else if (g->driver_phase == DRIVER_WAIT_MODE && d[0].zero_verified && d[0].mode_verified
        && new_proof(d[0].mode_verified_ms, g->prepare_phase_ms, now_ms, g->config.feedback_timeout_ms)) {
        if (!prepare_submit(g, 2U)) { withdraw(g, OUTPUT_REASON_TRANSPORT, true); return; }
        g->driver_phase = DRIVER_WAIT_CLOSED_LOOP; g->prepare_phase_ms = now_ms;
    } else if (g->driver_phase == DRIVER_WAIT_CLOSED_LOOP && d[0].axis_state == 8U
        && d[0].zero_verified && d[0].mode_verified
        && new_proof(d[0].received_ms, g->prepare_phase_ms, now_ms, g->config.feedback_timeout_ms)) {
        g->driver_phase = DRIVER_READY; g->state = OUTPUT_READY;
    }
}
void OutputGateway_RejectRequest(OutputGateway *g)
{ withdraw(g, OUTPUT_REASON_REQUEST, true); }
