#include "test_s4_site.h"
#include "s4_site.h"
#include "test_s3.h"
#include "app_main.h"
#include "s4_follow.h"
#include <assert.h>
#include <string.h>

void TestS4Site_Run(const char *scenario)
{
    assert(strcmp(scenario, "s4_site") == 0);
    assert(!S4Site_ReadReady(S4Site_GetProfile()));
    S4Site_TestUseProduction(true);
    const S4SiteProfile *production = S4Site_GetProfile();
    assert(production != NULL && !production->output_enabled);
    assert(production->s3.safety.enabled_axes == 0U);
    assert(S4Site_ReadReady(production));
    assert(!S4Site_ObservationReady(production));
    assert(!S4Site_ProfileReady(production));
    App_Init();
    assert(S4Follow_IsSelected());
    S4FollowStatus production_status;
    S4Follow_GetStatus(&production_status);
    assert(production_status.phase == S4_FOLLOW_IDLE && !production_status.output_bound
        && !production_status.output_allowed);

    S4SiteProfile p = {0};
    p.s3.shoulder_source = S3_SHOULDER_ENCODER_PROPERTIES;
    p.s3.shoulder_node = production->s3.shoulder_node;
    p.s3.safety.feedback_timeout_ms = 100U;
    p.s3.shoulder_property_reply_ms = 20U;
    p.s3.shoulder_property_period_ms = 50U;
    p.s3.shoulder_property_window_ms = 500U;
    assert(S4Site_ReadReady(&p));
    assert(!S4Site_ObservationReady(&p) && !S4Site_ProfileReady(&p));
    p.s3.shoulder_property_window_ms = GDS_PROPERTY_COUNT * p.s3.shoulder_property_reply_ms;
    assert(!S4Site_ReadReady(&p));
    p.s3.shoulder_property_window_ms = 500U;
    p.s3.shoulder_property_period_ms = 100U;
    assert(!S4Site_ReadReady(&p));
    p.s3.shoulder_property_period_ms = 50U;

    p.s3.coordinates[0] = S3_ASSEMBLED_JOINT;
    p.s3.rotor_reference_confirmed[0] = true;
    p.s3.trusted_feedback_axes = SAFETY_SHOULDER_AXES;
    p.s3.safety.joint[0].feedback_confirmed = true;
    p.s3.safety.joint[0].protocol_confirmed = true;
    p.s3.safety.joint[0].mapping = (JointMapping){8.0f, 0.0f, 1, true};
    p.gravity = (GravityCompensationConfig){1.0f, 0.0f, 1, true};
    assert(S4Site_ObservationReady(&p));
    assert(!S4Site_ProfileReady(&p));
    p.s3.safety.joint[0].mapping.rotor_per_joint = 0.0f;
    assert(!S4Site_ObservationReady(&p));
    p.s3.safety.joint[0].mapping.rotor_per_joint = 8.0f;
    p.gravity.sign_g = 0;
    assert(!S4Site_ObservationReady(&p));
    p.gravity.sign_g = 1;
    p.output_enabled = true;
    assert(!S4Site_ProfileReady(&p)); /* No output safety/stop confirmations. */

    /* Synthetic configuration exercises the gate only. These pins and limits
     * are not installed or approved in the production site. */
    p.s3.safety = TestS3_ConfirmedConfig();
    p.s3.safety.enabled_axes = SAFETY_SHOULDER_AXES;
    p.s3.safety.stop_policy = SAFETY_STOP_EXTERNAL_INPUT;
    p.s3.stop_input_confirmed = true;
    p.s3.operator_stop_input = (BoardStopInputConfig){BOARD_STOP_PORT_A, 1U, true};
    p.s3.independent_stop_input = (BoardStopInputConfig){BOARD_STOP_PORT_B, 1U, true};
    p.s3.prepare_timeout_ms = 100U;
    p.s3.shoulder_property_window_ms = 2000U;
    p.s3.shoulder_property_reply_ms = 5U;
    p.s3.shoulder_standstill = (ShoulderStandstillConfig){0.001f, 0.02f, 20U, 80U};
    uint32_t tail_min_ms = 2U * GDS_PROPERTY_COUNT * p.s3.shoulder_property_reply_ms
        + p.s3.shoulder_property_period_ms + p.s3.safety.service_limit_ms
        + p.s3.shoulder_standstill.dwell_ms;
    p.s3.shoulder_stop_window_ms = tail_min_ms;
    p.assist_gain = 0.5f;
    p.release_slew_nm_s = 2.0f;
    p.send_interval_ms = 10U;
    p.session_ms = 1000U;
    p.stop_submit_budget_ms = 20U;
    p.ramp_max_ms = 540U; /* 50 steps x 10 ms x 2 Nm/s = 1 Nm. */
    assert(!S4Site_ProfileReady(&p)); /* No room for a complete post-IDLE cycle. */
    p.s3.shoulder_stop_window_ms = tail_min_ms + 1U;
    assert(!S4Site_ProfileReady(&p)); /* External policy is not the S4 FOLLOW policy. */
    p.s3.safety.stop_policy = SAFETY_STOP_SUPERVISED_SHOULDER_FOLLOW;
    assert(S4Site_ProfileReady(&p));
    /* The zero frame can remain pending for almost a full service deadline;
     * a shorter submit budget must not be called a normal-stop profile. */
    p.stop_submit_budget_ms = p.s3.safety.service_limit_ms - 1U;
    assert(!S4Site_ProfileReady(&p));
    p.stop_submit_budget_ms = p.s3.safety.service_limit_ms;
    assert(S4Site_ProfileReady(&p));
    /* Model peak is 1 Nm x 0.5 / 8 = 0.0625 Nm at the rotor. The release
     * budget still fits both limits; only the model-versus-limit gate varies. */
    p.s3.safety.joint[0].max_motor_command = 0.06f;
    assert(!S4Site_ProfileReady(&p));
    p.s3.safety.joint[0].max_motor_command = 0.07f;
    assert(S4Site_ProfileReady(&p));
    p.s3.safety.joint[0].max_motor_command = 1.0f;
    p.session_zero_on_prepare = true;
    p.s3.rotor_reference_confirmed[0] = false;
    p.s3.safety.joint[0].mapping.rotor_zero_rad = 0.0f;
    assert(!S4Site_SessionZeroReady(&p)); /* No two-sample minimum configured. */
    p.session_zero_dwell_ms = p.s3.safety.feedback_timeout_ms;
    assert(!S4Site_SessionZeroReady(&p)); /* Second full sample cannot be fresh. */
    p.session_zero_dwell_ms = 10U;
    assert(!S4Site_ProfileReady(&p) && S4Site_SessionZeroReady(&p));
    p.gravity.gravity_zero_rad = 0.1f;
    assert(!S4Site_SessionZeroReady(&p));
    p.gravity.gravity_zero_rad = 0.0f;
    p.s3.safety.joint[0].limits_confirmed = false;
    assert(!S4Site_SessionZeroReady(&p));
    p.s3.safety.joint[0].limits_confirmed = true;
    p.output_enabled = false;
    assert(!S4Site_SessionZeroReady(&p));
    p.output_enabled = true;
    assert(S4Site_SessionZeroReady(&p));
    p.s3.rotor_reference_confirmed[0] = true;
    p.session_zero_on_prepare = false;
    p.s3.shoulder_standstill = (ShoulderStandstillConfig){0};
    assert(!S4Site_ProfileReady(&p));
    p.s3.shoulder_standstill = (ShoulderStandstillConfig){0.001f, 0.02f, 20U, 80U};
    p.ramp_max_ms = 539U;
    assert(!S4Site_ProfileReady(&p));
    p.ramp_max_ms = 540U;
    p.s3.safety.stop_policy = SAFETY_STOP_SUPERVISED_SHOULDER_BENCH;
    assert(!S4Site_ProfileReady(&p)); /* S3 single-pulse policy is not S4. */
}
