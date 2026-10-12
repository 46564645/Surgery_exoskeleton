/* Full S4 synthetic CAN and CLI path. All values below are TEST-only. */
#include "test_s4_follow_output.h"
#include "test_console_probe.h"
#include "s4_follow.h"
#include "s3_bench.h"
#include "s3_site_mock.h"
#include "can_monitor.h"
#include "can_mock.h"
#include "can_tx_mock.h"
#include "board_console.h"
#include "console_commands.h"
#include "s1_config.h"
#include "s2_config.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static S4SiteProfile profile;
static uint32_t tick;
static uint8_t axis_state, control_mode, input_mode, life;
static float input_torque_nm, rotor_rev, rotor_velocity_rev_s;
static unsigned reads, torques, modes, idles;

static uint32_t bits(float value)
{ uint32_t raw; memcpy(&raw, &value, sizeof(raw)); return raw; }

static void heartbeat(void)
{
    uint8_t data[8] = {0};
    data[4] = axis_state; data[7] = ++life;
    TestCan_InjectData(BOARD_CAN_SHOULDER, 0x021U, 8U, 0U, 0U, data);
}

static S4SiteProfile test_profile(void)
{
    S4SiteProfile p = {0};
    p.s3.shoulder_source = S3_SHOULDER_ENCODER_PROPERTIES;
    p.s3.shoulder_node = S2_GDS_NODE; p.s3.elbow_id = S2_C620_ID;
    p.s3.safety.enabled_axes = SAFETY_SHOULDER_AXES;
    p.s3.safety.stop_policy = SAFETY_STOP_SUPERVISED_SHOULDER_FOLLOW;
    p.s3.safety.protection_confirmed = true;
    p.s3.safety.stop_method_confirmed = true;
    p.s3.safety.timing_confirmed = true;
    p.s3.safety.feedback_timeout_ms = 100U;
    p.s3.safety.service_limit_ms = 5U;
    p.s3.safety.max_request_ms = 100U;
    SafetyJointConfig *j = &p.s3.safety.joint[0];
    j->mapping = (JointMapping){2.0f, 0.0f, 1, true};
    j->lower_rad = -1.0f; j->upper_rad = 2.0f;
    j->max_speed_rad_s = 2.0f;
    j->max_motor_command = 0.05f; j->motor_slew_per_s = 1.0f;
    j->limits_confirmed = j->feedback_confirmed = j->protocol_confirmed = true;
    p.s3.trusted_feedback_axes = SAFETY_SHOULDER_AXES;
    p.s3.coordinates[0] = S3_ASSEMBLED_JOINT;
    p.s3.rotor_reference_confirmed[0] = true;
    p.s3.prepare_timeout_ms = 220U;
    p.s3.shoulder_property_reply_ms = 4U;
    p.s3.shoulder_property_period_ms = 20U;
    p.s3.shoulder_property_window_ms = 1200U;
    p.s3.shoulder_standstill = (ShoulderStandstillConfig){0.001f, 0.02f, 10U, 50U};
    p.s3.shoulder_stop_window_ms = 200U;
    p.gravity = (GravityCompensationConfig){0.02f, 0.0f, 1, true};
    p.assist_gain = 0.5f;
    p.release_slew_nm_s = 1.0f;
    p.send_interval_ms = 2U;
    p.session_ms = 300U;
    p.ramp_max_ms = 100U;
    p.stop_submit_budget_ms = 10U;
    p.session_zero_dwell_ms = 10U;
    p.output_enabled = true;
    return p;
}

