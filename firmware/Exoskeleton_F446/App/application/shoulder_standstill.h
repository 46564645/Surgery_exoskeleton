#ifndef SHOULDER_STANDSTILL_H
#define SHOULDER_STANDSTILL_H
/* L4 receive-only bench evidence, NOT power-off, zero-torque or independent stop. */
#include "shoulder_property_feedback.h"

typedef struct {
    float span_rev, max_velocity_rev_s;
    uint32_t dwell_ms, freshness_ms;
} ShoulderStandstillConfig;

typedef struct {
    ShoulderStandstillConfig config;
    uint32_t after_ms;
    uint32_t requested_ms[2], received_ms[2];
    uint32_t start_requested_ms[2], start_received_ms[2];
    uint32_t dwell_ms;
    float minimum_rev, maximum_rev, last_position_rev, last_velocity_rev_s;
    bool configured, fenced, has_pair, seen, tracking;
} ShoulderStandstill;

typedef struct {
    uint32_t dwell_ms, age_ms;
    bool seen, fresh, stationary;
} ShoulderStandstillStatus;

bool ShoulderStandstill_ConfigValid(const ShoulderStandstillConfig *config);
/* Copies explicit thresholds. Zero/default config is disabled; no defaults. */
void ShoulderStandstill_Init(ShoulderStandstill *observer,
                             const ShoulderStandstillConfig *config);
/* A new event clears all prior evidence; Observe requires this explicit fence. */
void ShoulderStandstill_After(ShoulderStandstill *observer, uint32_t event_ms);
/* Only new position/velocity pairs can extend dwell. All four request/receive
 * timestamps must be fresh, causal and strictly after the event, across wrap.
 * Repeated polling of one unchanged pair does not refresh age or add dwell. */
void ShoulderStandstill_Observe(ShoulderStandstill *observer,
                                const ShoulderPropertySnapshot *snapshot,
                                uint32_t now_ms);
ShoulderStandstillStatus ShoulderStandstill_Read(ShoulderStandstill *observer,
                                               uint32_t now_ms);
#endif
