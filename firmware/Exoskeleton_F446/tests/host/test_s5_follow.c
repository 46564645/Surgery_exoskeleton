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
    assert(!S5Site_ReadReady(S5Site_GetProfile()));
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

    /* Reuse the observed S4 one-sided shoulder sector and unchanged 40 mNm cap. */
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
    assert(!S5Site_ProfileReady(&one_sided)); /* Endpoint exceeds 40 mNm. */
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
        && sh->lower_rad == 0.34906585f && sh->upper_rad == 0.52359878f
        && sh->max_speed_rad_s == 0.69813170f
        && sh->max_motor_command == 0.040f && sh->motor_slew_per_s == 0.020f);
    assert(el->mapping.rotor_per_joint == 1.0f
        && el->lower_rad == -0.08726646f && el->upper_rad == 0.08726646f
        && el->max_speed_rad_s == 0.34906585f
        && el->max_motor_command == 2.0f && el->motor_slew_per_s == 20.0f);
    assert(p->s3.safety.feedback_timeout_ms == 150U
        && p->s3.safety.service_limit_ms == 100U
        && p->s3.safety.max_request_ms == 250U
        && p->s3.prepare_timeout_ms == 20000U
        && p->s3.shoulder_property_reply_ms == 20U
        && p->s3.shoulder_property_period_ms == 20U
        && p->s3.shoulder_property_window_ms == 35000U
        && p->s3.shoulder_stop_window_ms == 1500U
        && p->s3.shoulder_standstill.span_rev == 0.001f
        && p->s3.shoulder_standstill.max_velocity_rev_s == 0.02f
        && p->s3.shoulder_standstill.dwell_ms == 200U
        && p->s3.shoulder_standstill.freshness_ms == 150U);
    assert(p->shoulder_gravity.amplitude_nm == 1.64f
        && p->shoulder_gravity.sign_g == 1 && p->shoulder_gain == 0.36f
        && p->shoulder_zero_dwell_ms == 20U
        && p->send_interval_ms == 50U && p->session_ms == 10000U
        && p->ramp_max_ms == 1000U
        && p->release_slew_per_s.shoulder_rotor_nm == 0.080f
        && p->release_slew_per_s.elbow_current_a == 20.0f);
    JointState elbow = {el->upper_rad, 0.0f, 1U, true};
    float current_a = 0.0f;
    const float expected = 0.001f * S5_R5_ELBOW_SUPPORT_MA
        * S5_R5_ELBOW_SUPPORT_SIGN;
    assert(S5ElbowSupport_Compute(&p->elbow_support, &elbow, &current_a)
        && fabsf(current_a - expected) < 0.000001f);
    S5SiteProfile active = *p;
    active.s3.rotor_reference_confirmed[0] = true;
    active.s3.rotor_reference_confirmed[1] = true;
    assert(S5Site_ProfileReady(&active));
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
static float shoulder_torque, shoulder_rev;
static unsigned shoulder_nonzero, shoulder_idle;

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
        case 378U: raw = bits(0.0f); break;
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
        uint16_t angle = (uint16_t)((8191U + (tick & 1U)) % 8192U);
        uint8_t elbow[8] = {(uint8_t)(angle >> 8), (uint8_t)angle,
                            0U, 0U, 0U, 0U, 30U, 0U};
        TestCan_InjectData(BOARD_CAN_ELBOW, 0x201U, 8U, 0U, 0U, elbow);
        TestCan_Interrupt(BOARD_CAN_ELBOW);
    }
    CanMonitor_Poll();
    S5Follow_Poll(tick, 1U);
}

static void dual_step(void) { dual_step_with_elbow(true); }