static void begin_profile_with_stop_dwell(uint32_t session_ms, bool session_zero,
                                          float lower_rad, uint32_t stop_dwell_ms)
{
    TestS3Site_Reset();
    profile = test_profile();
    if (stop_dwell_ms != 0U) {
        profile.s3.shoulder_standstill.dwell_ms = stop_dwell_ms;
        if (stop_dwell_ms >= profile.s3.shoulder_stop_window_ms) {
            profile.s3.prepare_timeout_ms = stop_dwell_ms + 400U;
            profile.s3.shoulder_stop_window_ms = stop_dwell_ms + 200U;
        }
    }
    if (session_ms != 0U) {
        profile.session_ms = session_ms;
        profile.s3.shoulder_property_window_ms = session_ms + profile.s3.prepare_timeout_ms + 300U;
    }
    if (session_zero) {
        profile.session_zero_on_prepare = true;
        profile.s3.rotor_reference_confirmed[0] = false;
        profile.s3.safety.joint[0].lower_rad = lower_rad;
        assert(!S4Site_ProfileReady(&profile) && S4Site_SessionZeroReady(&profile));
    } else { assert(S4Site_ProfileReady(&profile)); }
    tick = 100U; axis_state = 1U; control_mode = input_mode = 3U; life = 0U;
    input_torque_nm = 0.0f; rotor_rev = 0.25f; rotor_velocity_rev_s = 0.0f;
    reads = torques = modes = idles = 0U;
    TestClock_Set(tick);
    CanMonitor_Init(&profile.s3, SHOULDER_READER_S4);
    S3Bench_Init();
    TestCanTx_Reset();
    S4Follow_Init(&profile);
    /* Several synthetic cold boots share this host process; the UART mock is
     * already started after the first case and has no restart entry point. */
    if (!BoardConsole_Start()) { assert(!BoardConsole_HasFault()); }
    ConsoleCommands_Init();
    heartbeat(); TestCan_Interrupt(BOARD_CAN_SHOULDER); CanMonitor_Poll();
    S4FollowStatus s; S4Follow_GetStatus(&s);
    assert(s.phase == S4_FOLLOW_IDLE && s.output_bound);
    assert(TestCanTx_Sent(BOARD_CAN_SHOULDER) == 0U);
}

static void begin_profile(uint32_t session_ms, bool session_zero, float lower_rad)
{ begin_profile_with_stop_dwell(session_ms, session_zero, lower_rad, 0U); }

static void begin(uint32_t session_ms) { begin_profile(session_ms, false, -1.0f); }

static void receive_output(const BoardCanFrame *frame)
{
    if (frame->id == 0x024U) {
        reads++;
        unsigned ep = frame->data[1] | ((unsigned)frame->data[2] << 8);
        uint32_t raw;
        switch (ep) {
        case 372U: raw = bits(rotor_rev); break;
        case 378U: raw = bits(rotor_velocity_rev_s); break;
        case 195U: case 366U: raw = 1U; break;
        case 272U: raw = bits(input_torque_nm); break;
        case 287U: raw = control_mode; break;
        case 288U: raw = input_mode; break;
        default: assert(false); raw = 0U;
        }
        uint8_t data[8] = {0};
        data[1] = frame->data[1]; data[2] = frame->data[2];
        for (unsigned i = 0U; i < 4U; i++) { data[4U+i] = (uint8_t)(raw >> (8U*i)); }
        TestCan_InjectData(BOARD_CAN_SHOULDER, 0x025U, 8U, 0U, 0U, data);
    } else if (frame->id == 0x02EU) {
        memcpy(&input_torque_nm, frame->data, sizeof(input_torque_nm)); torques++;
    } else if (frame->id == 0x02BU) {
        modes++;
        control_mode = frame->data[0]; input_mode = frame->data[4];
    } else if (frame->id == 0x027U) {
        axis_state = frame->data[0];
        if (axis_state == 1U) { idles++; }
        else { assert(axis_state == 8U); }
    } else { assert(false); }
}

static void step(void)
{
    tick++; TestClock_Set(tick);
    BoardCanFrame frame[3]; unsigned count = 0U;
    for (unsigned i = 0U; i < 3U; i++) {
        const BoardCanFrame *pending = TestCanTx_Pending(BOARD_CAN_SHOULDER, i);
        if (pending != NULL) { frame[count++] = *pending; }
    }
    TestCanTx_Complete(BOARD_CAN_SHOULDER);
    for (unsigned i = 0U; i < count; i++) {
        for (unsigned j = i+1U; j < count; j++) {
            if (frame[j].id < frame[i].id) {
                BoardCanFrame swap = frame[i]; frame[i] = frame[j]; frame[j] = swap;
            }
        }
    }
    for (unsigned i = 0U; i < count; i++) { receive_output(&frame[i]); }
    heartbeat(); TestCan_Interrupt(BOARD_CAN_SHOULDER);
    CanMonitor_Poll(); BoardConsole_Poll(tick);
    S4Follow_Poll(tick, 1U);
    assert(TestCanTx_Sent(BOARD_CAN_ELBOW) == 0U);
}

