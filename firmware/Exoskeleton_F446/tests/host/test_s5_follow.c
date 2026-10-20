/* Synthetic S5 command and CAN lifecycle; values never populate a site image. */
#include "test_s5_follow.h"
#include "test_console_probe.h"
#include "s5_follow.h"
#include "s4_follow.h"
#include "s5_site.h"
#include "s3_feedback.h"
#include "app_main.h"
#include "can_monitor.h"
#include "can_mock.h"
#include "can_tx_mock.h"
#include "s2_config.h"
#include <assert.h>
#include <math.h>
#include <string.h>

static S5SiteProfile profile;
static uint32_t tick;
static S5SiteProfile dual_profile(void);

static S5SiteProfile elbow_profile(void)
{
    S5SiteProfile p = {0};
    p.s3.safety.enabled_axes = SAFETY_ELBOW_AXES;
    p.s3.safety.stop_policy = SAFETY_STOP_SUPERVISED_ELBOW_FOLLOW;
    p.s3.safety.protection_confirmed = true;
    p.s3.safety.stop_method_confirmed = true;
    p.s3.safety.timing_confirmed = true;
    p.s3.safety.feedback_timeout_ms = 100U;
    p.s3.safety.service_limit_ms = 20U;
    p.s3.safety.max_request_ms = 100U;
    SafetyJointConfig *j = &p.s3.safety.joint[1];
    j->mapping = (JointMapping){1.0f, 0.0f, 1, true};
    j->lower_rad = -0.1f; j->upper_rad = 0.1f;
    j->max_speed_rad_s = 5.0f;
    j->max_motor_command = 0.2f;
    j->motor_slew_per_s = 10.0f;
    j->limits_confirmed = j->feedback_confirmed = j->protocol_confirmed = true;
    p.s3.elbow_id = S2_C620_ID; p.s3.shoulder_node = S2_GDS_NODE;
    p.s3.trusted_feedback_axes = SAFETY_ELBOW_AXES;
    p.s3.coordinates[1] = S3_ASSEMBLED_JOINT;
    p.s3.operational_confirmed[1] = true;
    p.s3.prepare_timeout_ms = 1000U;
    p.elbow_support = (S5ElbowSupportConfig){0.1f, 0.0f, true};
    p.release_slew_per_s.elbow_current_a = 5.0f;
    p.send_interval_ms = 10U;
    p.session_ms = 500U;
    p.ramp_max_ms = 100U;
    p.output_enabled = true;
    return p;
}

static void site_and_model(void)
{
    S5Site_TestUseProfile(NULL);
#if defined(S5_R4_ELBOW_FOLLOW) || defined(S5_R5_DUAL_FOLLOW)
    assert(S5Site_ReadReady(S5Site_GetProfile()));
#else
    assert(!S5Site_ReadReady(S5Site_GetProfile()));
#endif
    S5SiteProfile p = elbow_profile();
    assert(S5Site_ReadReady(&p));
    assert(!S5Site_ProfileReady(&p));
    p.s3.rotor_reference_confirmed[1] = true;
    assert(S5Site_ProfileReady(&p));
    JointState q = {0.05f, 0.0f, 1U, true};
    float a = 0.0f;
    assert(S5ElbowSupport_Compute(&p.elbow_support, &q, &a) && a == 0.1f);
    p.elbow_support.slope_a_per_rad = 2.0f;
    assert(S5ElbowSupport_Compute(&p.elbow_support, &q, &a) && a > 0.19f);
    assert(!S5Site_ProfileReady(&p)); /* Model exceeds cap at sector edge. */
    p.elbow_support.confirmed = false;
    assert(!S5ElbowSupport_Compute(&p.elbow_support, &q, &a) && a == 0.0f);

    /* The dual candidate clips a wider model to the unchanged 40 mNm cap. */
    S5SiteProfile one_sided = dual_profile();
    SafetyJointConfig *sh = &one_sided.s3.safety.joint[0];
    sh->mapping.rotor_per_joint = 8.0f;
    sh->lower_rad = 0.34906585f; sh->upper_rad = 0.52359878f;
    sh->max_motor_command = 0.040f;
    one_sided.shoulder_gravity.amplitude_nm = 1.64f;
    one_sided.shoulder_gain = 0.36f;
    assert(S5Site_ReadReady(&one_sided));
    one_sided.s3.rotor_reference_confirmed[0] = true;
    one_sided.s3.rotor_reference_confirmed[1] = true;
    assert(S5Site_ProfileReady(&one_sided));
    one_sided.shoulder_gain = 0.40f;
    assert(S5Site_ProfileReady(&one_sided));
}

static void elbow_gravity_model(void)
{
    GravityCompensationConfig config = {0.04f, 0.0f, 1, true};
    const struct { float shoulder_deg, elbow_deg, expected_nm; } cases[] = {
        {0.0f, 0.0f, 0.0f}, {20.0f, 10.0f, 0.02f},
        {30.0f, 15.0f, 0.02828427f}, {30.0f, 60.0f, 0.04f},
        {20.0f, -20.0f, 0.0f}, {-10.0f, 0.0f, 0.0f},
        {100.0f, 100.0f, 0.0f}, {60.0f, 60.0f, 0.03464102f}
    };
    JointState shoulder = {0.0f, 0.0f, 1U, true};
    JointState elbow = {0.0f, 0.0f, 1U, true};
    float nm = 0.0f;
    for (unsigned i = 0U; i < sizeof(cases) / sizeof(cases[0]); i++) {
        shoulder.position_rad = cases[i].shoulder_deg * 0.0174532925f;
        elbow.position_rad = cases[i].elbow_deg * 0.0174532925f;
        assert(S5ElbowGravity_Compute(&config, &shoulder, &elbow, &nm)
            && fabsf(nm - cases[i].expected_nm) < 0.000001f);
    }
    config.sign_g = -1;
    shoulder.position_rad = 0.0f;
    elbow.position_rad = 0.52359878f;
    assert(S5ElbowGravity_Compute(&config, &shoulder, &elbow, &nm)
        && fabsf(nm + 0.02f) < 0.000001f);
    elbow.position_rad = -0.52359878f;
    assert(S5ElbowGravity_Compute(&config, &shoulder, &elbow, &nm) && nm == 0.0f);
    shoulder.valid = false;
    nm = 1.0f;
    assert(!S5ElbowGravity_Compute(&config, &shoulder, &elbow, &nm) && nm == 0.0f);
    shoulder.valid = true;
    elbow.valid = false;
    assert(!S5ElbowGravity_Compute(&config, &shoulder, &elbow, &nm) && nm == 0.0f);
    elbow.valid = true;
    shoulder.position_rad = NAN;
    assert(!S5ElbowGravity_Compute(&config, &shoulder, &elbow, &nm) && nm == 0.0f);
    shoulder.position_rad = 0.0f;
    elbow.velocity_rad_s = INFINITY;
    assert(!S5ElbowGravity_Compute(&config, &shoulder, &elbow, &nm) && nm == 0.0f);
    elbow.velocity_rad_s = 0.0f;
    config.amplitude_nm = 0.0f;
    assert(!S5ElbowGravity_Compute(&config, &shoulder, &elbow, &nm) && nm == 0.0f);
    assert(!S5ElbowGravity_Compute(NULL, &shoulder, &elbow, &nm));
    assert(!S5ElbowGravity_Compute(&config, NULL, &elbow, &nm));
    assert(!S5ElbowGravity_Compute(&config, &shoulder, NULL, &nm));
    assert(!S5ElbowGravity_Compute(&config, &shoulder, &elbow, NULL));

    S5SiteProfile p = dual_profile();
    assert(S5Site_ReadReady(&p));
    p.elbow_motor_kt_nm_per_a = 0.0f;
    assert(!S5Site_ReadReady(&p));
    p = dual_profile();
    p.elbow_gravity.amplitude_nm = 2.0f * p.s3.safety.joint[1].max_motor_command
        * p.elbow_motor_kt_nm_per_a;
    assert(!S5Site_ReadReady(&p));
    p = dual_profile();
    p.elbow_gravity.gravity_zero_rad = 0.1f;
    assert(!S5Site_ReadReady(&p));
}

