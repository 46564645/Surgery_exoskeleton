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
        || policy == SAFETY_STOP_SUPERVISED_SHOULDER_BENCH;
}

static void withdraw(OutputGateway *g, OutputReason reason, bool fault)
{
    bool was_live = g->state == OUTPUT_ARMED || g->state == OUTPUT_ACTIVE
        || g->state == OUTPUT_PREPARING || g->state == OUTPUT_READY;
    /* Revoke software permission BEFORE touching any transport state. */
    g->state = fault ? OUTPUT_FAULT : OUTPUT_SAFE_OFF;
    g->reason = reason;
    g->request_valid = false;
    g->driver_phase = DRIVER_NOT_PREPARED;
    g->target = 0.0f;
    g->applied = 0.0f;
    if (!was_live) { return; }
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

bool OutputGateway_Arm(OutputGateway *g, const SafetyInputs *inputs, uint32_t now_ms)
{
    if (g->state != OUTPUT_SAFE_OFF && g->state != OUTPUT_READY) { return false; }
    if (g->config.stop_policy == SAFETY_STOP_SUPERVISED_SHOULDER_BENCH && g->shoulder_attempted) {
        return false;
    }
    if (supervised_bench(g->config.stop_policy) && g->state != OUTPUT_READY) {
        return false; /* Bench always starts with an explicit zero preparation. */
    }
    if (!transport_ready(g) || !SafetySupervisor_ConfigReady(&g->config)) {
        g->reason = OUTPUT_REASON_CONFIG;
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
    g->stop_attempted = false;
    g->stop_accepted = false;
    g->purge_ok = true;
    return true;
}

bool OutputGateway_Replace(OutputGateway *g, unsigned joint, float value,
                           uint32_t ttl_ms, uint32_t now_ms)
{
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

void OutputGateway_Step(OutputGateway *g, const SafetyInputs *inputs, uint32_t now_ms)
{
    if (g->state != OUTPUT_ARMED && g->state != OUTPUT_ACTIVE) { return; }
    g->safety_reason = SafetySupervisor_Check(&g->config, inputs, now_ms);
    if (g->safety_reason != SAFETY_OK) { withdraw(g, OUTPUT_REASON_HEALTH, true); return; }
    uint32_t elapsed = now_ms - g->last_step_ms;
    if (elapsed > g->config.service_limit_ms) { withdraw(g, OUTPUT_REASON_SERVICE, true); return; }
    if ((!g->request_valid && (uint32_t)(now_ms - g->armed_ms) >= g->config.max_request_ms)
        || (g->request_valid && (uint32_t)(now_ms - g->request_ms) >= g->ttl_ms)) {
        withdraw(g, OUTPUT_REASON_EXPIRED, true);
        return;
    }
    if (elapsed == 0U) { return; }
    g->last_step_ms = now_ms;
    if (!g->request_valid) { return; }
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
}

void OutputGateway_Cancel(OutputGateway *g)
{
    /* Cancel from FAULT must preserve the latch and evidence. */
    if (g->state == OUTPUT_FAULT) { return; }
    withdraw(g, OUTPUT_REASON_CANCEL, false);
}

bool OutputGateway_AcknowledgeFault(OutputGateway *g, const SafetyInputs *inputs,
                                    uint32_t now_ms, bool manual_acknowledged)
{
    if (g->state != OUTPUT_FAULT || !manual_acknowledged || !transport_ready(g)
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
    if (g->state != OUTPUT_SAFE_OFF) { return false; }
    if (g->config.stop_policy == SAFETY_STOP_SUPERVISED_SHOULDER_BENCH && g->shoulder_attempted) {
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
        && (g->state == OUTPUT_ACTIVE || zero_targets_held(g, d))
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