static void ready(void)
{
    assert(strstr(TestConsole_QueryCached("followprepare\n"), "OK FOLLOW preparing"));
    for (unsigned i = 0U; i < 220U; i++) {
        step();
        S4FollowStatus s; S4Follow_GetStatus(&s);
        if (s.phase == S4_FOLLOW_READY) { assert(reads >= 14U); return; }
        assert(s.phase != S4_FOLLOW_FAULT);
    }
    assert(false);
}

static void cli_and_cancel(void)
{
    begin(0U);
    const char *empty_angle = TestConsole_QueryCached("followangle\n");
    assert(strstr(empty_angle, "seq=0 pos_mrev=na joint_urad=na sample_ms=na"));
    assert(strstr(empty_angle, "age_ms=na fresh=0 angle_valid=0")
        && strstr(empty_angle, "output_allowed=0"));
    assert(!strcmp(TestConsole_QueryRaw("prepare\n"), "ERR s4_mode\r\n"));
    assert(!strcmp(TestConsole_QueryRaw("arm\n"), "ERR s4_mode\r\n"));
    assert(!strcmp(TestConsole_QueryRaw("shoulderpulse 1 20 100\n"), "ERR s4_mode\r\n"));
    assert(!strcmp(TestConsole_QueryRaw("elbowpulse 1 20 100\n"), "ERR s4_mode\r\n"));
    ready();
    const char *started = TestConsole_QueryCached("followstart\n");
    assert(strstr(started, "expires_in_ms=300; ramp_in_ms=200; support arm before ramp"));
    for (unsigned i = 0U; i < 60U; i++) { step(); }
    S4FollowStatus s; S4Follow_GetStatus(&s);
    assert(s.phase == S4_FOLLOW_RUNNING && s.output_allowed && torques > 4U);
    assert(reads > 14U && s.model_valid && s.model_sequence > 0U);
    const char *line = TestConsole_QueryCached("followstatus\n");
    assert(strstr(line, "output_allowed=1") && strstr(line, "cmd_torque=1"));
    line = TestConsole_QueryCached("followangle\n");
    assert(strlen(line) < S1_REPLY_SIZE);
    char expected[100];
    uint32_t sample_ms = s.observation.properties.requested_ms[GDS_PROPERTY_POSITION];
    (void)snprintf(expected, sizeof(expected), "seq=%lu pos_mrev=250 joint_urad=%ld sample_ms=%lu",
        (unsigned long)s.observation.properties.sequence,
        (long)(int32_t)(s.observation.joint.position_rad * 1000000.0f),
        (unsigned long)sample_ms);
    assert(strstr(line, expected));
    (void)snprintf(expected, sizeof(expected), "asof_ms=%lu age_ms=%lu fresh=1 angle_valid=1",
        (unsigned long)tick, (unsigned long)(tick - sample_ms));
    assert(strstr(line, expected) && strstr(line, "output_allowed=1"));
    line = TestConsole_QueryCached("followangle\n");
    assert(strstr(line, expected)); /* A query never invents a new seven-property cycle. */
    TestClock_Set(tick + profile.s3.safety.feedback_timeout_ms + 1U);
    line = TestConsole_QueryCached("followangle\n");
    assert(strstr(line, "fresh=0 angle_valid=0") && strstr(line, "output_allowed=1")); /* Poll cache cannot claim live data. */
    TestClock_Set(sample_ms - 1U);
    line = TestConsole_QueryCached("followangle\n");
    assert(strstr(line, "age_ms=4294967295 fresh=0 angle_valid=0")); /* Future sample is invalid. */
    TestClock_Set(tick);
    assert(strstr(TestConsole_QueryCached("cancel\n"), "ramping_down; support arm"));
    S4Follow_GetStatus(&s); assert(s.phase == S4_FOLLOW_RAMPING_DOWN && !s.output_allowed);
    bool saw_stopping = false;
    for (unsigned i = 0U; i < 200U; i++) {
        step(); S4Follow_GetStatus(&s);
        if (s.phase == S4_FOLLOW_STOPPING) {
            saw_stopping = true;
            assert(strstr(TestConsole_QueryCached("followstop\n"),
                "idle_submitted; observing_stop; support arm"));
        }
        if (s.phase == S4_FOLLOW_STOPPED || s.phase == S4_FOLLOW_FAULT) { break; }
    }
    assert(saw_stopping && s.phase == S4_FOLLOW_STOPPED && s.stop_accepted && idles == 1U);
    line = TestConsole_QueryCached("followangle\n");
    assert(strstr(line, "fresh=0 angle_valid=0")
        && strstr(line, "output_allowed=0 ramping_down=0 cmd_torque=0"));
    assert(strstr(line, "sample_ms=na") == NULL); /* Cached values are labeled stale. */
    assert(strstr(TestConsole_QueryCached("followstop\n"), "session_closed; support arm"));
    assert(!S4Follow_Prepare(tick) && !S4Follow_Start(tick));
    assert(!strcmp(TestConsole_QueryRaw("followprepare\n"), "ERR blocked\r\n"));
    assert(TestCanTx_Sent(BOARD_CAN_ELBOW) == 0U);
}