/* ExoKit AmplifyReaction::runLoop() timing: 0.35 rad/s start, 400 ms
 * direction-change suspension and hold, plus the local 200 ms ramp. */
static void shoulder_amplify_model(void)
{
    const ShoulderAmplifyConfig c = {0.04f, 0.35f, 400U, 200U};
    assert(ShoulderAmplify_ConfigValid(&c));
    ShoulderAmplify a;
    ShoulderAmplify_Reset(&a, 0U);
    uint32_t t = 0U;
    float out = 1.0f;
    for (; t < 300U; t += 10U) {
        out = ShoulderAmplify_Step(&a, &c, 0.2f, t);
        assert(out == 0.0f); /* Below the starting speed. */
    }
    out = ShoulderAmplify_Step(&a, &c, 0.5f, t); /* First raise: no suspension. */
    assert(a.engaged && out > 0.0f && out < 0.04f);
    for (t += 10U; t <= 520U; t += 10U) { out = ShoulderAmplify_Step(&a, &c, 0.5f, t); }
    assert(out == 0.04f);
    out = ShoulderAmplify_Step(&a, &c, -0.5f, t); /* Lowering switches off. */
    assert(!a.engaged && out < 0.04f);
    for (t += 10U; t <= 760U; t += 10U) {
        out = ShoulderAmplify_Step(&a, &c, -0.5f, t);
        assert(!a.engaged);
    }
    assert(out == 0.0f);
    const uint32_t change = t;
    uint32_t engaged_ms = 0U;
    for (; t <= change + 600U; t += 10U) {
        out = ShoulderAmplify_Step(&a, &c, 0.5f, t);
        if (t - change <= 400U) { assert(!a.engaged && out == 0.0f); }
        else if (engaged_ms == 0U) { assert(a.engaged); engaged_ms = t; }
    }
    assert(engaged_ms == change + 410U && out == 0.04f);
    t = engaged_ms + 210U; /* Quiet before the 400 ms hold ends. */
    out = ShoulderAmplify_Step(&a, &c, 0.0f, t);
    assert(a.engaged && out == 0.04f);
    for (t += 10U; t <= engaged_ms + 700U; t += 10U) {
        out = ShoulderAmplify_Step(&a, &c, 0.0f, t);
    }
    assert(!a.engaged && out == 0.0f);
    assert(ShoulderAmplify_Step(&a, &c, NAN, t + 10U) == 0.0f);
    assert(ShoulderAmplify_Step(NULL, &c, 0.5f, t) == 0.0f);

    ShoulderAmplifyConfig bad = c;
    bad.increment_rotor_nm = 0.0f; /* Off needs no other field. */
    bad.starting_velocity_rad_s = 0.0f;
    assert(ShoulderAmplify_ConfigValid(&bad));
    ShoulderAmplify_Reset(&a, 0U);
    assert(ShoulderAmplify_Step(&a, &bad, 1.0f, 10U) == 0.0f);
    bad = c; bad.increment_rotor_nm = -0.01f; assert(!ShoulderAmplify_ConfigValid(&bad));
    bad = c; bad.increment_rotor_nm = NAN; assert(!ShoulderAmplify_ConfigValid(&bad));
    bad = c; bad.starting_velocity_rad_s = 0.0f; assert(!ShoulderAmplify_ConfigValid(&bad));
    bad = c; bad.suspension_ms = 0U; assert(!ShoulderAmplify_ConfigValid(&bad));
    bad = c; bad.ramp_ms = 0U; assert(!ShoulderAmplify_ConfigValid(&bad));
    assert(!ShoulderAmplify_ConfigValid(NULL));

    S5SiteProfile p = dual_profile();
    p.shoulder_amplify = c;
    assert(S5Site_ReadReady(&p));
    p.shoulder_amplify.increment_rotor_nm = 2.0f * p.s3.safety.joint[0].max_motor_command;
    assert(!S5Site_ReadReady(&p)); /* Increment above the shoulder cap. */
    p.shoulder_amplify = c;
    p.shoulder_amplify.ramp_ms = 0U;
    assert(!S5Site_ReadReady(&p));
}

#ifdef S5_R4_ELBOW_FOLLOW
static void r4_isolated_site(void)
{
    S5Site_TestUseProfile(NULL);
    const S5SiteProfile *p = S5Site_GetProfile();
    assert(S5Site_ReadReady(p) && !S5Site_ProfileReady(p));
    assert(p->s3.safety.enabled_axes == SAFETY_ELBOW_AXES
        && p->s3.safety.stop_policy == SAFETY_STOP_SUPERVISED_ELBOW_FOLLOW);
    const SafetyJointConfig *e = &p->s3.safety.joint[1];
    assert(e->lower_rad < 0.0f && e->upper_rad > 0.0f
        && fabsf(e->lower_rad + 0.08726646f) < 0.000001f
        && fabsf(e->upper_rad - 0.08726646f) < 0.000001f);
    assert(fabsf(e->max_motor_command - 0.001f * S5_R4_ELBOW_SUPPORT_MA)
            < 0.000001f && e->max_speed_rad_s == 0.34906585f);
    assert(p->session_ms == 5000U && p->ramp_max_ms == 250U
        && p->send_interval_ms == 10U
        && p->release_slew_per_s.elbow_current_a == 20.0f);
    JointState q = {0.0f, 0.0f, 1U, true};
    float a = 0.0f;
    const float expected = 0.001f * S5_R4_ELBOW_SUPPORT_MA
        * S5_R4_ELBOW_SUPPORT_SIGN;
    assert(S5ElbowSupport_Compute(&p->elbow_support, &q, &a)
        && fabsf(a - expected) < 0.000001f);
    q.position_rad = e->upper_rad;
    assert(S5ElbowSupport_Compute(&p->elbow_support, &q, &a)
        && fabsf(a - expected) < 0.000001f);
    TestClock_Set(100U);
    App_Init();
    assert(S5Follow_IsSelected() && !S4Follow_IsSelected());
}
#endif

