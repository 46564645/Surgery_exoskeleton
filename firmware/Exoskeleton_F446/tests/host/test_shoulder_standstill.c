/* Pure receive-window cases. Thresholds below are TEST values, not site defaults. */
#include "test_shoulder_standstill.h"
#include "shoulder_standstill.h"
#include <assert.h>
#include <math.h>
#include <stddef.h>

static ShoulderPropertySnapshot pair(uint32_t request, float position, float velocity)
{
    ShoulderPropertySnapshot f = {0}; f.valid = true;
    f.value[GDS_PROPERTY_POSITION].real = position;
    f.value[GDS_PROPERTY_VELOCITY].real = velocity;
    f.requested_ms[GDS_PROPERTY_POSITION] = request;
    f.received_ms[GDS_PROPERTY_POSITION] = request + 1U;
    f.requested_ms[GDS_PROPERTY_VELOCITY] = request + 2U;
    f.received_ms[GDS_PROPERTY_VELOCITY] = request + 3U;
    return f;
}

static void observe(ShoulderStandstill *s, uint32_t request, float position, float velocity)
{
    ShoulderPropertySnapshot f = pair(request, position, velocity);
    ShoulderStandstill_Observe(s, &f, request + 3U);
}

static void restart(ShoulderStandstill *s, const ShoulderStandstillConfig *c, uint32_t event)
{
    ShoulderStandstill_Init(s, c); ShoulderStandstill_After(s, event);
}