static void expiry(void)
{
    begin(0U); ready();
    assert(strstr(TestConsole_QueryCached("followstart\n"), "ramp_in_ms=200"));
    S4FollowStatus s;
    for (unsigned i = 0U; i < 400U; i++) {
        step(); S4Follow_GetStatus(&s);
        if (s.phase == S4_FOLLOW_STOPPED || s.phase == S4_FOLLOW_FAULT) { break; }
    }
    assert(s.phase == S4_FOLLOW_STOPPED && s.stop_accepted && idles == 1U);
    assert(!s.output_allowed && !S4Follow_Prepare(tick));
}

static void arm_inside_read_cycle(void)
{
    begin(0U); ready();
    bool pending_position = false;
    for (unsigned i = 0U; i < 40U; i++) {
        step();
        for (unsigned slot = 0U; slot < 3U; slot++) {
            const BoardCanFrame *f = TestCanTx_Pending(BOARD_CAN_SHOULDER, slot);
            if (f != NULL && f->id == 0x024U && f->data[1] == 0x74U
                && f->data[2] == 0x01U) { pending_position = true; }
        }
        if (pending_position) { break; }
    }
    assert(pending_position);
    tick++; TestClock_Set(tick); /* Arm after position request, before its reply. */
    assert(strstr(TestConsole_QueryCached("followstart\n"), "OK FOLLOW started"));
    for (unsigned i = 0U; i < 8U; i++) { step(); }
    S4FollowStatus s; S4Follow_GetStatus(&s);
    assert(s.phase == S4_FOLLOW_RUNNING && s.output_state == OUTPUT_ARMED);
    assert(input_torque_nm == 0.0f);
    for (unsigned i = 0U; i < 45U; i++) { step(); }
    S4Follow_GetStatus(&s);
    assert(s.phase == S4_FOLLOW_RUNNING && s.output_state == OUTPUT_ACTIVE);
    assert(input_torque_nm > 0.0f && reads >= 21U);
}

static void long_warning_queue(void)
{
    begin(3300U); ready();
    assert(strstr(TestConsole_QueryCached("followstart\n"), "ramp_in_ms=3200"));
    const uint32_t armed = tick;
    while (tick - armed < 1199U) { step(); }
    S4FollowStatus s; S4Follow_GetStatus(&s);
    assert(!s.warning_due && !s.warning_queued && s.output_allowed);
    uint8_t fill[1800]; memset(fill, 'X', sizeof(fill));
    assert(BoardConsole_Write(fill, sizeof(fill)));
    assert(BoardConsole_TxFree() < S1_REPLY_SIZE + 80U);
    step(); /* Lead instant. A full queue cannot defer the control schedule. */
    S4Follow_GetStatus(&s);
    assert(s.warning_due && !s.warning_queued && !s.warning_failed && s.output_allowed);
    TestConsole_Drain(10U);
    step();
    S4Follow_GetStatus(&s);
    assert(s.warning_queued && !s.warning_failed && s.output_allowed);
    const char *line = TestConsole_QueryRaw("ping\n");
    assert(strstr(line, "WARN FOLLOW ending soon; support arm; torque will ramp down\r\n"));
    assert(strstr(line, "OK pong\r\n"));
    for (unsigned i = 0U; i < 10U; i++) { step(); }
    assert(!strcmp(TestConsole_QueryRaw("ping\n"), "OK pong\r\n"));
}