#ifdef S5_R5_DUAL_FOLLOW
static void r5_dual_two_pair_zero(void);
static void r5_dual_no_post_zero_dwell(void);
static void r5_dual_site(void)
{
    S5Site_TestUseProfile(NULL);
    const S5SiteProfile *p = S5Site_GetProfile();
    assert(S5Site_ReadReady(p) && !S5Site_ProfileReady(p));
    assert(p->s3.safety.enabled_axes == SAFETY_ALL_AXES
        && p->s3.safety.stop_policy == SAFETY_STOP_SUPERVISED_DUAL_FOLLOW
        && p->s3.trusted_feedback_axes == SAFETY_ALL_AXES);
    const SafetyJointConfig *sh = &p->s3.safety.joint[0];
    const SafetyJointConfig *el = &p->s3.safety.joint[1];
    assert(sh->mapping.rotor_per_joint == 8.0f
        && sh->lower_rad == -0.17453293f && sh->upper_rad == 1.83259571f
        && sh->max_speed_rad_s == 2.09439510f
        && sh->max_motor_command == 0.205f && sh->motor_slew_per_s == 0.100f);
    assert(el->mapping.rotor_per_joint == 1.0f
        && el->mapping.direction == -1
        && el->lower_rad == -0.34906585f && el->upper_rad == 1.83259571f
        && el->max_speed_rad_s == 3.14159265f
        && el->max_motor_command == 20.0f && el->motor_slew_per_s == 20.0f);
    /* Soft pause at 60/90 deg/s, below the 120/180 deg/s latched limits. */
    assert(p->soft_speed_rad_s[0] == 1.04719755f
        && p->soft_speed_rad_s[1] == 1.57079633f
        && p->resume_quiet_ms == 100U && p->resume_ramp_ms == 300U);
    S5SiteProfile soft_bad = *p;
    soft_bad.soft_speed_rad_s[1] = el->max_speed_rad_s;
    assert(!S5Site_ReadReady(&soft_bad));
    soft_bad = *p;
    soft_bad.resume_ramp_ms = 0U;
    assert(!S5Site_ReadReady(&soft_bad));
    assert(p->s3.safety.feedback_timeout_ms == 150U
        && p->s3.safety.service_limit_ms == 100U
        && p->s3.safety.max_request_ms == 250U
        && p->s3.prepare_timeout_ms == 20000U
        && p->s3.shoulder_property_reply_ms == 20U
        && p->s3.shoulder_property_period_ms == 20U
        && p->s3.shoulder_property_window_ms == 55000U
        && p->s3.shoulder_stop_window_ms == 1500U
        && p->s3.shoulder_standstill.span_rev == 0.011f
        && p->s3.shoulder_standstill.max_velocity_rev_s == 0.066f
        && p->s3.shoulder_standstill.dwell_ms == 100U
        && p->s3.shoulder_standstill.freshness_ms == 150U);
    assert(p->shoulder_gravity.amplitude_nm == 1.64f
        && p->shoulder_gravity.sign_g == 1 && p->shoulder_gain == 1.0f
        && p->shoulder_zero_dwell_ms <= 1U
        && p->send_interval_ms == 50U && p->session_ms == 30000U
        && p->ramp_max_ms == 2000U
        && p->release_slew_per_s.shoulder_rotor_nm == 0.120f
        && p->release_slew_per_s.elbow_current_a == 25.0f);
    JointState shoulder = {0.0f, 0.0f, 1U, true};
    JointState elbow = {1.57079633f, 0.0f, 1U, true};
    float torque_nm = 0.0f;
    const float expected = 0.001f * S5_R5_ELBOW_SUPPORT_MA
        * S5_R5_ELBOW_SUPPORT_SIGN;
    assert(p->elbow_motor_kt_nm_per_a == 0.02f
        && p->elbow_gravity.gravity_zero_rad == 0.0f);
    assert(S5ElbowGravity_Compute(&p->elbow_gravity, &shoulder, &elbow, &torque_nm));
    float current_a = torque_nm / p->elbow_motor_kt_nm_per_a;
    assert(fabsf(current_a) <= el->max_motor_command
        && fabsf(current_a - expected) < 0.000001f
        && p->elbow_gravity.sign_g == S5_R5_ELBOW_SUPPORT_SIGN);
    S5SiteProfile active = *p;
    active.s3.rotor_reference_confirmed[0] = true;
    active.s3.rotor_reference_confirmed[1] = true;
    assert(S5Site_ProfileReady(&active));
    const ShoulderAmplifyConfig *amp = &p->shoulder_amplify;
    assert(amp->increment_rotor_nm == 0.001f * S5_R5_SHOULDER_AMPLIFY_MNM
        && amp->starting_velocity_rad_s == 0.34906585f
        && amp->suspension_ms == 400U && amp->ramp_ms == 200U);
    S5SiteProfile amp_site = *p;
    amp_site.shoulder_amplify.increment_rotor_nm = 0.040f;
    assert(S5Site_ReadReady(&amp_site));
    amp_site.shoulder_amplify.starting_velocity_rad_s = p->soft_speed_rad_s[0];
    assert(!S5Site_ReadReady(&amp_site)); /* Must engage below the soft pause. */
    TestClock_Set(100U);
    App_Init();
    assert(S5Follow_IsSelected() && !S4Follow_IsSelected());
}
#endif

static void sample(uint32_t now_ms, uint16_t ticks)
{
    tick = now_ms; TestClock_Set(tick);
    TestCanTx_Complete(BOARD_CAN_ELBOW);
    uint8_t data[8] = {(uint8_t)(ticks >> 8), (uint8_t)ticks,
                       0U, 0U, 0U, 0U, 30U, 0U};
    TestCan_InjectData(BOARD_CAN_ELBOW, 0x201U, 8U, 0U, 0U, data);
    TestCan_Interrupt(BOARD_CAN_ELBOW);
    CanMonitor_Poll();
    S5Follow_Poll(tick, 10U);
}

static void elbow_chain(void)
{
    profile = elbow_profile();
    S5Site_TestUseProfile(&profile);
    tick = 100U; TestClock_Set(tick);
    App_Init(); TestCanTx_Reset();
    assert(S5Follow_IsSelected() && !S4Follow_IsSelected());
    assert(strstr(TestConsole_QueryCached("followstatus\n"), "axes=2 phase=1"));
    assert(!strcmp(TestConsole_QueryRaw("arm\n"), "ERR s5_mode\r\n"));
    assert(strstr(TestConsole_QueryCached("followprepare\n"), "preparing"));
    S5FollowStatus s;
    for (uint32_t t = 110U; t <= 500U; t += 10U) {
        sample(t, (uint16_t)((8191U + ((t / 10U) & 1U)) % 8192U));
        S5Follow_GetStatus(&s);
        if (s.phase == S5_FOLLOW_READY) { break; }
        assert(s.phase != S5_FOLLOW_FAULT);
    }
    assert(s.phase == S5_FOLLOW_READY && s.elbow_zero_valid
        && TestCanTx_Sent(BOARD_CAN_ELBOW) == 1U);
    for (unsigned i = 0U; i < 8U; i++) {
        assert(TestCanTx_Last(BOARD_CAN_ELBOW)->data[i] == 0U);
    }
    assert(strstr(TestConsole_QueryCached("followstart\n"), "axes=2"));
    assert(!strcmp(TestConsole_QueryRaw("elbowpulse 1 100 50\n"), "ERR s5_mode\r\n"));
    for (unsigned i = 0U; i < 5U; i++) {
        sample(tick + 10U, (uint16_t)((8191U + (((tick + 10U) / 10U) & 1U)) % 8192U));
    }
    S5Follow_GetStatus(&s);
    assert(s.phase == S5_FOLLOW_RUNNING && s.output_allowed && s.model_valid);
    assert(s.sample.sequence[1] != 0U && s.sample.sequence[0] == 0U);
    assert(TestCanTx_Sent(BOARD_CAN_SHOULDER) == 0U);
    assert(TestCanTx_Last(BOARD_CAN_ELBOW)->id == 0x200U);
    assert(TestCanTx_Last(BOARD_CAN_ELBOW)->data[0] != 0U
        || TestCanTx_Last(BOARD_CAN_ELBOW)->data[1] != 0U);
    assert(strstr(TestConsole_QueryCached("followstatus\n"), "cmd_el_mA=100"));
    TestCanTx_Complete(BOARD_CAN_ELBOW);
    assert(strstr(TestConsole_QueryCached("followstop\n"), "ramping_down"));
    for (unsigned i = 0U; i < 20U; i++) {
        sample(tick + 10U, 8191U);
        S5Follow_GetStatus(&s);
        if (s.phase == S5_FOLLOW_STOPPED || s.phase == S5_FOLLOW_FAULT) { break; }
    }
    assert(s.phase == S5_FOLLOW_STOPPED && s.stop_accepted);
    assert(!s.elbow_zero_valid && !s.output_allowed);
    assert(S3Feedback_ElbowSessionPhase() == ELBOW_SESSION_UNSET);
    for (unsigned i = 0U; i < 8U; i++) {
        assert(TestCanTx_Last(BOARD_CAN_ELBOW)->data[i] == 0U);
    }
}

static uint8_t shoulder_axis_state, shoulder_mode, shoulder_input, life;
static float shoulder_torque, shoulder_rev, shoulder_rev_s;
static unsigned shoulder_nonzero, shoulder_idle;
static uint16_t elbow_offset_ticks;
static bool elbow_reverse;
static int16_t elbow_rpm;

