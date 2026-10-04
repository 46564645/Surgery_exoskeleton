/* L3: ExoKit range/all-active-joints shutdown semantics, strict confirmed config. */
#include "safety_supervisor.h"
#include <math.h>
#include <stddef.h>

static bool duration_valid(uint32_t value)
{ return value > 0U && value < UINT32_C(0x80000000); }

bool SafetySupervisor_AxisEnabled(const SafetyConfig *c, unsigned axis)
{ return c != NULL && axis < SAFETY_JOINT_COUNT && (c->enabled_axes & (1U << axis)) != 0U; }

bool SafetySupervisor_ConfigReady(const SafetyConfig *c)
{
    if (c == NULL || c->enabled_axes == 0U || (c->enabled_axes & ~SAFETY_ALL_AXES) != 0U
        || (c->stop_policy != SAFETY_STOP_EXTERNAL_INPUT
            && c->stop_policy != SAFETY_STOP_SUPERVISED_C620_BENCH
            && c->stop_policy != SAFETY_STOP_SUPERVISED_SHOULDER_BENCH)
        || (c->stop_policy == SAFETY_STOP_SUPERVISED_C620_BENCH
            && c->enabled_axes != SAFETY_ELBOW_AXES)
        || (c->stop_policy == SAFETY_STOP_SUPERVISED_SHOULDER_BENCH
            && c->enabled_axes != SAFETY_SHOULDER_AXES)
        || !c->protection_confirmed
        || !c->stop_method_confirmed || !c->timing_confirmed
        || !duration_valid(c->feedback_timeout_ms) || !duration_valid(c->service_limit_ms)
        || !duration_valid(c->max_request_ms)) { return false; }
    for (unsigned i = 0U; i < SAFETY_JOINT_COUNT; i++) {
        if (!SafetySupervisor_AxisEnabled(c, i)) { continue; }
        const SafetyJointConfig *j = &c->joint[i];
        const JointMapping *m = &j->mapping;
        bool envelope = j->relative_native_envelope
            ? i == 0U && c->stop_policy == SAFETY_STOP_SUPERVISED_SHOULDER_BENCH
                && j->software_envelope_approved && !j->limits_confirmed
                && m->direction == 1 && m->rotor_per_joint == 1.0f && m->rotor_zero_rad == 0.0f
            : j->limits_confirmed && !j->software_envelope_approved;
        if (!envelope || !j->feedback_confirmed || !j->protocol_confirmed || !m->confirmed || (m->direction != 1 && m->direction != -1)
            || !isfinite(m->rotor_per_joint) || m->rotor_per_joint <= 0.0f
            || !isfinite(m->rotor_zero_rad) || !isfinite(j->lower_rad) || !isfinite(j->upper_rad)
            || j->lower_rad >= j->upper_rad || !isfinite(j->max_speed_rad_s)
            || j->max_speed_rad_s <= 0.0f || !isfinite(j->max_motor_command)
            || j->max_motor_command <= 0.0f || !isfinite(j->motor_slew_per_s)
            || j->motor_slew_per_s <= 0.0f) { return false; }
    }
    /* Codec full scale only. No analogous assumed GDS runtime limit. */
    return !SafetySupervisor_AxisEnabled(c, 1U) || c->joint[1].max_motor_command <= 20.0f;
}

SafetyReason SafetySupervisor_Check(const SafetyConfig *c, const SafetyInputs *in, uint32_t now_ms)
{
    if (!SafetySupervisor_ConfigReady(c)) { return SAFETY_UNCONFIGURED; }
    if (in == NULL || !in->transport_healthy) { return SAFETY_TRANSPORT; }
    if (c->stop_policy == SAFETY_STOP_EXTERNAL_INPUT && !in->operator_stop_clear) {
        return SAFETY_STOP_INPUT;
    }
    if (!in->drivers_ready) { return SAFETY_DRIVER_NOT_READY; }
    for (unsigned i = 0U; i < SAFETY_JOINT_COUNT; i++) {
        if (!SafetySupervisor_AxisEnabled(c, i)) { continue; }
        const JointState *s = &in->joint[i];
        const SafetyJointConfig *j = &c->joint[i];
        if (!s->valid || !isfinite(s->position_rad) || !isfinite(s->velocity_rad_s)
            || (uint32_t)(now_ms - s->received_ms) > c->feedback_timeout_ms) {
            return SAFETY_FEEDBACK;
        }
        if (s->position_rad < j->lower_rad || s->position_rad > j->upper_rad) { return SAFETY_RANGE; }
        if (fabsf(s->velocity_rad_s) > j->max_speed_rad_s) { return SAFETY_SPEED; }
    }
    return SAFETY_OK;
}

bool SafetySupervisor_StopObserved(const SafetyConfig *c, const SafetyInputs *in)
{
    if (c == NULL || in == NULL) { return false; }
    if (c->stop_policy == SAFETY_STOP_SUPERVISED_C620_BENCH) {
        return c->enabled_axes == SAFETY_ELBOW_AXES && in->feedback_stationary;
    }
    if (c->stop_policy == SAFETY_STOP_SUPERVISED_SHOULDER_BENCH) {
        return c->enabled_axes == SAFETY_SHOULDER_AXES && in->feedback_stationary;
    }
    return c->stop_policy == SAFETY_STOP_EXTERNAL_INPUT && in->independently_stopped;
}

SafetyReason SafetySupervisor_CheckBeforePrepare(const SafetyConfig *c, const SafetyInputs *in, uint32_t now_ms)
{
    if (in == NULL) { return SAFETY_TRANSPORT; }
    SafetyInputs precheck = *in;
    precheck.drivers_ready = true; /* Skip this one dependency, not an assertion. */
    return SafetySupervisor_Check(c, &precheck, now_ms);
}