static void session_zero_success(void)
{
    begin_profile(0U, true, -1.0f);
    S4FollowStatus s; S4Follow_GetStatus(&s);
    assert(s.output_bound && !s.session_zero_valid && !S4Follow_Start(tick));
    assert(strstr(TestConsole_QueryCached("followprepare\n"), "OK FOLLOW preparing"));
    bool captured = false;
    for (unsigned i = 0U; i < 220U; i++) {
        step(); S4Follow_GetStatus(&s);
        if (!s.session_zero_valid) {
            assert(torques == 0U && axis_state == 1U && s.output_state == OUTPUT_SAFE_OFF);
        } else {
            captured = true;
            assert(s.session_zero_sequence >= 2U && s.session_zero_sample_ms > 100U);
            assert(fabsf(s.session_zero_rev - 0.25f) < 0.000001f);
            assert(fabsf(s.observation.joint.position_rad) < 0.000001f);
            assert(!s.output_allowed);
        }
        if (s.phase == S4_FOLLOW_READY) { break; }
        assert(s.phase != S4_FOLLOW_FAULT);
    }
    assert(captured && s.phase == S4_FOLLOW_READY && s.session_zero_valid);
    const char *angle = TestConsole_QueryCached("followangle\n");
    assert(strstr(angle, "zero_mrev=250 zero_ms=")
        && strstr(angle, "angle_valid=1"));
    assert(strstr(TestConsole_QueryCached("followstatus\n"), "zero=1"));
    rotor_rev = 0.30f;
    assert(strstr(TestConsole_QueryCached("followstart\n"), "OK FOLLOW started"));
    for (unsigned i = 0U; i < 60U; i++) { step(); }
    S4Follow_GetStatus(&s);
    assert(s.phase == S4_FOLLOW_RUNNING && s.output_allowed && torques > 0U);
    assert(s.model_valid && s.model_joint_torque_nm > 0.0f);
    assert(strstr(TestConsole_QueryCached("followstop\n"), "ramping_down"));
    S4Follow_GetStatus(&s);
    assert(!s.session_zero_valid && !s.output_allowed);
    angle = TestConsole_QueryCached("followangle\n");
    assert(strstr(angle, "angle_valid=0 zero_mrev=na zero_ms=na"));
    for (unsigned i = 0U; i < 200U; i++) {
        step(); S4Follow_GetStatus(&s);
        if (s.phase == S4_FOLLOW_STOPPED || s.phase == S4_FOLLOW_FAULT) { break; }
    }
    assert(s.phase == S4_FOLLOW_STOPPED && !S4Follow_Prepare(tick)
        && !S4Follow_Start(tick));
    const char *status_line = TestConsole_QueryCached("followstatus\n");
    assert(strstr(status_line, "fresh=0") && strstr(status_line, "fault=0"));
}

static void session_zero_dwell_independent(void)
{
    /* Stop confirmation may require longer than feedback freshness. The two
     * complete zero samples use their own shorter minimum while SAFE_OFF. */
    begin_profile_with_stop_dwell(0U, true, -1.0f, 101U);
    assert(strstr(TestConsole_QueryCached("followprepare\n"), "OK FOLLOW preparing"));
    S4FollowStatus s;
    bool captured = false;
    for (unsigned i = 0U; i < 100U; i++) {
        step(); S4Follow_GetStatus(&s);
        assert(torques == 0U && s.output_state == OUTPUT_SAFE_OFF);
        if (s.session_zero_valid) { captured = true; break; }
        assert(s.phase != S4_FOLLOW_FAULT);
    }
    assert(captured && s.session_zero_valid && !s.output_allowed);
    S4Follow_Cancel();
}

