#ifndef S3_SITE_H
#define S3_SITE_H
/* One site-confirmation boundary. CLI cannot set confirmations or fabricate
 * feedback. Production provider remains explicitly unavailable until verified. */
#include "output_gateway.h"
#include "c620_feedback.h"
#include "gds_feedback.h"
#include "shoulder_property_feedback.h"
#include "s3_stop_input.h"
#include "shoulder_standstill.h"
typedef enum { S3_SHOULDER_SOURCE_UNSET, S3_SHOULDER_ENCODER_PROPERTIES } S3ShoulderSource;
typedef enum { S3_COORDINATE_UNSET, S3_BENCH_ROTOR, S3_ASSEMBLED_JOINT,
               S3_BENCH_RELATIVE_ROTOR } S3CoordinateScope;
typedef struct {
    SafetyConfig safety;
    uint32_t prepare_timeout_ms;
    S3ShoulderSource shoulder_source;
    uint32_t shoulder_property_reply_ms; /* Zero: no property receiver enabled. */
    uint32_t shoulder_property_period_ms, shoulder_property_window_ms;
    BoardStopInputConfig operator_stop_input, independent_stop_input;
    ShoulderStandstillConfig shoulder_standstill;
    uint32_t shoulder_stop_window_ms; /* Bounded read-only tail, default disabled. */
    uint8_t shoulder_node, elbow_id;
    uint8_t trusted_feedback_axes;
    S3CoordinateScope coordinates[2];
    bool rotor_reference_confirmed[2], bench_sector_confirmed[2];
    bool operational_confirmed[2], stop_input_confirmed;
} S3SiteProfile;
typedef struct {
    JointState joint[SAFETY_JOINT_COUNT];
    OutputDriverStatus driver[2];
    C620Feedback elbow_rotor;
    GdsFeedback shoulder_rotor; /* Legacy periodic observation, never mapped to S3. */
    ShoulderPropertySnapshot shoulder_properties;
    uint8_t observed_axes, wire_valid_axes; /* Observation is not a permit. */
    bool operator_stop_clear, independently_stopped, feedback_stationary;
} S3SiteSample;
const S3SiteProfile *S3Site_GetProfile(void);
/* Fresh, trusted continuous joint coordinates and external stop evidence.
 * No raw modulo-angle or experimental decoder may silently satisfy this API. */
bool S3Site_ReadSample(uint32_t now_ms, S3SiteSample *out);
#endif
