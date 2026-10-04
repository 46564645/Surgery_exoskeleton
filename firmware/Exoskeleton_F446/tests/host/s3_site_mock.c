#include "s3_site_mock.h"
#include "s3_feedback.h"
#include <stddef.h>
#include <string.h>
static S3SiteProfile profile = {.shoulder_node = 1U, .elbow_id = 1U};
static S3SiteSample sample;
static bool available, use_real_feedback;
const S3SiteProfile *S3Site_GetProfile(void) { return &profile; }
bool S3Site_ReadSample(uint32_t now_ms, S3SiteSample *out)
{ if (use_real_feedback) { return S3Feedback_Read(&profile, now_ms, out); }
  *out = sample; return available; }
void TestS3Site_Reset(void)
{
    memset(&profile, 0, sizeof(profile)); memset(&sample, 0, sizeof(sample));
    profile.shoulder_node = profile.elbow_id = 1U; available = use_real_feedback = false;
}
void TestS3Site_Configure(const SafetyConfig *config)
{
    TestS3Site_Reset(); profile.safety = *config;
    profile.prepare_timeout_ms = 1000U; /* Synthetic host values, never copied to production. */
    profile.trusted_feedback_axes = config->enabled_axes; profile.stop_input_confirmed = true;
    profile.operator_stop_input = (BoardStopInputConfig){BOARD_STOP_PORT_C, 1U, true};
    profile.independent_stop_input = (BoardStopInputConfig){BOARD_STOP_PORT_C, 2U, true};
    for (unsigned axis = 0U; axis < 2U; axis++) {
        profile.coordinates[axis] = S3_ASSEMBLED_JOINT;
        profile.rotor_reference_confirmed[axis] = true;
    }
}
void TestS3Site_ConfigureBench(const SafetyConfig *config)
{
    TestS3Site_Configure(config);
    profile.coordinates[1] = S3_BENCH_ROTOR;
    profile.bench_sector_confirmed[1] = profile.operational_confirmed[1] = true;
    profile.stop_input_confirmed = false; /* No physical stop contact is fabricated. */
    use_real_feedback = true;
}
void TestS3Site_SetSample(const S3SiteSample *value, bool ready) { sample = *value; available = ready; }

void TestS3Site_ConfigureShoulderProperties(uint32_t reply_ms)
{
    TestS3Site_Reset();
    profile.shoulder_source = S3_SHOULDER_ENCODER_PROPERTIES;
    profile.shoulder_property_reply_ms = reply_ms;
}

void TestS3Site_ConfigureShoulderChain(const S3SiteProfile *site)
{ TestS3Site_Reset(); profile = *site; use_real_feedback = true; }