static void session_zero_rejections(void)
{
    S4FollowStatus s;
    begin_profile(0U, true, -1.0f);
    assert(strstr(TestConsole_QueryCached("followprepare\n"), "OK FOLLOW preparing"));
    for (unsigned i = 0U; i < 5U; i++) {
        step(); S4Follow_GetStatus(&s);
        assert(!s.session_zero_valid && torques == 0U);
    }
    assert(strstr(TestConsole_QueryCached("followstop\n"), "read_cancel_pending"));
    for (unsigned i = 0U; i < 10U; i++) {
        step(); S4Follow_GetStatus(&s);
        if (s.phase == S4_FOLLOW_STOPPED || s.phase == S4_FOLLOW_FAULT) { break; }
    }
    assert(s.phase == S4_FOLLOW_STOPPED && !s.session_zero_valid
        && !S4Follow_Prepare(tick) && torques == 0U);

    begin_profile(0U, true, -1.0f);
    input_torque_nm = 0.01f; /* A nonzero driver target cannot certify zero. */
    assert(strstr(TestConsole_QueryCached("followprepare\n"), "OK FOLLOW preparing"));
    for (unsigned i = 0U; i < 80U; i++) {
        step(); S4Follow_GetStatus(&s);
        if (s.phase == S4_FOLLOW_FAULT) { break; }
    }
    assert(s.phase == S4_FOLLOW_FAULT && !s.session_zero_valid && torques == 0U
        && !S4Follow_Start(tick) && !S4Follow_Prepare(tick));
    const char *status_line = TestConsole_QueryCached("followstatus\n");
    assert(strstr(status_line, "fresh=0") && strstr(status_line, "fault=1"));

    begin_profile(0U, true, -1.0f);
    assert(strstr(TestConsole_QueryCached("followprepare\n"), "OK FOLLOW preparing"));
    for (unsigned i = 0U; i < 80U && reads < 7U; i++) { step(); }
    assert(reads >= 7U);
    S4Follow_GetStatus(&s);
    assert(!s.session_zero_valid && torques == 0U);
    rotor_rev += 0.01f; /* Movement exceeds the configured stationary span. */
    for (unsigned i = 0U; i < 80U; i++) {
        step(); S4Follow_GetStatus(&s);
        if (s.phase == S4_FOLLOW_FAULT) { break; }
    }
    assert(s.phase == S4_FOLLOW_FAULT && !s.session_zero_valid && torques == 0U);
}

static void session_zero_one_sided(void)
{
    S4FollowStatus s;
    begin_profile(0U, true, 0.05f); /* Vertical q=0 lies below the approved range. */
    assert(strstr(TestConsole_QueryCached("followprepare\n"), "OK FOLLOW preparing"));
    bool captured = false;
    for (unsigned i = 0U; i < 100U; i++) {
        step(); S4Follow_GetStatus(&s);
        assert(torques == 0U && modes == 0U && idles == 0U && axis_state == 1U);
        if (s.session_zero_valid) { captured = true; break; }
        assert(s.phase == S4_FOLLOW_PREPARING);
    }
    assert(captured && s.phase == S4_FOLLOW_PREPARING
        && s.output_state == OUTPUT_SAFE_OFF);
    uint32_t captured_sequence = s.session_zero_sequence;
    for (unsigned i = 0U; i < 15U; i++) {
        step(); S4Follow_GetStatus(&s);
        assert(s.phase == S4_FOLLOW_PREPARING && s.session_zero_valid
            && s.output_state == OUTPUT_SAFE_OFF && !s.output_allowed
            && torques == 0U && modes == 0U && idles == 0U && axis_state == 1U);
    }
    rotor_rev = 0.30f; /* Synthetic manual push; measured q is now inside range. */
    for (unsigned i = 0U; i < 100U; i++) {
        step(); S4Follow_GetStatus(&s);
        if (torques != 0U || modes != 0U || axis_state != 1U) {
            assert(s.observation.properties.sequence > captured_sequence
                && s.observation.joint.valid
                && s.observation.joint.position_rad >= 0.05f);
        }
        if (s.phase == S4_FOLLOW_READY) { break; }
        assert(s.phase != S4_FOLLOW_FAULT);
    }
    assert(s.phase == S4_FOLLOW_READY && s.session_zero_valid && torques >= 1U);
    S4Follow_Cancel();
    S4Follow_GetStatus(&s);
    assert(!s.session_zero_valid && !s.output_allowed && !S4Follow_Start(tick));

    begin_profile(0U, true, 0.05f);
    assert(strstr(TestConsole_QueryCached("followprepare\n"), "OK FOLLOW preparing"));
    for (unsigned i = 0U; i < 240U; i++) {
        step(); S4Follow_GetStatus(&s);
        if (s.phase == S4_FOLLOW_FAULT) { break; }
        assert(torques == 0U && modes == 0U && idles == 0U && axis_state == 1U);
    }
    assert(s.phase == S4_FOLLOW_FAULT && !s.session_zero_valid
        && torques == 0U && modes == 0U && idles == 0U
        && !S4Follow_Prepare(tick) && !S4Follow_Start(tick));
}