static uint32_t bits(float value)
{ uint32_t raw; memcpy(&raw, &value, sizeof(raw)); return raw; }

static void shoulder_heartbeat(void)
{
    uint8_t data[8] = {0};
    data[4] = shoulder_axis_state;
    data[7] = ++life;
    TestCan_InjectData(BOARD_CAN_SHOULDER, 0x021U, 8U, 0U, 0U, data);
}

static S5SiteProfile dual_profile(void)
{
    S5SiteProfile p = elbow_profile();
    p.s3.safety.enabled_axes = SAFETY_ALL_AXES;
    p.s3.safety.stop_policy = SAFETY_STOP_SUPERVISED_DUAL_FOLLOW;
    SafetyJointConfig *j = &p.s3.safety.joint[0];
    j->mapping = (JointMapping){2.0f, 0.0f, 1, true};
    j->lower_rad = -0.5f; j->upper_rad = 1.0f;
    j->max_speed_rad_s = 2.0f;
    j->max_motor_command = 0.05f;
    j->motor_slew_per_s = 1.0f;
    j->limits_confirmed = j->feedback_confirmed = j->protocol_confirmed = true;
    p.s3.trusted_feedback_axes = SAFETY_ALL_AXES;
    p.s3.coordinates[0] = S3_ASSEMBLED_JOINT;
    p.s3.shoulder_source = S3_SHOULDER_ENCODER_PROPERTIES;
    p.s3.shoulder_property_reply_ms = 4U;
    p.s3.shoulder_property_period_ms = 20U;
    p.s3.shoulder_property_window_ms = 1700U;
    p.s3.shoulder_standstill = (ShoulderStandstillConfig){0.001f, 0.02f, 10U, 50U};
    p.s3.shoulder_stop_window_ms = 200U;
    p.shoulder_gravity = (GravityCompensationConfig){0.02f, 0.0f, 1, true};
    p.elbow_gravity = (GravityCompensationConfig){0.002f, 0.0f, 1, true};
    p.elbow_motor_kt_nm_per_a = 0.02f;
    p.shoulder_gain = 0.5f;
    p.shoulder_zero_dwell_ms = 10U;
    p.release_slew_per_s.shoulder_rotor_nm = 1.0f;
    return p;
}

static void receive_shoulder_output(const BoardCanFrame *frame)
{
    if (frame->id == 0x024U) {
        unsigned ep = frame->data[1] | ((unsigned)frame->data[2] << 8);
        uint32_t raw;
        switch (ep) {
        case 372U: raw = bits(shoulder_rev); break;
        case 378U: raw = bits(shoulder_rev_s); break;
        case 195U: case 366U: raw = 1U; break;
        case 272U: raw = bits(shoulder_torque); break;
        case 287U: raw = shoulder_mode; break;
        case 288U: raw = shoulder_input; break;
        default: assert(false); raw = 0U;
        }
        uint8_t data[8] = {0};
        data[1] = frame->data[1]; data[2] = frame->data[2];
        for (unsigned i = 0U; i < 4U; i++) {
            data[4U + i] = (uint8_t)(raw >> (8U * i));
        }
        TestCan_InjectData(BOARD_CAN_SHOULDER, 0x025U, 8U, 0U, 0U, data);
    } else if (frame->id == 0x02EU) {
        memcpy(&shoulder_torque, frame->data, sizeof(shoulder_torque));
        if (fabsf(shoulder_torque) > 0.0001f) { shoulder_nonzero++; }
    } else if (frame->id == 0x02BU) {
        shoulder_mode = frame->data[0];
        shoulder_input = frame->data[4];
    } else if (frame->id == 0x027U) {
        shoulder_axis_state = frame->data[0];
        if (shoulder_axis_state == 1U) { shoulder_idle++; }
        else { assert(shoulder_axis_state == 8U); }
    } else { assert(false); }
}

static void dual_step_with_elbow(bool elbow_feedback)
{
    tick++; TestClock_Set(tick);
    BoardCanFrame pending[3]; unsigned count = 0U;
    for (unsigned i = 0U; i < 3U; i++) {
        const BoardCanFrame *frame = TestCanTx_Pending(BOARD_CAN_SHOULDER, i);
        if (frame != NULL) { pending[count++] = *frame; }
    }
    TestCanTx_Complete(BOARD_CAN_SHOULDER);
    TestCanTx_Complete(BOARD_CAN_ELBOW);
    for (unsigned i = 0U; i < count; i++) { receive_shoulder_output(&pending[i]); }
    shoulder_heartbeat();
    TestCan_Interrupt(BOARD_CAN_SHOULDER);
    if (elbow_feedback) {
        int32_t ticks = 8191 + (int32_t)(tick & 1U)
            + (elbow_reverse ? -(int32_t)elbow_offset_ticks : (int32_t)elbow_offset_ticks);
        uint16_t angle = (uint16_t)((ticks + 8192) % 8192);
        uint8_t elbow[8] = {(uint8_t)(angle >> 8), (uint8_t)angle,
                            (uint8_t)((uint16_t)elbow_rpm >> 8), (uint8_t)elbow_rpm,
                            0U, 0U, 30U, 0U};
        TestCan_InjectData(BOARD_CAN_ELBOW, 0x201U, 8U, 0U, 0U, elbow);
        TestCan_Interrupt(BOARD_CAN_ELBOW);
    }
    CanMonitor_Poll();
    S5Follow_Poll(tick, 1U);
}

static void dual_step(void) { dual_step_with_elbow(true); }

#ifdef S5_R5_DUAL_FOLLOW
static void r5_zero_fixture(uint32_t property_period_ms)
{
    S5Site_TestUseProfile(NULL);
    profile = *S5Site_GetProfile();
    profile.s3.shoulder_property_period_ms = property_period_ms;
    assert(S5Site_ReadReady(&profile));
    S5Site_TestUseProfile(&profile);
    tick = 100U; TestClock_Set(tick);
    shoulder_axis_state = 1U; shoulder_mode = shoulder_input = 3U;
    shoulder_torque = 0.0f; shoulder_rev = 0.25f;
    elbow_offset_ticks = 0U; elbow_reverse = true; elbow_rpm = 0;
    life = 0U; shoulder_nonzero = shoulder_idle = 0U;
    App_Init(); TestCanTx_Reset();
    shoulder_heartbeat(); TestCan_Interrupt(BOARD_CAN_SHOULDER); CanMonitor_Poll();
    assert(strstr(TestConsole_QueryCached("followprepare\n"), "preparing"));
}

static uint32_t r5_first_property_pair(void)
{
    for (unsigned i = 0U; i < 500U; i++) {
        dual_step();
        S5FollowStatus s = {0};
        S5Follow_GetStatus(&s);
        assert(s.phase == S5_FOLLOW_PREPARING && !s.shoulder_zero_valid
            && !s.output_allowed);
        ShoulderPropertySnapshot pair = {0};
        if (s.elbow_zero_valid && CanMonitor_GetShoulderProperties(tick,
                profile.s3.safety.feedback_timeout_ms, &pair)) {
            assert(pair.sequence != 0U);
            return pair.sequence;
        }
    }
    assert(!"first complete shoulder pair missing");
    return 0U;
}

