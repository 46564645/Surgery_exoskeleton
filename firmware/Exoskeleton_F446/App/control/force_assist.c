#include "force_assist.h"
#include <math.h>
#include <stddef.h>

bool ForceAssist_ConfigValid(const ForceAssistConfig *config)
{
    if (config == NULL || !config->confirmed) { return false; }
    if (!isfinite(config->c0) || !isfinite(config->c1) || !isfinite(config->c2)) { return false; }
    if (!isfinite(config->on_n) || !isfinite(config->off_n)
        || config->on_n <= 0.0f || config->off_n < 0.0f
        || config->off_n >= config->on_n) { return false; }
    if (!isfinite(config->boost_pct) || config->boost_pct <= 0.0f
        || config->boost_pct > 50.0f) { return false; }
    return config->ramp_ms > 0U && config->ramp_ms < UINT32_C(0x80000000)
        && config->max_hold_ms > 0U && config->max_hold_ms < UINT32_C(0x80000000)
        && config->freshness_ms > 0U && config->freshness_ms < UINT32_C(0x80000000);
}

float ForceAssist_Expected(const ForceAssistConfig *config, float q_sh_rad, float q_el_rad)
{
    if (config == NULL || !isfinite(q_sh_rad) || !isfinite(q_el_rad)) { return 0.0f; }
    return config->c0 + config->c1 * sinf(q_sh_rad) + config->c2 * sinf(q_sh_rad + q_el_rad);
}

void ForceAssist_Reset(ForceAssistState *state, uint32_t now_ms)
{
    if (state == NULL) { return; }
    state->active = false;
    state->ref_force = 0.0f;
    state->gain = 1.0f;
    state->last_error_n = 0.0f;
    state->entered_ms = now_ms;
    state->ramp_ms = now_ms;
    state->triggers = 0U;
}

bool ForceAssist_Update(ForceAssistState *state, const ForceAssistConfig *config,
                        bool force_valid, float force_n,
                        float q_sh_rad, float q_el_rad, uint32_t now_ms)
{
    if (state == NULL) { return false; }
    if (config == NULL || !ForceAssist_ConfigValid(config) || !force_valid
        || !isfinite(force_n)) {
        /* Missing or invalid force drops the increment at once; it is not a fault. */
        state->active = false;
        state->gain = 1.0f;
        state->ramp_ms = now_ms;
        return false;
    }
    float expected = ForceAssist_Expected(config, q_sh_rad, q_el_rad);
    float error_n = force_n - expected;
    state->last_error_n = error_n;
    if (!state->active) {
        if (fabsf(error_n) > config->on_n) {
            state->active = true;
            state->ref_force = expected;
            state->entered_ms = now_ms;
            if (state->triggers < UINT16_MAX) { state->triggers++; }
        }
    } else {
        bool expired = (uint32_t)(now_ms - state->entered_ms) >= config->max_hold_ms;
        float held_n = force_n - state->ref_force;
        if (expired || fabsf(held_n) < config->off_n) { state->active = false; }
    }
    float target = state->active ? 1.0f + config->boost_pct / 100.0f : 1.0f;
    float span = config->boost_pct / 100.0f;
    float step = span * (float)(uint32_t)(now_ms - state->ramp_ms) / (float)config->ramp_ms;
    if (state->gain < target) {
        state->gain = fminf(target, state->gain + step);
    } else if (state->gain > target) {
        state->gain = fmaxf(target, state->gain - step);
    }
    state->ramp_ms = now_ms;
    return state->active;
}

float ForceAssist_Gain(const ForceAssistState *state)
{ return state == NULL ? 1.0f : state->gain; }