static void session_zero_waits_for_in_range_standstill(void)
{
    /* Reproduce the bench fault: one low-speed in-range sample during a hand
     * move must not start the zero-target/mode/closed-loop preparation. */
    begin_profile_with_stop_dwell(0U, true, 0.35f, 200U);
    assert(strstr(TestConsole_QueryCached("followprepare\n"), "OK FOLLOW preparing"));
    S4FollowStatus s;
    for (unsigned i = 0U; i < 100U; i++) {
        step(); S4Follow_GetStatus(&s);
        if (s.session_zero_valid) { break; }
        assert(s.phase == S4_FOLLOW_PREPARING);
    }
    assert(s.session_zero_valid && s.output_state == OUTPUT_SAFE_OFF);
    uint32_t zero_sequence = s.session_zero_sequence;
    rotor_rev = 0.364f; /* q=0.358 rad, just inside the one-sided range. */
    rotor_velocity_rev_s = 0.10f; /* Below run speed cap, still moving. */
    bool saw_in_range = false;
    for (unsigned i = 0U; i < 60U; i++) {
        step(); S4Follow_GetStatus(&s);
        assert(s.phase == S4_FOLLOW_PREPARING && s.output_state == OUTPUT_SAFE_OFF);
        assert(torques == 0U && modes == 0U && idles == 0U && axis_state == 1U);
        if (s.observation.properties.sequence > zero_sequence
            && s.observation.joint.valid && s.observation.joint.position_rad >= 0.35f) {
            saw_in_range = true; break;
        }
    }
    assert(saw_in_range);
    rotor_rev = 0.37f; rotor_velocity_rev_s = 0.0f;
    for (unsigned i = 0U; i < 60U; i++) {
        step(); S4Follow_GetStatus(&s);
        assert(s.phase == S4_FOLLOW_PREPARING && s.output_state == OUTPUT_SAFE_OFF);
        assert(torques == 0U && modes == 0U && idles == 0U);
    }
    rotor_velocity_rev_s = 1.0f; /* Fresh speed above the run cap clears partial dwell. */
    uint32_t prior_sequence = s.observation.properties.sequence;
    for (unsigned i = 0U; i < 60U; i++) {
        step(); S4Follow_GetStatus(&s);
        assert(s.phase == S4_FOLLOW_PREPARING && s.output_state == OUTPUT_SAFE_OFF);
        assert(torques == 0U && modes == 0U && idles == 0U);
        if (s.observation.properties.sequence > prior_sequence) { break; }
    }
    assert(s.observation.properties.sequence > prior_sequence);
    rotor_velocity_rev_s = 0.0f;
    rotor_rev = 0.30f; /* A fresh out-of-range sample must reset partial dwell. */
    prior_sequence = s.observation.properties.sequence;
    for (unsigned i = 0U; i < 60U; i++) {
        step(); S4Follow_GetStatus(&s);
        assert(s.phase == S4_FOLLOW_PREPARING && s.output_state == OUTPUT_SAFE_OFF);
        assert(torques == 0U && modes == 0U && idles == 0U);
        if (s.observation.properties.sequence > prior_sequence
            && s.observation.joint.valid && s.observation.joint.position_rad < 0.35f) { break; }
    }
    assert(s.observation.joint.valid && s.observation.joint.position_rad < 0.35f);
    rotor_rev = 0.37f;
    uint32_t entered_ms = tick;
    for (unsigned i = 0U; i < 150U; i++) {
        step(); S4Follow_GetStatus(&s);
        assert(s.phase == S4_FOLLOW_PREPARING && s.output_state == OUTPUT_SAFE_OFF);
        assert(torques == 0U && modes == 0U && idles == 0U);
    }
    assert(tick - entered_ms == 150U);
    bool prepared = false;
    for (unsigned i = 0U; i < 160U; i++) {
        step(); S4Follow_GetStatus(&s);
        if (s.output_state != OUTPUT_SAFE_OFF) { prepared = true; break; }
        assert(s.phase == S4_FOLLOW_PREPARING);
    }
    assert(prepared && s.phase == S4_FOLLOW_PREPARING && !s.output_allowed);
}

void TestS4FollowOutput_Run(const char *scenario)
{
    if (!strcmp(scenario, "s4_follow_cli_cancel")) {
        cli_and_cancel(); session_zero_success(); session_zero_dwell_independent();
        session_zero_rejections();
        session_zero_one_sided();
        session_zero_waits_for_in_range_standstill();
    }
    else if (!strcmp(scenario, "s4_follow_expiry")) { expiry(); }
    else if (!strcmp(scenario, "s4_follow_midcycle_arm")) { arm_inside_read_cycle(); }
    else if (!strcmp(scenario, "s4_follow_long_warning")) { long_warning_queue(); }
    else { assert(!"Unknown S4 FOLLOW output scenario"); }
}
