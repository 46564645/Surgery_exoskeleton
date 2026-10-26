#ifndef FORCE_ASSIST_H
#define FORCE_ASSIST_H
#include <stdbool.h>
#include <stdint.h>

/* Interaction-force triggered bounded boost (IROS 2024 force-triggered idea).
 * The user's voluntary force is the measured cuff force minus the passive
 * component F0(q); a short force change beyond a threshold adds a bounded
 * increment on top of the gravity baseline. Force never gates safety. */
typedef struct {
    float c0, c1, c2;      /* F0(q) = c0 + c1*sin(q_sh) + c2*sin(q_sh+q_el) */
    float on_n;            /* enter threshold, N */
    float off_n;           /* exit threshold, N (hysteresis) */
    float boost_pct;       /* 0..50 percent of the gravity baseline */
    uint32_t ramp_ms;      /* gain ramp in and out */
    uint32_t max_hold_ms;  /* longest single boost */
    uint32_t freshness_ms; /* force sample age limit */
    bool confirmed;        /* bench-calibrated coefficients, not generic */
} ForceAssistConfig;

typedef struct {
    bool active;
    float ref_force;       /* F0 frozen at entry */
    float gain;            /* 1.0 .. 1 + boost_pct/100 */
    float last_error_n;    /* latest e_F for telemetry */
    uint32_t entered_ms;
    uint32_t ramp_ms;
    uint16_t triggers;
} ForceAssistState;

bool ForceAssist_ConfigValid(const ForceAssistConfig *config);
float ForceAssist_Expected(const ForceAssistConfig *config, float q_sh_rad, float q_el_rad);
void ForceAssist_Reset(ForceAssistState *state, uint32_t now_ms);
/* force_valid false drops the boost immediately; it never latches a fault. */
bool ForceAssist_Update(ForceAssistState *state, const ForceAssistConfig *config,
                        bool force_valid, float force_n,
                        float q_sh_rad, float q_el_rad, uint32_t now_ms);
float ForceAssist_Gain(const ForceAssistState *state);
#endif
