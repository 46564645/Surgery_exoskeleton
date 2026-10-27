/* Synthetic composition cases for the extracted S5/R5 assist function.
 * No devices, no CAN, no site image: values below are test numbers only. */
#include "test_assist_compose.h"
#include "s5_assist_compose.h"
#include <assert.h>
#include <math.h>
#include <string.h>

#define PI 3.14159265358979f

static JointState joint(float position_rad, float velocity_rad_s)
{
    JointState state = {0};
    state.position_rad = position_rad;
    state.velocity_rad_s = velocity_rad_s;
    state.valid = true;
    return state;
}

/* Everything the config points at has to stay alive for the call. */
typedef struct {
    GravityCompensationConfig shoulder_gravity;
    GravityCompensationConfig elbow_gravity;
    S5ElbowSupportConfig elbow_support;
    ShoulderAmplifyConfig amplify;
    ShoulderAmplifyConfig elbow_amplify;
    ForceAssistConfig force;
    S5AssistComposeConfig config;
    ShoulderAmplify amplify_state;
    ShoulderAmplify elbow_amplify_state;
    ForceAssistState force_state;
} Fixture;

/* Fill in place: the config points at this fixture's own members, so the
 * fixture must not be returned by value (the pointers would dangle). */
static void fixture_init(Fixture *f)
{
    *f = (Fixture){0};
    f->shoulder_gravity = (GravityCompensationConfig){1.64f, 0.0f, 1, true};
    f->elbow_gravity = (GravityCompensationConfig){0.04f, 0.0f, 1, true};
    f->elbow_support = (S5ElbowSupportConfig){1.0f, 2.0f, true};
    f->amplify = (ShoulderAmplifyConfig){0.0f, 0.34906585f, 400U, 200U};
    /* Elbow Amplify off in the existing cases; one case below turns it on. */
    f->elbow_amplify = (ShoulderAmplifyConfig){0.0f, 0.34906585f, 400U, 200U};
    f->force = (ForceAssistConfig){0};
    f->config = (S5AssistComposeConfig){
        .dual = true,
        .shoulder_gravity = &f->shoulder_gravity,
        .elbow_gravity = &f->elbow_gravity,
        .elbow_support = &f->elbow_support,
        .shoulder_amplify = &f->amplify,
        .elbow_amplify = &f->elbow_amplify,
        .force_assist = &f->force,
        .shoulder_gain = 1.0f,
        .elbow_motor_kt_nm_per_a = 0.02f,
        .shoulder_rotor_per_joint = 8.0f,
        .shoulder_rotor_cap_nm = 0.205f,
        .elbow_current_cap_a = 20.0f
    };
    ShoulderAmplify_Reset(&f->amplify_state, 0U);
    ShoulderAmplify_Reset(&f->elbow_amplify_state, 0U);
    ForceAssist_Reset(&f->force_state, 0U);
}

static bool update(Fixture *f, JointState shoulder, JointState elbow,
                   bool assist_enabled, bool force_valid, float force_n,
                   uint32_t now_ms, S5AssistComposeOutput *out)
{
    return S5AssistCompose_Update(&f->config, &shoulder, &elbow, assist_enabled,
                                  force_valid, force_n, &f->amplify_state,
                                  &f->elbow_amplify_state, &f->force_state, now_ms, out);
}

static void gravity_baseline(void)
{
    Fixture f;
    fixture_init(&f);
    S5AssistComposeOutput out;
    assert(update(&f, joint(PI / 2.0f, 0.0f), joint(0.0f, 0.0f), true, false, 0.0f, 0U, &out));
    /* 1.64 Nm joint / 8 = 0.205 rotor, exactly the cap; elbow 0.04 Nm / 0.02 = 2 A. */
    assert(fabsf(out.shoulder_rotor_nm - 0.205f) < 1e-6f);
    assert(fabsf(out.elbow_current_a - 2.0f) < 1e-5f);

    assert(update(&f, joint(PI / 6.0f, 0.0f), joint(0.0f, 0.0f), true, false, 0.0f, 0U, &out));
    assert(fabsf(out.shoulder_rotor_nm - 0.1025f) < 1e-5f);
    assert(fabsf(out.elbow_current_a - 1.0f) < 1e-5f);

    assert(update(&f, joint(0.0f, 0.0f), joint(0.0f, 0.0f), true, false, 0.0f, 0U, &out));
    assert(out.shoulder_rotor_nm == 0.0f && out.elbow_current_a == 0.0f);

    /* Elbow angle alone moves only the elbow term: 0.04*sin(90)/0.02 = 2 A. */
    assert(update(&f, joint(0.0f, 0.0f), joint(PI / 2.0f, 0.0f), true, false, 0.0f, 0U, &out));
    assert(out.shoulder_rotor_nm == 0.0f);
    assert(fabsf(out.elbow_current_a - 2.0f) < 1e-5f);
}