static void r5_dual_gravity_zero(void)
{
    r5_zero_fixture(20U);
    S5FollowStatus s = {0};
    for (unsigned i = 0U; i < 5000U; i++) {
        dual_step(); S5Follow_GetStatus(&s);
        assert(s.phase != S5_FOLLOW_FAULT);
        if (s.phase == S5_FOLLOW_READY) { break; }
    }
    assert(s.phase == S5_FOLLOW_READY);
    assert(strstr(TestConsole_QueryCached("followstart\n"), "axes=3"));
    const float peak_a = 0.001f * S5_R5_ELBOW_SUPPORT_MA;
    for (unsigned i = 0U; i < 200U; i++) {
        dual_step(); S5Follow_GetStatus(&s);
        assert(s.phase == S5_FOLLOW_RUNNING && s.output_allowed);
        assert(fabsf(s.target.elbow_current_a) < peak_a * 0.001f);
        const BoardCanFrame *frame = TestCanTx_Last(BOARD_CAN_ELBOW);
        assert(frame != NULL && frame->id == 0x200U);
        int16_t raw = (int16_t)((uint16_t)frame->data[0] << 8U | frame->data[1]);
        assert(fabsf((float)raw * (20.0f / 16384.0f)) < peak_a * 0.001f);
    }
}

static void r5_dual_two_pair_zero(void)
{
    r5_zero_fixture(1U);
    uint32_t first_sequence = r5_first_property_pair();
    uint32_t first_ms = tick;
    shoulder_rev = 0.30f; /* Changed by 0.05 rotor rev: well beyond the old fine span. */
    S5FollowStatus s = {0};
    for (unsigned i = 0U; i < 19U; i++) {
        dual_step(); S5Follow_GetStatus(&s);
        assert(s.phase == S5_FOLLOW_PREPARING && !s.output_allowed);
        if (s.shoulder_zero_valid) { break; }
    }
    ShoulderPropertySnapshot second = {0};
    assert(s.shoulder_zero_valid && (uint32_t)(tick - first_ms) < 20U
        && CanMonitor_GetShoulderProperties(tick,
            profile.s3.safety.feedback_timeout_ms, &second)
        && second.sequence != first_sequence);
}

static void r5_dual_no_post_zero_dwell(void)
{
    /* The second shoulder pair arrives after the elbow's 250 ms zero window.
     * The gateway must prepare on those two pairs, without a third shoulder
     * pair or another 200 ms stationary window. */
    r5_zero_fixture(140U);
    (void)r5_first_property_pair();
    shoulder_rev = 0.30f;
    uint32_t both_zero_ms = 0U;
    S5FollowStatus s = {0};
    for (unsigned i = 0U; i < 500U; i++) {
        dual_step(); S5Follow_GetStatus(&s);
        assert(s.phase != S5_FOLLOW_FAULT && !s.output_allowed);
        if (both_zero_ms == 0U && s.shoulder_zero_valid && s.elbow_zero_valid) {
            both_zero_ms = tick;
        }
        if (s.output_state != OUTPUT_SAFE_OFF) {
            assert(both_zero_ms != 0U && (uint32_t)(tick - both_zero_ms) < 10U);
            return;
        }
    }
    assert(!"R5 stayed SAFE_OFF after both RAM zeroes");
}
#endif

static void dual_chain(void)
{
    profile = dual_profile();
    assert(S5Site_ReadReady(&profile));
    S5Site_TestUseProfile(&profile);
    tick = 100U; TestClock_Set(tick);
    shoulder_axis_state = 1U; shoulder_mode = shoulder_input = 3U;
    shoulder_torque = 0.0f; shoulder_rev = 0.25f;
    elbow_offset_ticks = 0U;
    elbow_reverse = false;
    elbow_rpm = 0;
    life = 0U; shoulder_nonzero = shoulder_idle = 0U;
    App_Init(); TestCanTx_Reset();
    shoulder_heartbeat(); TestCan_Interrupt(BOARD_CAN_SHOULDER); CanMonitor_Poll();
    assert(S5Follow_IsSelected() && !S4Follow_IsSelected());
    assert(strstr(TestConsole_QueryCached("followprepare\n"), "preparing"));
    S5FollowStatus s;
    for (unsigned i = 0U; i < 900U; i++) {
        dual_step(); S5Follow_GetStatus(&s);
        if (s.phase == S5_FOLLOW_READY) { break; }
        assert(s.phase != S5_FOLLOW_FAULT);
    }
    assert(s.phase == S5_FOLLOW_READY && s.elbow_zero_valid && s.shoulder_zero_valid);
    assert(strstr(TestConsole_QueryCached("followstart\n"), "axes=3"));
    shoulder_rev = 0.30f; /* Synthetic low-pose displacement, inside the envelope. */
    for (unsigned i = 0U; i < 100U; i++) {
        dual_step(); S5Follow_GetStatus(&s);
        assert(s.phase != S5_FOLLOW_FAULT);
        if (shoulder_nonzero != 0U) { break; }
    }
    assert(s.phase == S5_FOLLOW_RUNNING && s.output_allowed
        && shoulder_nonzero != 0U && s.sample.sequence[0] != 0U
        && s.sample.sequence[1] != 0U);
    assert(TestCanTx_Sent(BOARD_CAN_ELBOW) > 1U);
    assert(strstr(TestConsole_QueryCached("followstatus\n"), "axes=3"));
    TestCanTx_Complete(BOARD_CAN_SHOULDER);
    TestCanTx_Complete(BOARD_CAN_ELBOW);
    assert(strstr(TestConsole_QueryCached("followstop\n"), "ramping_down"));
    for (unsigned i = 0U; i < 150U; i++) {
        dual_step(); S5Follow_GetStatus(&s);
        if (s.phase == S5_FOLLOW_STOPPED || s.phase == S5_FOLLOW_FAULT) { break; }
    }
    assert(s.phase == S5_FOLLOW_STOPPED && s.stop_accepted && shoulder_idle == 1U);
    assert(!s.elbow_zero_valid && !s.shoulder_zero_valid);
}

#ifdef S5_R5_DUAL_FOLLOW
static void r5_dual_cancel_early(bool pending_read)
{
    S5Site_TestUseProfile(NULL);
    tick = 100U; TestClock_Set(tick);
    shoulder_axis_state = 1U; shoulder_mode = shoulder_input = 3U;
    shoulder_torque = 0.0f; shoulder_rev = 0.25f;
    elbow_offset_ticks = 0U; elbow_reverse = true;
    elbow_rpm = 0;
    life = 0U; shoulder_nonzero = shoulder_idle = 0U;
    App_Init(); TestCanTx_Reset();
    shoulder_heartbeat(); TestCan_Interrupt(BOARD_CAN_SHOULDER); CanMonitor_Poll();
    assert(strstr(TestConsole_QueryCached("followprepare\n"), "preparing"));
    S5FollowStatus s;
    S5Follow_GetStatus(&s);
    assert(s.phase == S5_FOLLOW_PREPARING && s.output_state == OUTPUT_SAFE_OFF);
    if (pending_read) {
        dual_step();
        assert(TestCanTx_Pending(BOARD_CAN_SHOULDER, 0U) != NULL);
    }
    (void)TestConsole_QueryCached("followstop\n");
    S5Follow_GetStatus(&s);
    assert(s.phase != S5_FOLLOW_FAULT && !s.output_allowed);
    for (unsigned i = 0U; i < 100U; i++) {
        dual_step(); S5Follow_GetStatus(&s);
        if (s.phase == S5_FOLLOW_STOPPED || s.phase == S5_FOLLOW_FAULT) { break; }
    }
    assert(s.phase == S5_FOLLOW_STOPPED && !s.output_allowed
        && !s.shoulder_zero_valid && !s.elbow_zero_valid);
}

static void r5_inject_shoulder_can_history(void)
{
    TestCan_Inject(BOARD_CAN_SHOULDER, 0x7FFU, 9U, 0U, 0U, 0U);
    TestCan_Interrupt(BOARD_CAN_SHOULDER); /* Invalid DLC is only historical. */
    for (unsigned i = 0U; i <= S2_CAN_QUEUE_CAPACITY; i++) {
        TestCan_Inject(BOARD_CAN_SHOULDER, 0x7FFU, 8U, 0U, 0U, 0U);
        TestCan_Interrupt(BOARD_CAN_SHOULDER);
    }
    for (unsigned i = 0U; i < S2_CAN_QUEUE_CAPACITY; i++) { CanMonitor_Poll(); }
    hcan1.ErrorCode = HAL_CAN_ERROR_RX_FOV0;
    HAL_CAN_ErrorCallback(&hcan1);
    BoardCanStats can;
    assert(BoardCan_GetStats(BOARD_CAN_SHOULDER, &can)
        && can.dropped != 0U && can.invalid != 0U
        && can.fifo_overruns != 0U && can.queued == 0U && !can.bus_off);
}

