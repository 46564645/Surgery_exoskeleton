/* L4: one main-loop gateway; site profile/feedback/stop facts cannot be set by CLI. */
#include "s3_bench.h"
#include "s3_site.h"
#include "s3_feedback.h"
#include "output_can_transport.h"
#include "board_console.h"
#include "board_runtime.h"
#include "can_monitor.h"
#include "s3_shoulder_service.h"
#include <stddef.h>
#include <string.h>
static OutputGateway gateway;
static const S3SiteProfile *profile;
static uint32_t last_service_interval, token_counter, arm_token;
static bool previous_live, feedback_pending;
static uint32_t feedback_prepare_ms;

static bool site_ready(void)
{
    if (profile == NULL) { return false; }
    for (unsigned axis = 0U; axis < 2U; axis++) {
        if (!SafetySupervisor_AxisEnabled(&profile->safety, axis)) { continue; }
        bool relative = profile->coordinates[axis] == S3_BENCH_RELATIVE_ROTOR;
        if ((profile->coordinates[axis] != S3_BENCH_ROTOR
            && profile->coordinates[axis] != S3_ASSEMBLED_JOINT && !relative)
            || relative != profile->safety.joint[axis].relative_native_envelope
            || (relative && (axis != 0U || profile->rotor_reference_confirmed[axis]))
            || ((profile->coordinates[axis] == S3_BENCH_ROTOR || relative)
                && profile->safety.joint[axis].mapping.rotor_per_joint != 1.0f)) { return false; }
    }
    if (profile->shoulder_source == S3_SHOULDER_ENCODER_PROPERTIES
        && !S3ShoulderService_ProfileReady(profile)) { return false; }
    return SafetySupervisor_ConfigReady(&profile->safety)
        && (profile->trusted_feedback_axes & profile->safety.enabled_axes) == profile->safety.enabled_axes
        && ((profile->safety.stop_policy == SAFETY_STOP_EXTERNAL_INPUT && profile->stop_input_confirmed
                && S3StopInput_ConfigReady(&profile->operator_stop_input, &profile->independent_stop_input))
            || (profile->safety.stop_policy == SAFETY_STOP_SUPERVISED_C620_BENCH
                && profile->safety.enabled_axes == SAFETY_ELBOW_AXES
                && profile->coordinates[1] == S3_BENCH_ROTOR)
            || (profile->safety.stop_policy == SAFETY_STOP_SUPERVISED_SHOULDER_BENCH
                && S3ShoulderService_ProfileReady(profile)))
        && profile->prepare_timeout_ms > 0U && profile->prepare_timeout_ms < UINT32_C(0x80000000)
        && profile->shoulder_node <= 63U && profile->elbow_id >= 1U && profile->elbow_id <= 8U;
}
static bool inputs(uint32_t now_ms, SafetyInputs *in, S3SiteSample *site)
{
    memset(in, 0, sizeof(*in)); memset(site, 0, sizeof(*site));
    bool received = S3Site_ReadSample(now_ms, site);
    if (!site_ready() || !received) { return false; }
    memcpy(in->joint, site->joint, sizeof(in->joint));
    in->operator_stop_clear = site->operator_stop_clear;
    in->independently_stopped = site->independently_stopped;
    in->feedback_stationary = site->feedback_stationary;
    in->drivers_ready = OutputGateway_DriverReady(&gateway, site->driver, now_ms);
    in->transport_healthy = !BoardConsole_HasFault() && last_service_interval > 0U
        && last_service_interval <= profile->safety.service_limit_ms;
    for (unsigned bus = 0U; bus < BOARD_CAN_COUNT; bus++) {
        if (!SafetySupervisor_AxisEnabled(&profile->safety, bus)) { continue; }
        BoardCanStats s;
        if (!BoardCan_GetStats((BoardCanBus)bus, &s) || !s.started || s.bus_off
            || s.start_error != BOARD_CAN_START_OK || s.errors != 0U || s.dropped != 0U
            || s.fifo_overruns != 0U || s.invalid != 0U) { in->transport_healthy = false; }
    }
    return true;
}
static void invalidate_token(void)
{
    if (gateway.state == OUTPUT_FAULT || (!feedback_pending && gateway.state == OUTPUT_SAFE_OFF)) {
        S3Feedback_EndRelative();
    }
    if (gateway.state == OUTPUT_FAULT) {
        feedback_pending = false;
        if (gateway.stop_delivery_failed || gateway.reason == OUTPUT_REASON_TRANSPORT
            || gateway.reason == OUTPUT_REASON_SERVICE || gateway.reason == OUTPUT_REASON_HEALTH
            || S3ShoulderService_Get().state != S3_READER_OBSERVING) {
            (void)S3ShoulderService_Stop();
        }
    }
    bool live = gateway.state == OUTPUT_ARMED || gateway.state == OUTPUT_ACTIVE
        || gateway.state == OUTPUT_PREPARING || gateway.state == OUTPUT_READY;
    if (previous_live && !live) { CanMonitor_ResetElbowStandstill(BoardRuntime_NowMs()); }
    previous_live = live;
    if (gateway.state != OUTPUT_ARMED && gateway.state != OUTPUT_ACTIVE) { arm_token = 0U; }
}
void S3Bench_Init(void)
{
    S3Feedback_Init();
    profile = S3Site_GetProfile(); last_service_interval = token_counter = arm_token = 0U;
    previous_live = feedback_pending = false; feedback_prepare_ms = 0U; CanMonitor_ResetElbowStandstill(BoardRuntime_NowMs());
    OutputTransport port = {0};
    if (site_ready()) { port = OutputCanTransport_Create(); }
    OutputGateway_Init(&gateway, profile == NULL ? NULL : &profile->safety,
                        site_ready() ? &port : NULL,
                        profile == NULL ? 1U : profile->shoulder_node,
                        profile == NULL ? 1U : profile->elbow_id);
}
void S3Bench_Poll(uint32_t now_ms, uint32_t service_interval_ms)
{
    last_service_interval = service_interval_ms;
    if (feedback_pending && (uint32_t)(now_ms - feedback_prepare_ms) >= profile->prepare_timeout_ms) {
        feedback_pending = false; (void)S3ShoulderService_Stop();
        OutputGateway_RejectRequest(&gateway);
    }
    /* Read before this tick's output: an earlier output must have completed,
     * while a newly queued read is preserved by the output transport. */
    if (S3ShoulderService_Get().state == S3_READER_OBSERVING
        && (service_interval_ms == 0U || service_interval_ms > profile->safety.service_limit_ms)) {
        (void)S3ShoulderService_Stop();
    }
    S3ShoulderService_Poll(now_ms);
    if (feedback_pending) {
        SafetyInputs first; S3SiteSample sample;
        if (S3ShoulderService_Get().state != S3_READER_ACTIVE) {
            feedback_pending = false; (void)S3ShoulderService_Stop();
            OutputGateway_RejectRequest(&gateway);
        } else if (inputs(now_ms, &first, &sample)) {
            feedback_pending = false;
            if (!OutputGateway_Prepare(&gateway, &first, sample.driver, profile->prepare_timeout_ms, now_ms)) {
                if (S3ShoulderService_Get().state != S3_READER_OBSERVING
                    && !S3ShoulderService_Stop() && gateway.state != OUTPUT_FAULT) {
                    OutputGateway_RejectRequest(&gateway);
                }
            }
        }
    }
    SafetyInputs in; S3SiteSample site; (void)inputs(now_ms, &in, &site);
    OutputGateway_PollPreparation(&gateway, &in, site.driver, now_ms);
    /* PollPreparation may have just proven readiness. No implicit Arm. */
    in.drivers_ready = OutputGateway_DriverReady(&gateway, site.driver, now_ms);
    OutputGateway_Step(&gateway, &in, now_ms);
    invalidate_token();
}
bool S3Bench_Prepare(uint32_t now_ms)
{
    if (feedback_pending) { return false; }
    if (profile != NULL && profile->shoulder_source == S3_SHOULDER_ENCODER_PROPERTIES) {
        bool relative = profile->coordinates[0] == S3_BENCH_RELATIVE_ROTOR;
        if (relative && S3Feedback_GetAnchor().phase != S3_RELATIVE_UNSET) { return false; }
        if (!site_ready() || gateway.state != OUTPUT_SAFE_OFF
            || !S3ShoulderService_Start(now_ms)) { return false; }
        if (relative && !S3Feedback_BeginRelative(now_ms)) {
            (void)S3ShoulderService_Stop(); OutputGateway_RejectRequest(&gateway);
            invalidate_token(); return false;
        }
        feedback_pending = true; feedback_prepare_ms = now_ms;
        return true; /* Explicit asynchronous preparation; still no output. */
    }
    SafetyInputs in; S3SiteSample site;
    if (!inputs(now_ms, &in, &site)) { return false; }
    OutputState before = gateway.state;
    bool ok = OutputGateway_Prepare(&gateway, &in, site.driver, profile->prepare_timeout_ms, now_ms);
    /* Preparation can enter and withdraw within this call, before previous_live
     * ever sees it. Both zero preparation and that stop need a new window. */
    if (ok || (before == OUTPUT_SAFE_OFF && gateway.state == OUTPUT_FAULT && gateway.stop_attempted)) {
        CanMonitor_ResetElbowStandstill(BoardRuntime_NowMs());
    }
    invalidate_token(); return ok;
}
bool S3Bench_Arm(uint32_t now_ms)
{
    SafetyInputs in; S3SiteSample site;
    if (!inputs(now_ms, &in, &site) || !in.drivers_ready || gateway.state != OUTPUT_READY) { return false; }
    /* Tokens distinguish requests from earlier permits within this boot. No wrap reuse. */
    if (token_counter == UINT32_MAX) { S3Bench_Cancel(); return false; }
    if (!OutputGateway_Arm(&gateway, &in, now_ms)) { invalidate_token(); return false; }
    arm_token = ++token_counter;
    invalidate_token();
    return true;
}
bool S3Bench_Request(uint32_t token, unsigned joint, float value, uint32_t ttl_ms, uint32_t now_ms)
{
    if (token == 0U || token != arm_token) { return false; }
    SafetyInputs in; S3SiteSample site; (void)inputs(now_ms, &in, &site);
    if (SafetySupervisor_Check(&gateway.config, &in, now_ms) != SAFETY_OK) {
        OutputGateway_Step(&gateway, &in, now_ms); invalidate_token(); return false;
    }
    bool ok = OutputGateway_Replace(&gateway, joint, value, ttl_ms, now_ms);
    invalidate_token(); return ok;
}
void S3Bench_RejectRequest(void)
{
    feedback_pending = false; OutputGateway_RejectRequest(&gateway);
    if (S3ShoulderService_Get().state != S3_READER_OBSERVING) { (void)S3ShoulderService_Stop(); }
    invalidate_token();
}
void S3Bench_Cancel(void)
{
    feedback_pending = false;
    OutputGateway_Cancel(&gateway); /* Revoke output before starting any read-only tail. */
    bool read_stopped = S3ShoulderService_Get().state == S3_READER_OBSERVING
        || S3ShoulderService_Stop();
    if (!read_stopped && gateway.state != OUTPUT_FAULT) { OutputGateway_RejectRequest(&gateway); }
    invalidate_token();
}
bool S3Bench_AcknowledgeFault(uint32_t now_ms)
{
    SafetyInputs in; S3SiteSample site;
    if (!inputs(now_ms, &in, &site) || !SafetySupervisor_StopObserved(&gateway.config, &in)) { return false; }
    for (unsigned axis = 0U; axis < 2U; axis++) {
        if (!SafetySupervisor_AxisEnabled(&profile->safety, axis)) { continue; }
        const OutputDriverStatus *d = &site.driver[axis];
        if (!d->valid || d->axis_error != 0U || d->fault_flags != 0U
            || (axis == 0U && d->axis_state != 1U)
            || (uint32_t)(now_ms - d->received_ms) > gateway.config.feedback_timeout_ms) { return false; }
    }
    bool ok = OutputGateway_AcknowledgeFault(&gateway, &in, now_ms, true);
    invalidate_token(); return ok;
}
void S3Bench_GetStatus(S3BenchStatus *out)
{
    *out = (S3BenchStatus){gateway.state, gateway.reason, site_ready(),
        gateway.transport.submit != NULL && gateway.transport.purge != NULL,
        gateway.state == OUTPUT_ARMED || gateway.state == OUTPUT_ACTIVE,
        gateway.request_valid, gateway.stop_attempted, gateway.driver_phase,
        gateway.state == OUTPUT_ARMED || gateway.state == OUTPUT_ACTIVE ? arm_token : 0U,
        gateway.config.stop_policy, gateway.stop_accepted, gateway.purge_ok, gateway.stop_delivery_failed,
        feedback_pending, (unsigned)S3ShoulderService_Get().state, (unsigned)S3ShoulderService_Get().reason};
}
