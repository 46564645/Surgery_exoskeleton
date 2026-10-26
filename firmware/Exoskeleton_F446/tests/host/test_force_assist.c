/* Synthetic interaction-force trigger cases; no hardware and no CAN frames. */
#include "test_force_assist.h"
#include "force_assist.h"
#include <assert.h>
#include <math.h>
#include <string.h>

static ForceAssistConfig config(void)
{
    ForceAssistConfig c = {0};
    c.c0 = 0.2f; c.c1 = 0.8f; c.c2 = 0.3f;
    c.on_n = 0.5f; c.off_n = 0.25f;
    c.boost_pct = 50.0f;
    c.ramp_ms = 200U; c.max_hold_ms = 3000U; c.freshness_ms = 150U;
    c.confirmed = true;
    return c;
}

static void config_checks(void)
{
    ForceAssistConfig c = config();
    assert(ForceAssist_ConfigValid(&c));
    c.confirmed = false;
    assert(!ForceAssist_ConfigValid(&c));
    c = config(); c.on_n = 0.2f; c.off_n = 0.3f;
    assert(!ForceAssist_ConfigValid(&c)); /* off must be below on */
    c = config(); c.boost_pct = 51.0f;
    assert(!ForceAssist_ConfigValid(&c));
    c = config(); c.boost_pct = 0.0f;
    assert(!ForceAssist_ConfigValid(&c));
    c = config(); c.ramp_ms = 0U;
    assert(!ForceAssist_ConfigValid(&c));
    c = config(); c.max_hold_ms = 0U;
    assert(!ForceAssist_ConfigValid(&c));
    c = config(); c.freshness_ms = 0U;
    assert(!ForceAssist_ConfigValid(&c));
    c = config(); c.c1 = NAN;
    assert(!ForceAssist_ConfigValid(&c));
    assert(!ForceAssist_ConfigValid(NULL));
}

static void expected_model(void)
{
    ForceAssistConfig c = config();
    assert(fabsf(ForceAssist_Expected(&c, 0.0f, 0.0f) - 0.2f) < 0.000001f);
    assert(fabsf(ForceAssist_Expected(&c, 1.57079633f, 0.0f) - 1.3f) < 0.00001f);
    assert(fabsf(ForceAssist_Expected(&c, 0.0f, 1.57079633f) - 0.5f) < 0.00001f);
    assert(ForceAssist_Expected(NULL, 0.0f, 0.0f) == 0.0f);
    assert(ForceAssist_Expected(&c, NAN, 0.0f) == 0.0f);
}

static void trigger_and_hysteresis(void)
{
    ForceAssistConfig c = config();
    ForceAssistState s;
    ForceAssist_Reset(&s, 0U);
    assert(s.gain == 1.0f && !s.active);
    /* Below the threshold: no trigger. */
    assert(!ForceAssist_Update(&s, &c, true, 0.2f + 0.3f, 0.0f, 0.0f, 10U));
    assert(s.gain == 1.0f);
    /* Above: trigger and ramp up to 1.5 over 200 ms. */
    assert(ForceAssist_Update(&s, &c, true, 0.2f + 0.9f, 0.0f, 0.0f, 20U));
    assert(s.active && s.triggers == 1U);
    for (uint32_t t = 30U; t <= 220U; t += 10U) {
        (void)ForceAssist_Update(&s, &c, true, 0.2f + 0.9f, 0.0f, 0.0f, t);
    }
    assert(fabsf(s.gain - 1.5f) < 0.0001f);
    /* Falling into the hysteresis band keeps the boost. */
    (void)ForceAssist_Update(&s, &c, true, 0.2f + 0.3f, 0.0f, 0.0f, 300U);
    assert(s.active);
    /* Below off threshold: release and ramp back. */
    (void)ForceAssist_Update(&s, &c, true, 0.2f + 0.1f, 0.0f, 0.0f, 400U);
    assert(!s.active);
    for (uint32_t t = 410U; t <= 620U; t += 10U) {
        (void)ForceAssist_Update(&s, &c, true, 0.2f + 0.1f, 0.0f, 0.0f, t);
    }
    assert(fabsf(s.gain - 1.0f) < 0.0001f);
}

static void hold_limit_and_invalid(void)
{
    ForceAssistConfig c = config();
    ForceAssistState s;
    ForceAssist_Reset(&s, 0U);
    assert(ForceAssist_Update(&s, &c, true, 0.2f + 0.9f, 0.0f, 0.0f, 1000U));
    /* A constant force must not hold the boost past max_hold_ms. */
    (void)ForceAssist_Update(&s, &c, true, 0.2f + 0.9f, 0.0f, 0.0f, 1000U + 2999U);
    assert(s.active);
    (void)ForceAssist_Update(&s, &c, true, 0.2f + 0.9f, 0.0f, 0.0f, 1000U + 3000U);
    assert(!s.active);
    /* Re-entry needs the force to fall below the off threshold first. */
    (void)ForceAssist_Update(&s, &c, true, 0.2f + 0.9f, 0.0f, 0.0f, 5000U);
    assert(s.active); /* A new entry is allowed after the hold expired. */
    /* Invalid force drops the boost immediately and never latches. */
    (void)ForceAssist_Update(&s, &c, false, 0.0f, 0.0f, 0.0f, 5100U);
    assert(!s.active && s.gain == 1.0f);
    assert(!ForceAssist_Update(&s, &c, true, NAN, 0.0f, 0.0f, 5200U));
    assert(!ForceAssist_Update(&s, NULL, true, 1.0f, 0.0f, 0.0f, 5300U));
    assert(ForceAssist_Gain(NULL) == 1.0f);
}

static void direction_and_reference(void)
{
    ForceAssistConfig c = config();
    ForceAssistState s;
    ForceAssist_Reset(&s, 0U);
    /* Negative error (pressing down) also triggers; the caller decides direction. */
    assert(ForceAssist_Update(&s, &c, true, 0.2f - 0.9f, 0.0f, 0.0f, 10U));
    assert(s.active && s.last_error_n < -0.5f);
    /* The frozen reference ignores a pose change while active. */
    ForceAssistState s2;
    ForceAssist_Reset(&s2, 0U);
    assert(ForceAssist_Update(&s2, &c, true, 1.1f, 0.0f, 0.0f, 10U)); /* F0=0.2 */
    float ref = s2.ref_force;
    (void)ForceAssist_Update(&s2, &c, true, 1.1f, 1.57079633f, 0.0f, 20U); /* F0=1.0 */
    assert(s2.active && s2.ref_force == ref);
}

void TestForceAssist_Run(const char *scenario)
{
    if (!strcmp(scenario, "force_assist_config")) { config_checks(); }
    else if (!strcmp(scenario, "force_assist_model")) { expected_model(); }
    else if (!strcmp(scenario, "force_assist_trigger")) { trigger_and_hysteresis(); }
    else if (!strcmp(scenario, "force_assist_hold")) { hold_limit_and_invalid(); }
    else { assert(!strcmp(scenario, "force_assist_reference")); direction_and_reference(); }
}
