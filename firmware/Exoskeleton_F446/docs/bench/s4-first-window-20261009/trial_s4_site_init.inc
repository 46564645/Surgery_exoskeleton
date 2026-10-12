/* Isolated S4 first-window profile. This declaration replaces only the
 * initializer in a copied s4_site.c; it is never included by the active build.
 * The true confirmation bits are provisional engineering assumptions needed
 * to evaluate one supervised trial candidate offline. The user has not yet
 * permitted this image or any physical flash, prepare, start or motor output.
 * They do not claim powered read/send or physical stop validation. */
static const S4SiteProfile site = {
    .s3.safety.enabled_axes = SAFETY_SHOULDER_AXES,
    .s3.safety.stop_policy = SAFETY_STOP_SUPERVISED_SHOULDER_FOLLOW,
    .s3.safety.feedback_timeout_ms = 150U,
    .s3.safety.service_limit_ms = 100U,
    .s3.safety.max_request_ms = 250U,
    .s3.safety.protection_confirmed = true,
    .s3.safety.stop_method_confirmed = true,
    .s3.safety.timing_confirmed = true,
    .s3.safety.joint[0].mapping = {
        .rotor_per_joint = 8.0f,
        .rotor_zero_rad = 0.0f, /* Filled in RAM by this session's vertical reference. */
        .direction = 1,
        .confirmed = true
    },
    .s3.safety.joint[0].lower_rad = 0.34906585f, /* +20 deg from vertical. */
    .s3.safety.joint[0].upper_rad = 0.52359878f, /* +30 deg from vertical. */
    .s3.safety.joint[0].max_speed_rad_s = 0.17453293f, /* 10 deg/s. */
    .s3.safety.joint[0].max_motor_command = 0.020f, /* Rotor-side Nm; independent cap. */
    .s3.safety.joint[0].motor_slew_per_s = 0.020f, /* Rotor-side Nm/s. */
    .s3.safety.joint[0].limits_confirmed = true,
    .s3.safety.joint[0].feedback_confirmed = true,
    .s3.safety.joint[0].protocol_confirmed = true,
    .s3.shoulder_source = S3_SHOULDER_ENCODER_PROPERTIES,
    .s3.shoulder_property_reply_ms = 20U,
    .s3.shoulder_property_period_ms = 20U,
    .s3.shoulder_property_window_ms = 30000U,
    .s3.shoulder_node = S2_GDS_NODE,
    .s3.elbow_id = S2_C620_ID,
    .s3.trusted_feedback_axes = SAFETY_SHOULDER_AXES,
    .s3.coordinates[0] = S3_ASSEMBLED_JOINT,
    .s3.rotor_reference_confirmed[0] = false, /* Set only after two fresh cycles. */
    .s3.prepare_timeout_ms = 20000U,
    .s3.shoulder_standstill = {
        .span_rev = 0.001f,
        .max_velocity_rev_s = 0.02f,
        .dwell_ms = 200U,
        .freshness_ms = 150U
    },
    .s3.shoulder_stop_window_ms = 1500U,
    .gravity = {
        .amplitude_nm = 1.64f, /* Joint-side partial-load estimate, not measured torque. */
        .gravity_zero_rad = 0.0f, /* RAM vertical reference is model phase zero. */
        .sign_g = 1,
        .confirmed = true
    },
    .assist_gain = 0.09f,
    .release_slew_nm_s = 0.040f, /* Rotor-side normal withdrawal. */
    .send_interval_ms = 50U,
    .session_ms = 3000U,
    .ramp_max_ms = 1000U,
    .stop_submit_budget_ms = 250U,
    .session_zero_dwell_ms = 20U,
    .output_enabled = true,
    .session_zero_on_prepare = true
};