/* Elbow Amplify: same module as the shoulder, increment in amps, sign follows
 * the elbow movement, so a strike (extension, negative velocity) is helped. */
static void elbow_amplify_follows_movement(void)
{
    Fixture f;
    fixture_init(&f);
    f.elbow_amplify.increment_rotor_nm = 3.0f;   /* 3 A */
    S5AssistComposeOutput out;

    /* Fast extension (a strike): the increment ramps in over ramp_ms and
     * follows the negative velocity. */
    for (uint32_t t = 0U; t <= 400U; t += 20U) {
        assert(update(&f, joint(0.0f, 0.0f), joint(0.0f, -1.0f), true, false, 0.0f, t, &out));
    }
    assert(fabsf(out.elbow_current_a + 3.0f) < 1e-4f);

    /* Fast flexion: the sign follows the movement. */
    for (uint32_t t = 420U; t <= 900U; t += 20U) {
        assert(update(&f, joint(0.0f, 0.0f), joint(0.0f, 1.0f), true, false, 0.0f, t, &out));
    }
    assert(fabsf(out.elbow_current_a - 3.0f) < 1e-4f);

    /* Standing still: the increment ramps back out. */
    for (uint32_t t = 920U; t <= 1600U; t += 20U) {
        assert(update(&f, joint(0.0f, 0.0f), joint(0.0f, 0.0f), true, false, 0.0f, t, &out));
    }
    assert(fabsf(out.elbow_current_a) < 1e-4f);
}

static void output_caps(void)
{
    Fixture f;
    fixture_init(&f);
    f.shoulder_gravity.amplitude_nm = 100.0f;
    f.elbow_gravity.amplitude_nm = 10.0f;
    S5AssistComposeOutput out;
    assert(update(&f, joint(PI / 2.0f, 0.0f), joint(0.0f, 0.0f), true, false, 0.0f, 0U, &out));
    assert(out.shoulder_rotor_nm == 0.205f);
    assert(out.elbow_current_a == 20.0f);
}

static void assist_gate(void)
{
    Fixture f;
    fixture_init(&f);
    S5AssistComposeOutput out;
    assert(update(&f, joint(PI / 2.0f, 0.0f), joint(0.0f, 0.0f), false, false, 0.0f, 0U, &out));
    assert(out.shoulder_rotor_nm == 0.0f && out.elbow_current_a == 0.0f);
}

static void amplify_increment(void)
{
    Fixture f;
    fixture_init(&f);
    f.amplify.increment_rotor_nm = 0.02f;
    S5AssistComposeOutput out;
    const JointState shoulder = joint(PI / 6.0f, 1.0f); /* raising above 20 deg/s */
    const JointState elbow = joint(0.0f, 0.0f);

    assert(update(&f, shoulder, elbow, true, false, 0.0f, 0U, &out));
    assert(fabsf(out.shoulder_rotor_nm - 0.1025f) < 1e-5f); /* ramp starts at 0 */
    assert(update(&f, shoulder, elbow, true, false, 0.0f, 100U, &out));
    assert(fabsf(out.shoulder_rotor_nm - 0.1125f) < 1e-4f); /* half ramp */
    assert(update(&f, shoulder, elbow, true, false, 0.0f, 200U, &out));
    assert(fabsf(out.shoulder_rotor_nm - 0.1225f) < 1e-4f); /* full increment */

    /* Stopped: the increment holds for the suspension window, then releases. */
    const JointState still = joint(PI / 6.0f, 0.0f);
    assert(update(&f, still, elbow, true, false, 0.0f, 300U, &out));
    assert(out.shoulder_rotor_nm > 0.1025f);
    assert(update(&f, still, elbow, true, false, 0.0f, 700U, &out));
    assert(fabsf(out.shoulder_rotor_nm - 0.1025f) < 1e-4f);
}