static const S5SiteProfile *r5_site_override; /* NULL = actual isolated R5 site. */

static void r5_dual_start_running(bool inject_br, bool inject_shoulder_history)
{
    S5Site_TestUseProfile(r5_site_override);
    const S5SiteProfile *p = S5Site_GetProfile();
    tick = 100U; TestClock_Set(tick);
    shoulder_axis_state = 1U; shoulder_mode = shoulder_input = 3U;
    shoulder_torque = 0.0f; shoulder_rev = 0.25f;
    elbow_offset_ticks = 0U;
    elbow_reverse = true;
    elbow_rpm = 0;
    life = 0U; shoulder_nonzero = shoulder_idle = 0U;
    App_Init(); TestCanTx_Reset();
    if (inject_shoulder_history) { r5_inject_shoulder_can_history(); }
    shoulder_heartbeat(); TestCan_Interrupt(BOARD_CAN_SHOULDER); CanMonitor_Poll();
    assert(S5Follow_IsSelected() && !S4Follow_IsSelected());
    assert(strstr(TestConsole_QueryCached("followprepare\n"), "preparing"));
    S5FollowStatus s = {0};
    bool br_injected = false;
    for (unsigned i = 0U; i < 5000U; i++) {
        dual_step(); S5Follow_GetStatus(&s);
        assert(s.phase != S5_FOLLOW_FAULT);
        if (!br_injected && inject_br && s.shoulder_zero_valid) {
            /* One historical CAN bit error cannot strand fresh dual feedback. */
            const uint32_t br = 0x40U;
            BoardCanStats can;
            assert(s.phase == S5_FOLLOW_PREPARING);
            hcan1.ErrorCode = br;
            HAL_CAN_ErrorCallback(&hcan1);
            assert(BoardCan_GetStats(BOARD_CAN_SHOULDER, &can)
                && can.errors == 1U && can.last_error == br && !can.bus_off);
            br_injected = true;
        }
        if (s.phase == S5_FOLLOW_READY) { break; }
    }
    assert((!inject_br || br_injected) && s.phase == S5_FOLLOW_READY
        && s.shoulder_zero_valid && s.elbow_zero_valid
        && fabsf(s.joint[0].position_rad) < 0.005f
        && fabsf(s.joint[1].position_rad) < 0.005f);
    /* READY at the RAM zero must be startable without a precise pre-start
     * displacement. At zero, both gravity candidates are near zero, allowing
     * the encoder's quantization step. */
    assert(strstr(TestConsole_QueryCached("followstart\n"), "axes=3"));
    uint32_t armed_ms = tick;
    const float peak_a = 0.001f * S5_R5_ELBOW_SUPPORT_MA;
    for (unsigned i = 0U; i < 200U; i++) {
        dual_step(); S5Follow_GetStatus(&s);
        assert(s.phase == S5_FOLLOW_RUNNING && s.output_allowed
            && fabsf(s.target.elbow_current_a) < peak_a * 0.001f);
    }
    assert(s.phase == S5_FOLLOW_RUNNING && s.output_allowed
        && shoulder_nonzero == 0U);
    /* Raise only the mock shoulder slowly after Start. No exact angle is
     * needed to trigger RUNNING; movement makes both posture targets visible. */
    for (unsigned i = 0U; i < 600U; i++) {
        shoulder_rev += 8.0f * 10.0f / (360.0f * 600.0f);
        dual_step(); S5Follow_GetStatus(&s);
        assert(s.phase != S5_FOLLOW_FAULT);
    }
    assert(s.phase == S5_FOLLOW_RUNNING && s.output_allowed
        && shoulder_nonzero != 0U
        && s.sample.sequence[0] != 0U && s.sample.sequence[1] != 0U
        && (uint32_t)(s.sample.sample_ms[0] - armed_ms) > 0U
        && (uint32_t)(s.sample.sample_ms[1] - armed_ms) > 0U);
    const BoardCanFrame *elbow = TestCanTx_Last(BOARD_CAN_ELBOW);
    assert(elbow != NULL && elbow->id == 0x200U
        && (elbow->data[0] != 0U || elbow->data[1] != 0U));
    for (unsigned i = 0U; i < 100U; i++) { dual_step(); S5Follow_GetStatus(&s); }
    float expected_a = peak_a * S5_R5_ELBOW_SUPPORT_SIGN
        * fmaxf(0.0f, sinf(s.joint[0].position_rad + s.joint[1].position_rad));
    assert(fabsf(s.target.elbow_current_a - expected_a) < 0.00001f);
    elbow = TestCanTx_Last(BOARD_CAN_ELBOW);
    int16_t raw = (int16_t)((uint16_t)elbow->data[0] << 8U | elbow->data[1]);
    float wire_a = (float)raw * (20.0f / 16384.0f);
    assert(fabsf(wire_a - expected_a * p->s3.safety.joint[1].mapping.direction)
        < peak_a * 0.002f + 20.0f / 16384.0f);
}

static void r5_dual_chain(void)
{
    r5_dual_start_running(true, false);
    /* A 70-degree posture: the model target and the actual CAN command must
     * both equal the model value, clipped to the shoulder cap when it binds. */
    const S5SiteProfile *site_p = S5Site_GetProfile();
    const float model = site_p->shoulder_gravity.amplitude_nm * site_p->shoulder_gain
        * sinf(70.0f * 3.14159265f / 180.0f) / site_p->s3.safety.joint[0].mapping.rotor_per_joint;
    const float cap = fminf(site_p->s3.safety.joint[0].max_motor_command, model);
    shoulder_rev = 0.25f + 8.0f * 70.0f / 360.0f;
    S5FollowStatus s = {0};
    for (unsigned i = 0U; i < 300U; i++) {
        dual_step(); S5Follow_GetStatus(&s);
        assert(s.phase == S5_FOLLOW_RUNNING);
        if (fabsf(s.target.shoulder_rotor_nm - cap) < 0.0001f
            && fabsf(shoulder_torque - cap) < 0.0001f) { break; }
    }
    assert(fabsf(s.target.shoulder_rotor_nm - cap) < 0.0001f
        && fabsf(shoulder_torque - cap) < 0.0001f);
    TestCanTx_Complete(BOARD_CAN_SHOULDER);
    TestCanTx_Complete(BOARD_CAN_ELBOW);
    uint32_t stop_start_ms = tick;
    uint32_t zero_sent_ms = 0U;
    assert(strstr(TestConsole_QueryCached("followstop\n"), "ramping_down"));
    for (unsigned i = 0U; i < 2000U; i++) {
        dual_step(); S5Follow_GetStatus(&s);
        const BoardCanFrame *sent = TestCanTx_Last(BOARD_CAN_ELBOW);
        if (zero_sent_ms == 0U && sent != NULL && sent->id == 0x200U
            && sent->data[0] == 0U && sent->data[1] == 0U) {
            zero_sent_ms = (uint32_t)(tick - stop_start_ms);
        }
        if (s.phase == S5_FOLLOW_STOPPED || s.phase == S5_FOLLOW_FAULT) { break; }
    }
    assert(s.phase == S5_FOLLOW_STOPPED && s.stop_accepted
        && zero_sent_ms > 0U && zero_sent_ms <= 1000U
        && !s.output_allowed && shoulder_idle == 1U
        && !s.shoulder_zero_valid && !s.elbow_zero_valid);
    const BoardCanFrame *elbow = TestCanTx_Last(BOARD_CAN_ELBOW);
    assert(elbow != NULL && elbow->id == 0x200U);
    for (unsigned i = 0U; i < 8U; i++) { assert(elbow->data[i] == 0U); }
}