void TestShoulderStandstill(void)
{
    const ShoulderStandstillConfig config = { 0.001f, 0.01f, 20U, 15U };
    ShoulderStandstill s;
    assert(ShoulderStandstill_ConfigValid(&config));
    assert(!ShoulderStandstill_ConfigValid(NULL));
    ShoulderStandstillConfig bad = {0};
    assert(!ShoulderStandstill_ConfigValid(&bad));
    for (unsigned i = 0U; i < 8U; i++) {
        bad = config;
        if (i == 0U) { bad.span_rev = NAN; }
        else if (i == 1U) { bad.span_rev = -1.0f; }
        else if (i == 2U) { bad.max_velocity_rev_s = INFINITY; }
        else if (i == 3U) { bad.max_velocity_rev_s = 0.0f; }
        else if (i == 4U) { bad.dwell_ms = 0U; }
        else if (i == 5U) { bad.dwell_ms = UINT32_C(0x80000000); }
        else if (i == 6U) { bad.freshness_ms = 0U; }
        else { bad.freshness_ms = UINT32_C(0x80000000); }
        assert(!ShoulderStandstill_ConfigValid(&bad));
    }
    ShoulderStandstill_Init(&s, &bad);
    ShoulderStandstill_After(&s, 100U); observe(&s, 101U, 1.0f, 0.0f);
    assert(!ShoulderStandstill_Read(&s, 104U).seen);
    ShoulderStandstill_Init(&s, &config);
    observe(&s, 101U, 1.0f, 0.0f); /* Explicit event required. */
    assert(!ShoulderStandstill_Read(&s, 104U).seen);

    restart(&s, &config, 100U);
    observe(&s, 100U, 1.0f, 0.0f); /* Equality/pre-event request, later replies. */
    assert(!ShoulderStandstill_Read(&s, 103U).seen);
    observe(&s, 99U, 1.0f, 0.0f);
    assert(!ShoulderStandstill_Read(&s, 102U).seen);
    observe(&s, 101U, 1.0f, 0.0f);
    ShoulderPropertySnapshot same = pair(101U, 1.0f, 0.0f);
    for (uint32_t now = 104U; now <= 116U; now++) {
        ShoulderStandstill_Observe(&s, &same, now);
        ShoulderStandstillStatus status = ShoulderStandstill_Read(&s, now);
        assert(status.fresh && status.dwell_ms == 0U && !status.stationary);
    }
    assert(!ShoulderStandstill_Read(&s, 117U).fresh); /* age is request-based. */
    assert(!ShoulderStandstill_Read(&s, 117U).stationary);

    restart(&s, &config, 100U);
    observe(&s, 101U, 1.0f, 0.0f);
    observe(&s, 111U, 1.0001f, -0.005f);
    ShoulderStandstillStatus status = ShoulderStandstill_Read(&s, 114U);
    assert(status.seen && status.fresh && !status.stationary && status.dwell_ms == 10U);
    observe(&s, 121U, 1.0002f, 0.0f);
    status = ShoulderStandstill_Read(&s, 124U);
    assert(status.stationary && status.dwell_ms == 20U && status.age_ms == 3U);
    assert(!ShoulderStandstill_Read(&s, 137U).stationary);
    observe(&s, 141U, 1.0002f, 0.0f); /* Gap cannot extend the old window. */
    assert(ShoulderStandstill_Read(&s, 144U).dwell_ms == 0U);
    ShoulderStandstill_After(&s, 150U);
    same = pair(141U, 1.0002f, 0.0f);
    ShoulderStandstill_Observe(&s, &same, 151U);
    assert(!ShoulderStandstill_Read(&s, 151U).seen);

    restart(&s, &config, 100U);
    observe(&s, 101U, 0.0f, 0.0f);
    observe(&s, 111U, 0.0006f, 0.0f);
    observe(&s, 121U, 0.0012f, 0.0f); /* Small per-step drift, excessive total span. */
    assert(ShoulderStandstill_Read(&s, 124U).dwell_ms == 0U);
    observe(&s, 131U, 0.0012f, 0.02f);
    assert(!ShoulderStandstill_Read(&s, 134U).stationary);
    observe(&s, 141U, 0.0012f, 0.0f);
    assert(ShoulderStandstill_Read(&s, 144U).dwell_ms == 0U);
    same = pair(141U, 0.002f, 0.0f); /* Modified payload under repeated identity. */
    ShoulderStandstill_Observe(&s, &same, 144U);
    assert(!ShoulderStandstill_Read(&s, 144U).seen);

    for (unsigned fault = 0U; fault < 10U; fault++) {
        restart(&s, &config, 100U);
        observe(&s, 101U, 1.0f, 0.0f);
        same = pair(111U, 1.0f, 0.0f);
        if (fault == 0U) { same.valid = false; }
        else if (fault == 1U) { same.value[GDS_PROPERTY_POSITION].real = NAN; }
        else if (fault == 2U) { same.value[GDS_PROPERTY_VELOCITY].real = INFINITY; }
        else if (fault == 3U) { same.requested_ms[GDS_PROPERTY_VELOCITY] = 100U; }
        else if (fault == 4U) { same.received_ms[GDS_PROPERTY_POSITION] = 100U; }
        else if (fault == 5U) { same = pair(99U, 1.0f, 0.0f); }
        else if (fault == 6U) { same.requested_ms[GDS_PROPERTY_POSITION] = 101U;
                               same.received_ms[GDS_PROPERTY_POSITION] = 102U; }
        else if (fault == 7U) { same.received_ms[GDS_PROPERTY_VELOCITY] = 112U; }
        else if (fault == 8U) { same.received_ms[GDS_PROPERTY_VELOCITY] = 115U; }
        else { same = pair(1U, 1.0f, 0.0f); }
        ShoulderStandstill_Observe(&s, &same, 114U);
        assert(!ShoulderStandstill_Read(&s, 114U).seen);
    }
    restart(&s, &config, 100U);
    observe(&s, 111U, 1.0f, 0.0f);
    same = pair(110U, 1.0f, 0.0f); /* Fresh but backwards, both after event. */
    ShoulderStandstill_Observe(&s, &same, 114U);
    assert(!ShoulderStandstill_Read(&s, 114U).seen);
    ShoulderStandstill_Observe(&s, &same, 114U);
    assert(!ShoulderStandstill_Read(&s, 114U).seen); /* Rejected replay stays rejected. */

    /* Counter wrap is valid; dwell saturates and does not grow on polling. */
    restart(&s, &config, UINT32_MAX - 15U);
    observe(&s, UINT32_MAX - 14U, 0.0f, 0.0f);
    observe(&s, UINT32_MAX - 4U, 0.0f, 0.0f);
    observe(&s, 5U, 0.0f, 0.0f);
    status = ShoulderStandstill_Read(&s, 8U);
    assert(status.stationary && status.dwell_ms == 20U);
    observe(&s, 15U, 0.0f, 0.0f);
    assert(ShoulderStandstill_Read(&s, 18U).dwell_ms == 20U);
    ShoulderStandstill_Observe(&s, NULL, 18U);
    assert(!ShoulderStandstill_Read(&s, 18U).seen);
}