static void force_gain(void)
{
    Fixture f;
    fixture_init(&f);
    f.force = (ForceAssistConfig){0.0f, 0.0f, 0.0f, 0.5f, 0.25f, 50.0f,
                                  200U, 3000U, 150U, true};
    S5AssistComposeOutput out;
    const JointState shoulder = joint(PI / 6.0f, 0.0f);
    const JointState elbow = joint(0.0f, 0.0f);

    /* 1.0 N against F0 = 0 crosses the 0.5 N threshold; gain ramps to 1.5. */
    assert(update(&f, shoulder, elbow, true, true, 1.0f, 0U, &out));
    assert(fabsf(out.elbow_current_a - 1.0f) < 1e-4f);
    assert(update(&f, shoulder, elbow, true, true, 1.0f, 100U, &out));
    assert(fabsf(out.elbow_current_a - 1.25f) < 1e-4f);
    assert(update(&f, shoulder, elbow, true, true, 1.0f, 200U, &out));
    assert(fabsf(out.elbow_current_a - 1.5f) < 1e-4f);
    assert(fabsf(out.shoulder_rotor_nm - 0.1025f * 1.5f) < 1e-4f);

    /* A stale or missing sample drops the increment at once, not the baseline. */
    assert(update(&f, shoulder, elbow, true, false, 0.0f, 300U, &out));
    assert(fabsf(out.elbow_current_a - 1.0f) < 1e-5f);
    assert(fabsf(out.shoulder_rotor_nm - 0.1025f) < 1e-5f);
    assert(!f.force_state.active);
}

static void single_axis(void)
{
    Fixture f;
    fixture_init(&f);
    f.config.dual = false;
    f.elbow_support = (S5ElbowSupportConfig){1.0f, 2.0f, true};
    S5AssistComposeOutput out;
    assert(update(&f, joint(0.5f, 0.0f), joint(0.5f, 0.0f), true, false, 0.0f, 0U, &out));
    assert(out.shoulder_rotor_nm == 0.0f);
    assert(fabsf(out.elbow_current_a - 2.0f) < 1e-5f);

    f.elbow_support.confirmed = false;
    assert(!update(&f, joint(0.5f, 0.0f), joint(0.5f, 0.0f), true, false, 0.0f, 0U, &out));
    assert(out.shoulder_rotor_nm == 0.0f && out.elbow_current_a == 0.0f);
}

static void invalid_inputs(void)
{
    Fixture f;
    fixture_init(&f);
    S5AssistComposeOutput out;
    const JointState shoulder = joint(0.0f, 0.0f);
    const JointState elbow = joint(0.0f, 0.0f);
    assert(!S5AssistCompose_Update(NULL, &shoulder, &elbow, true, false, 0.0f,
                                   &f.amplify_state, &f.elbow_amplify_state,
                                   &f.force_state, 0U, &out));
    JointState bad = joint(0.0f, 0.0f);
    bad.valid = false;
    assert(!update(&f, bad, elbow, true, false, 0.0f, 0U, &out));
    assert(out.shoulder_rotor_nm == 0.0f && out.elbow_current_a == 0.0f);
    JointState nan_joint = joint(NAN, 0.0f);
    assert(!update(&f, nan_joint, elbow, true, false, 0.0f, 0U, &out));
    f.shoulder_gravity.confirmed = false;
    assert(!update(&f, shoulder, elbow, true, false, 0.0f, 0U, &out));
}

void TestAssistCompose_Run(const char *scenario)
{
    if (!strcmp(scenario, "compose_gravity")) { gravity_baseline(); }
    else if (!strcmp(scenario, "compose_caps")) { output_caps(); }
    else if (!strcmp(scenario, "compose_assist_off")) { assist_gate(); }
    else if (!strcmp(scenario, "compose_amplify")) { amplify_increment(); }
    else if (!strcmp(scenario, "compose_elbow_amplify")) { elbow_amplify_follows_movement(); }
    else if (!strcmp(scenario, "compose_force")) { force_gain(); }
    else if (!strcmp(scenario, "compose_single")) { single_axis(); }
    else { assert(!strcmp(scenario, "compose_invalid")); invalid_inputs(); }
}
