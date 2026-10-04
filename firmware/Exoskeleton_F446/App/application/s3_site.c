/* Confirmed physical facts/verified provider belong here after on-site work.
 * No test constants, guessed geometry, auto-zero, fake stop input or enable. */
#include "s3_site.h"
#include "s3_feedback.h"
#include "s2_config.h"
#include <stddef.h>
#include <string.h>
static const S3SiteProfile site = {
    .shoulder_node = 1U, /* GDS USB readout: node1, FW0.6.5; compatibility pending. */
    .elbow_id = S2_C620_ID
    /* All safety confirmations, limits, durations and provider flags false/0. */
};
const S3SiteProfile *S3Site_GetProfile(void) { return &site; }
bool S3Site_ReadSample(uint32_t now_ms, S3SiteSample *out)
{
    return S3Feedback_Read(&site, now_ms, out);
}
