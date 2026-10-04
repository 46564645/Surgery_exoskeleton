/* Complete associated property pairs; no TX, HAL, manual permit or sensor fiction. */
#include "shoulder_standstill.h"
#include <math.h>
#include <stddef.h>
#include <string.h>

static bool duration_valid(uint32_t value)
{ return value > 0U && value < UINT32_C(0x80000000); }

bool ShoulderStandstill_ConfigValid(const ShoulderStandstillConfig *config)
{
    return config != NULL && isfinite(config->span_rev) && config->span_rev > 0.0f
        && isfinite(config->max_velocity_rev_s) && config->max_velocity_rev_s > 0.0f
        && duration_valid(config->dwell_ms) && duration_valid(config->freshness_ms);
}

void ShoulderStandstill_Init(ShoulderStandstill *s, const ShoulderStandstillConfig *config)
{
    if (s == NULL) { return; }
    memset(s, 0, sizeof(*s));
    if (ShoulderStandstill_ConfigValid(config)) {
        s->config = *config; s->configured = true;
    }
}

void ShoulderStandstill_After(ShoulderStandstill *s, uint32_t event_ms)
{
    if (s == NULL) { return; }
    ShoulderStandstillConfig config = s->config;
    ShoulderStandstill_Init(s, &config);
    s->after_ms = event_ms; s->fenced = true;
}

static void clear_window(ShoulderStandstill *s)
{
    /* Retain timestamp high-water marks: rejected data must not admit a replay. */
    s->seen = s->tracking = false; s->dwell_ms = 0U;
    s->minimum_rev = s->maximum_rev = 0.0f;
}

static uint32_t pair_age(const ShoulderStandstill *s, uint32_t now_ms)
{
    uint32_t oldest = 0U;
    for (unsigned i = 0U; i < 2U; i++) {
        uint32_t request_age = now_ms - s->requested_ms[i];
        uint32_t receive_age = now_ms - s->received_ms[i];
        if (request_age > oldest) { oldest = request_age; }
        if (receive_age > oldest) { oldest = receive_age; }
    }
    return oldest;
}

static bool strictly_after(uint32_t timestamp, uint32_t event_ms)
{
    uint32_t delta = timestamp - event_ms;
    return delta != 0U && delta < UINT32_C(0x80000000);
}

static void start_window(ShoulderStandstill *s, float position_rev)
{
    for (unsigned i = 0U; i < 2U; i++) {
        s->start_requested_ms[i] = s->requested_ms[i];
        s->start_received_ms[i] = s->received_ms[i];
    }
    s->minimum_rev = s->maximum_rev = position_rev;
    s->dwell_ms = 0U; s->tracking = true;
}

void ShoulderStandstill_Observe(ShoulderStandstill *s,
                                const ShoulderPropertySnapshot *f, uint32_t now_ms)
{
    if (s == NULL) { return; }
    if (!s->configured || !s->fenced || f == NULL || !f->valid) {
        clear_window(s); return;
    }
    const GdsPropertyField fields[2] = { GDS_PROPERTY_POSITION, GDS_PROPERTY_VELOCITY };
    uint32_t request[2], receive[2];
    float position = f->value[GDS_PROPERTY_POSITION].real;
    float velocity = f->value[GDS_PROPERTY_VELOCITY].real;
    if (!isfinite(position) || !isfinite(velocity)) { clear_window(s); return; }
    for (unsigned i = 0U; i < 2U; i++) {
        request[i] = f->requested_ms[fields[i]];
        receive[i] = f->received_ms[fields[i]];
        if (!strictly_after(request[i], s->after_ms)
            || !strictly_after(receive[i], s->after_ms)
            || (uint32_t)(now_ms - request[i]) > s->config.freshness_ms
            || (uint32_t)(now_ms - receive[i]) > s->config.freshness_ms
            || (uint32_t)(receive[i] - request[i]) >= UINT32_C(0x80000000)) {
            clear_window(s); return;
        }
    }
    /* Existing associated provider reads position then velocity in one cycle. */
    if ((uint32_t)(request[1] - receive[0]) >= UINT32_C(0x80000000)) {
        clear_window(s); return;
    }
    bool changed[2] = { true, true };
    bool gap_expired = false;
    if (s->has_pair) {
        for (unsigned i = 0U; i < 2U; i++) {
            uint32_t request_gap = request[i] - s->requested_ms[i];
            uint32_t receive_gap = receive[i] - s->received_ms[i];
            if (request_gap >= UINT32_C(0x80000000)
                || receive_gap >= UINT32_C(0x80000000)) { clear_window(s); return; }
            changed[i] = request_gap != 0U || receive_gap != 0U;
            gap_expired = gap_expired || request_gap > s->config.freshness_ms
                || receive_gap > s->config.freshness_ms;
        }
        if (!changed[0] && !changed[1]) {
            /* Changed data under identical pair identity is not a new observation. */
            if (position != s->last_position_rev || velocity != s->last_velocity_rev_s
                || fabsf(velocity) > s->config.max_velocity_rev_s) { clear_window(s); }
            return;
        }
        if (!changed[0] || !changed[1]) { clear_window(s); return; }
    }
    if (gap_expired) { clear_window(s); }
    for (unsigned i = 0U; i < 2U; i++) {
        s->requested_ms[i] = request[i]; s->received_ms[i] = receive[i];
    }
    s->last_position_rev = position; s->last_velocity_rev_s = velocity;
    s->has_pair = s->seen = true;
    if (fabsf(velocity) > s->config.max_velocity_rev_s) {
        s->tracking = false; s->dwell_ms = 0U; return;
    }
    if (!s->tracking) { start_window(s, position); return; }
    float minimum = position < s->minimum_rev ? position : s->minimum_rev;
    float maximum = position > s->maximum_rev ? position : s->maximum_rev;
    float span = maximum - minimum;
    if (!isfinite(span) || span > s->config.span_rev) { start_window(s, position); return; }
    s->minimum_rev = minimum; s->maximum_rev = maximum;
    if (s->dwell_ms < s->config.dwell_ms) {
        /* Take the shortest of four observed spans: asynchronous reads or reply
         * latency must not lengthen the position/velocity evidence window. */
        uint32_t dwell = s->config.dwell_ms;
        for (unsigned i = 0U; i < 2U; i++) {
            uint32_t request_span = request[i] - s->start_requested_ms[i];
            uint32_t receive_span = receive[i] - s->start_received_ms[i];
            if (request_span >= UINT32_C(0x80000000)
                || receive_span >= UINT32_C(0x80000000)) { clear_window(s); return; }
            if (request_span < dwell) { dwell = request_span; }
            if (receive_span < dwell) { dwell = receive_span; }
        }
        s->dwell_ms = dwell;
    }
}

ShoulderStandstillStatus ShoulderStandstill_Read(ShoulderStandstill *s, uint32_t now_ms)
{
    ShoulderStandstillStatus out = {0};
    if (s == NULL || !s->configured || !s->seen) { return out; }
    out.seen = true; out.age_ms = pair_age(s, now_ms);
    out.fresh = out.age_ms <= s->config.freshness_ms;
    if (!out.fresh) { s->tracking = false; s->dwell_ms = 0U; }
    if (s->tracking) { out.dwell_ms = s->dwell_ms; }
    out.stationary = out.fresh && s->tracking && out.dwell_ms >= s->config.dwell_ms;
    return out;
}