static void dual_chain(void)
{
    profile = dual_profile();
    assert(S5Site_ReadReady(&profile));
    S5Site_TestUseProfile(&profile);
    tick = 100U; TestClock_Set(tick);
    shoulder_axis_state = 1U; shoulder_mode = shoulder_input = 3U;
    shoulder_torque = 0.0f; shoulder_rev = 0.25f;
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
static void r5_dual_start_running(bool inject_br)
{
    S5Site_TestUseProfile(NULL); /* Use the actual isolated R5 site. */
    tick = 100U; TestClock_Set(tick);
    shoulder_axis_state = 1U; shoulder_mode = shoulder_input = 3U;
    shoulder_torque = 0.0f; shoulder_rev = 0.25f;
    life = 0U; shoulder_nonzero = shoulder_idle = 0U;
    App_Init(); TestCanTx_Reset();
    shoulder_heartbeat(); TestCan_Interrupt(BOARD_CAN_SHOULDER); CanMonitor_Poll();
    assert(S5Follow_IsSelected() && !S4Follow_IsSelected());
    assert(strstr(TestConsole_QueryCached("followprepare\n"), "preparing"));
    S5FollowStatus s = {0};
    bool moved_to_sector = false;
    bool br_injected = false;
    for (unsigned i = 0U; i < 5000U; i++) {
        dual_step(); S5Follow_GetStatus(&s);
        assert(s.phase != S5_FOLLOW_FAULT);
        if (!moved_to_sector && s.shoulder_zero_valid) {
            if (inject_br) {
                /* HAL_CAN_ERROR_BR is 0x40 in the STM32F4 HAL. A single
                 * historical CAN error must not strand fresh dual feedback. */
                const uint32_t br = 0x40U;
                BoardCanStats can;
                assert(s.phase == S5_FOLLOW_PREPARING);
                hcan1.ErrorCode = br;
                HAL_CAN_ErrorCallback(&hcan1);
                assert(BoardCan_GetStats(BOARD_CAN_SHOULDER, &can)
                    && can.errors == 1U && can.last_error == br && !can.bus_off);
                br_injected = true;
            }
            /* 8 rotor turns per joint turn: 0.25 rev origin + 25 joint degrees. */
            shoulder_rev = 0.25f + 8.0f * 25.0f / 360.0f;
            moved_to_sector = true;
        }
        if (s.phase == S5_FOLLOW_READY) { break; }
    }
    assert((!inject_br || br_injected) && moved_to_sector && s.phase == S5_FOLLOW_READY
        && s.shoulder_zero_valid && s.elbow_zero_valid
        && fabsf(s.joint[0].position_rad - 0.43633231f) < 0.005f);
    assert(strstr(TestConsole_QueryCached("followstart\n"), "axes=3"));
    uint32_t armed_ms = tick;
    for (unsigned i = 0U; i < 400U; i++) {
        dual_step(); S5Follow_GetStatus(&s);
        assert(s.phase != S5_FOLLOW_FAULT);
        const BoardCanFrame *elbow = TestCanTx_Last(BOARD_CAN_ELBOW);
        if (shoulder_nonzero != 0U && elbow != NULL && elbow->id == 0x200U
            && (elbow->data[0] != 0U || elbow->data[1] != 0U)) { break; }
    }
    assert(s.phase == S5_FOLLOW_RUNNING && s.output_allowed
        && shoulder_nonzero != 0U
        && s.sample.sequence[0] != 0U && s.sample.sequence[1] != 0U
        && (uint32_t)(s.sample.sample_ms[0] - armed_ms) > 0U
        && (uint32_t)(s.sample.sample_ms[1] - armed_ms) > 0U);
    const BoardCanFrame *elbow = TestCanTx_Last(BOARD_CAN_ELBOW);
    assert(elbow != NULL && elbow->id == 0x200U
        && (elbow->data[0] != 0U || elbow->data[1] != 0U));
}

static void r5_dual_chain(void)
{
    r5_dual_start_running(true);
    TestCanTx_Complete(BOARD_CAN_SHOULDER);
    TestCanTx_Complete(BOARD_CAN_ELBOW);
    assert(strstr(TestConsole_QueryCached("followstop\n"), "ramping_down"));
    S5FollowStatus s = {0};
    for (unsigned i = 0U; i < 2000U; i++) {
        dual_step(); S5Follow_GetStatus(&s);
        if (s.phase == S5_FOLLOW_STOPPED || s.phase == S5_FOLLOW_FAULT) { break; }
    }
    assert(s.phase == S5_FOLLOW_STOPPED && s.stop_accepted
        && !s.output_allowed && shoulder_idle == 1U
        && !s.shoulder_zero_valid && !s.elbow_zero_valid);
    const BoardCanFrame *elbow = TestCanTx_Last(BOARD_CAN_ELBOW);
    assert(elbow != NULL && elbow->id == 0x200U);
    for (unsigned i = 0U; i < 8U; i++) { assert(elbow->data[i] == 0U); }
}

static void r5_dual_elbow_stale(void)
{
    r5_dual_start_running(false);
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
#endif

void TestS5Follow_Run(const char *scenario)
{
    if (!strcmp(scenario, "s5_model_site")) { site_and_model(); }
    else if (!strcmp(scenario, "s5_elbow_follow")) { elbow_chain(); }
    else if (!strcmp(scenario, "s5_dual_follow")) { dual_chain(); }
#ifdef S5_R4_ELBOW_FOLLOW
    else if (!strcmp(scenario, "s5_r4_site")) { r4_isolated_site(); }
#endif
#ifdef S5_R5_DUAL_FOLLOW
    else if (!strcmp(scenario, "s5_r5_site")) { r5_dual_site(); }
    else if (!strcmp(scenario, "s5_r5_dual_chain")) { r5_dual_chain(); }
    else if (!strcmp(scenario, "s5_r5_dual_elbow_stale")) { r5_dual_elbow_stale(); }
#endif
    else { assert(!"unknown S5 scenario"); }
}
