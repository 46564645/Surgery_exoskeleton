#include "test_s4_gravity.h"
#include "gravity_compensation.h"
#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

void TestS4Gravity_Run(const char *scenario)
{
    assert(strcmp(scenario, "s4_gravity") == 0);
    const float half_pi = 1.5707963267948966f;
    GravityCompensationConfig model = {
        .amplitude_nm = 2.0f, .gravity_zero_rad = 0.25f,
        .sign_g = 1, .confirmed = true
    };
    JointState state = {.position_rad = 0.25f, .velocity_rad_s = 0.0f,
                        .received_ms = 10U, .valid = true};
    float torque = 9.0f;
    assert(GravityCompensation_Compute(&model, &state, &torque));
    assert(fabsf(torque) < 0.00001f);

    state.position_rad = model.gravity_zero_rad + half_pi;
    assert(GravityCompensation_Compute(&model, &state, &torque));
    assert(fabsf(torque - 2.0f) < 0.00001f);
    state.position_rad = model.gravity_zero_rad - half_pi;
    assert(GravityCompensation_Compute(&model, &state, &torque));
    assert(fabsf(torque + 2.0f) < 0.00001f);
    model.sign_g = -1;
    assert(GravityCompensation_Compute(&model, &state, &torque));
    assert(fabsf(torque - 2.0f) < 0.00001f);

    model.confirmed = false;
    torque = 9.0f;
    assert(!GravityCompensation_Compute(&model, &state, &torque) && torque == 0.0f);
    model.confirmed = true;
    model.amplitude_nm = 0.0f;
    assert(!GravityCompensation_Compute(&model, &state, &torque));
    model.amplitude_nm = 2.0f;
    model.gravity_zero_rad = NAN;
    assert(!GravityCompensation_Compute(&model, &state, &torque));
    model.gravity_zero_rad = 0.25f;
    model.sign_g = 0;
    assert(!GravityCompensation_Compute(&model, &state, &torque));
    model.sign_g = 1;
    state.velocity_rad_s = INFINITY;
    assert(!GravityCompensation_Compute(&model, &state, &torque));
    state.velocity_rad_s = 0.0f;
    state.valid = false;
    assert(!GravityCompensation_Compute(&model, &state, &torque));
    state.valid = true;
    assert(!GravityCompensation_Compute(NULL, &state, &torque));
    assert(!GravityCompensation_Compute(&model, NULL, &torque));
    assert(!GravityCompensation_Compute(&model, &state, NULL));
}