static void r5_dual_gravity_pose(void)
{
    r5_dual_start_running(false, false);
    const float peak_a = 0.001f * S5_R5_ELBOW_SUPPORT_MA;
    const struct { float shoulder_deg; uint16_t elbow_ticks; bool flexion; float fraction; } poses[] = {
        {20.0f, 228U, true, 0.5f},
        {20.0f, 455U, false, 0.0f},
        {30.0f, 1365U, true, 1.0f}
    };
    S5FollowStatus s = {0};
    for (unsigned i = 0U; i < sizeof(poses) / sizeof(poses[0]); i++) {
        shoulder_rev = 0.25f + 8.0f * poses[i].shoulder_deg / 360.0f;
        elbow_offset_ticks = poses[i].elbow_ticks;
        elbow_reverse = poses[i].flexion;
        for (unsigned j = 0U; j < 200U; j++) {
            dual_step(); S5Follow_GetStatus(&s);
            assert(s.phase == S5_FOLLOW_RUNNING && s.output_allowed);
        }
        float expected_a = peak_a * S5_R5_ELBOW_SUPPORT_SIGN * poses[i].fraction;
        assert(fabsf(s.target.elbow_current_a - expected_a) < peak_a * 0.002f);
        const BoardCanFrame *frame = TestCanTx_Last(BOARD_CAN_ELBOW);
        assert(frame != NULL && frame->id == 0x200U);
        int16_t raw = (int16_t)((uint16_t)frame->data[0] << 8U | frame->data[1]);
        float wire_a = (float)raw * (20.0f / 16384.0f);
        assert(fabsf(wire_a + expected_a) < peak_a * 0.002f + 20.0f / 16384.0f);
        float shoulder_nm = fminf(0.205f, 1.64f * sinf(s.joint[0].position_rad) / 8.0f);
        assert(fabsf(s.target.shoulder_rotor_nm - shoulder_nm) < 0.00001f);
    }
}

static void r5_dual_elbow_stale(void)
{
    r5_dual_start_running(false, false);
    S5FollowStatus s = {0};
    for (unsigned i = 0U; i < 220U; i++) {
        dual_step_with_elbow(false); S5Follow_GetStatus(&s);
        if (s.phase == S5_FOLLOW_FAULT) { break; }
    }
    assert(s.phase == S5_FOLLOW_FAULT && s.output_state == OUTPUT_FAULT
        && !s.output_allowed && s.stop_attempted);
    dual_step_with_elbow(false); /* Complete the pending shoulder stop frame. */
    assert(shoulder_idle == 1U);
    const BoardCanFrame *elbow = TestCanTx_Last(BOARD_CAN_ELBOW);
    assert(elbow != NULL && elbow->id == 0x200U);
    for (unsigned i = 0U; i < 8U; i++) { assert(elbow->data[i] == 0U); }
}

static void r5_dual_elbow_step(void)
{
    r5_dual_start_running(false, false);
    elbow_offset_ticks = 9U; /* A fresh in-range step previously latched FAULT. */
    dual_step();
    S5FollowStatus s = {0};
    S5Follow_GetStatus(&s);
    assert(s.phase == S5_FOLLOW_RUNNING && s.output_allowed
        && s.joint[1].valid && s.joint[1].position_rad > 0.0f);
}

static void r5_dual_can_history(void)
{
    r5_dual_start_running(false, true);
    TestCan_Inject(BOARD_CAN_ELBOW, 0x201U, 8U, 1U, 0U, 0U);
    TestCan_Interrupt(BOARD_CAN_ELBOW);
    BoardCanStats can;
    assert(BoardCan_GetStats(BOARD_CAN_ELBOW, &can) && can.invalid == 1U);
    dual_step(); /* Fresh valid feedback follows the malformed frame. */
    S5FollowStatus s = {0};
    S5Follow_GetStatus(&s);
    assert(s.phase == S5_FOLLOW_RUNNING && s.output_allowed);
}

static void r5_dual_upper_boundary(void)
{
    r5_dual_start_running(false, false);
    /* Measured front motion ended at 110 degrees; the test boundary is 105. */
    shoulder_rev = 0.25f + 8.0f * 106.0f / 360.0f;
    S5FollowStatus s = {0};
    for (unsigned i = 0U; i < 220U; i++) {
        dual_step(); S5Follow_GetStatus(&s);
        if (s.phase == S5_FOLLOW_FAULT) { break; }
    }
    assert(s.phase == S5_FOLLOW_FAULT && s.safety_reason == SAFETY_RANGE
        && !s.output_allowed && s.stop_attempted);
    dual_step();
    const BoardCanFrame *elbow = TestCanTx_Last(BOARD_CAN_ELBOW);
    assert(elbow != NULL && elbow->id == 0x200U && shoulder_idle == 1U);
    for (unsigned i = 0U; i < 8U; i++) { assert(elbow->data[i] == 0U); }
}

static bool elbow_last_zero(void)
{
    const BoardCanFrame *e = TestCanTx_Last(BOARD_CAN_ELBOW);
    return e != NULL && e->id == 0x200U && e->data[0] == 0U && e->data[1] == 0U;
}

static void r5_dual_soft_pause(void)
{
    r5_dual_start_running(false, false);
    elbow_rpm = 20; /* 120 deg/s: above the 90 deg/s soft pause, below 180. */
    S5FollowStatus s = {0};
    for (unsigned i = 0U; i < 200U; i++) {
        dual_step(); S5Follow_GetStatus(&s);
        assert(s.phase == S5_FOLLOW_RUNNING && s.output_allowed);
        if (s.assist_paused && elbow_last_zero()) { break; }
    }
    assert(s.assist_paused && s.speed_pauses == 1U && elbow_last_zero()
        && s.target.elbow_current_a == 0.0f && s.target.shoulder_rotor_nm == 0.0f
        && fabsf(s.peak_speed_rad_s[1] - 20.0f * 6.2831853f / 60.0f) < 0.01f);
    elbow_rpm = 0;
    shoulder_rev = 0.25f + 8.0f * 30.0f / 360.0f;
    bool resumed = false;
    for (unsigned i = 0U; i < 1000U; i++) {
        dual_step(); S5Follow_GetStatus(&s);
        assert(s.phase == S5_FOLLOW_RUNNING && s.output_allowed);
        if (!s.assist_paused && !elbow_last_zero()) { resumed = true; break; }
    }
    assert(resumed && s.speed_pauses == 1U);
    for (unsigned i = 0U; i < 500U; i++) { dual_step(); S5Follow_GetStatus(&s); }
    float expected_a = 0.001f * S5_R5_ELBOW_SUPPORT_MA * S5_R5_ELBOW_SUPPORT_SIGN
        * fmaxf(0.0f, sinf(s.joint[0].position_rad + s.joint[1].position_rad));
    assert(s.phase == S5_FOLLOW_RUNNING && !s.assist_paused
        && fabsf(s.target.elbow_current_a - expected_a) < 0.00001f);
}

static void r5_dual_hard_speed(void)
{
    r5_dual_start_running(false, false);
    elbow_rpm = 35; /* 210 deg/s exceeds the latched 180 deg/s elbow limit. */
    S5FollowStatus s = {0};
    for (unsigned i = 0U; i < 50U; i++) {
        dual_step(); S5Follow_GetStatus(&s);
        if (s.phase == S5_FOLLOW_FAULT) { break; }
    }
    assert(s.phase == S5_FOLLOW_FAULT && s.safety_reason == SAFETY_SPEED
        && !s.output_allowed && s.stop_attempted);
    dual_step(); /* Complete the pending shoulder stop frame. */
    assert(elbow_last_zero() && shoulder_idle == 1U);
}

static void r5_dual_stop_settling(void)
{
    r5_dual_start_running(false, false);
    TestCanTx_Complete(BOARD_CAN_SHOULDER);
    TestCanTx_Complete(BOARD_CAN_ELBOW);
    assert(strstr(TestConsole_QueryCached("followstop\n"), "ramping_down"));
    S5FollowStatus s = {0};
    for (unsigned i = 0U; i < 4000U; i++) {
        shoulder_rev += 0.0002f; /* About 9 deg/s at the joint: never stationary. */
        dual_step(); S5Follow_GetStatus(&s);
        if (s.phase == S5_FOLLOW_STOPPED || s.phase == S5_FOLLOW_FAULT) { break; }
    }
    assert(s.phase == S5_FOLLOW_STOPPED && s.stop_accepted && !s.output_allowed
        && shoulder_idle == 1U && !s.reader.stop_observed);
}

static void r5_dual_elbow_lower_margin(void)
{
    r5_dual_start_running(false, false);
    /* A hanging forearm reached about -8.9 degrees relative to the upper arm
     * on the bench; about -14.5 degrees must stay RUNNING. Beyond -20 degrees
     * still faults. */
    elbow_reverse = false;
    elbow_offset_ticks = 330U;
    S5FollowStatus s = {0};
    for (unsigned i = 0U; i < 50U; i++) {
        dual_step(); S5Follow_GetStatus(&s);
        assert(s.phase == S5_FOLLOW_RUNNING && s.output_allowed);
    }
    assert(s.joint[1].valid && s.joint[1].position_rad < -0.24f
        && s.joint[1].position_rad > -0.26f);
    elbow_offset_ticks = 467U; /* About -20.5 degrees. */
    for (unsigned i = 0U; i < 50U; i++) {
        dual_step(); S5Follow_GetStatus(&s);
        if (s.phase == S5_FOLLOW_FAULT) { break; }
    }
    assert(s.phase == S5_FOLLOW_FAULT && s.safety_reason == SAFETY_RANGE
        && !s.output_allowed && s.stop_attempted);
}

static float r5_shoulder_base_nm(const S5FollowStatus *s)
{ return 1.64f * sinf(s->joint[0].position_rad) / 8.0f; }

static void r5_dual_amplify(void)
{
    S5Site_TestUseProfile(NULL);
    profile = *S5Site_GetProfile();
    profile.shoulder_amplify.increment_rotor_nm = 0.040f;
    r5_site_override = &profile;
    r5_dual_start_running(false, false);
    const float raise_rev_s = 8.0f * 30.0f / 360.0f; /* 30 deg/s at the joint. */
    S5FollowStatus s = {0};
    shoulder_rev_s = raise_rev_s;
    for (unsigned i = 0U; i < 400U; i++) {
        dual_step(); S5Follow_GetStatus(&s);
        assert(s.phase == S5_FOLLOW_RUNNING && s.output_allowed && !s.assist_paused);
    }
    float base = r5_shoulder_base_nm(&s);
    assert(base > 0.02f && fabsf(s.target.shoulder_rotor_nm - (base + 0.040f)) < 0.0001f
        && fabsf(shoulder_torque - s.target.shoulder_rotor_nm) < 0.0001f);
    shoulder_rev_s = -raise_rev_s; /* Lowering withdraws only the increment. */
    for (unsigned i = 0U; i < 300U; i++) { dual_step(); S5Follow_GetStatus(&s); }
    assert(s.phase == S5_FOLLOW_RUNNING
        && fabsf(s.target.shoulder_rotor_nm - base) < 0.0001f
        && fabsf(shoulder_torque - base) < 0.0001f);
    shoulder_rev_s = raise_rev_s; /* Re-raise waits the 400 ms suspension. */
    for (unsigned i = 0U; i < 350U; i++) {
        dual_step(); S5Follow_GetStatus(&s);
        assert(fabsf(s.target.shoulder_rotor_nm - base) < 0.0001f);
    }
    for (unsigned i = 0U; i < 400U; i++) { dual_step(); S5Follow_GetStatus(&s); }
    assert(fabsf(s.target.shoulder_rotor_nm - (base + 0.040f)) < 0.0001f);
    shoulder_rev_s = 0.0f; /* Quiet: hold, then release to the baseline. */
    for (unsigned i = 0U; i < 800U; i++) { dual_step(); S5Follow_GetStatus(&s); }
    assert(s.phase == S5_FOLLOW_RUNNING
        && fabsf(s.target.shoulder_rotor_nm - base) < 0.0001f);
    shoulder_rev_s = 8.0f * 70.0f / 360.0f; /* Above the 60 deg/s soft pause. */
    for (unsigned i = 0U; i < 100U; i++) { dual_step(); S5Follow_GetStatus(&s); }
    assert(s.phase == S5_FOLLOW_RUNNING && s.assist_paused
        && s.target.shoulder_rotor_nm == 0.0f);
    shoulder_rev_s = 0.0f;
    r5_site_override = NULL;
}
#endif

void TestS5Follow_Run(const char *scenario)
{
    if (!strcmp(scenario, "s5_model_site")) { site_and_model(); }
    else if (!strcmp(scenario, "s5_elbow_gravity")) { elbow_gravity_model(); }
    else if (!strcmp(scenario, "s5_shoulder_amplify")) { shoulder_amplify_model(); }
    else if (!strcmp(scenario, "s5_elbow_follow")) { elbow_chain(); }
    else if (!strcmp(scenario, "s5_dual_follow")) { dual_chain(); }
#ifdef S5_R4_ELBOW_FOLLOW
    else if (!strcmp(scenario, "s5_r4_site")) { r4_isolated_site(); }
#endif
#ifdef S5_R5_DUAL_FOLLOW
    else if (!strcmp(scenario, "s5_r5_site")) { r5_dual_site(); }
    else if (!strcmp(scenario, "s5_r5_dual_gravity_zero")) { r5_dual_gravity_zero(); }
    else if (!strcmp(scenario, "s5_r5_dual_gravity_pose")) { r5_dual_gravity_pose(); }
    else if (!strcmp(scenario, "s5_r5_dual_two_pair_zero")) { r5_dual_two_pair_zero(); }
    else if (!strcmp(scenario, "s5_r5_dual_no_post_zero_dwell")) { r5_dual_no_post_zero_dwell(); }
    else if (!strcmp(scenario, "s5_r5_dual_cancel_early")) { r5_dual_cancel_early(false); }
    else if (!strcmp(scenario, "s5_r5_dual_cancel_pending")) { r5_dual_cancel_early(true); }
    else if (!strcmp(scenario, "s5_r5_dual_chain")) { r5_dual_chain(); }
    else if (!strcmp(scenario, "s5_r5_dual_elbow_stale")) { r5_dual_elbow_stale(); }
    else if (!strcmp(scenario, "s5_r5_dual_elbow_step")) { r5_dual_elbow_step(); }
    else if (!strcmp(scenario, "s5_r5_dual_can_history")) { r5_dual_can_history(); }
    else if (!strcmp(scenario, "s5_r5_dual_upper_boundary")) { r5_dual_upper_boundary(); }
    else if (!strcmp(scenario, "s5_r5_dual_soft_pause")) { r5_dual_soft_pause(); }
    else if (!strcmp(scenario, "s5_r5_dual_hard_speed")) { r5_dual_hard_speed(); }
    else if (!strcmp(scenario, "s5_r5_dual_stop_settling")) { r5_dual_stop_settling(); }
    else if (!strcmp(scenario, "s5_r5_dual_elbow_lower_margin")) { r5_dual_elbow_lower_margin(); }
    else if (!strcmp(scenario, "s5_r5_dual_amplify")) { r5_dual_amplify(); }
#endif
    else { assert(!"unknown S5 scenario"); }
}
