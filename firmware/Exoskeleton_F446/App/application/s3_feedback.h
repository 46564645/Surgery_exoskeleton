#ifndef S3_FEEDBACK_H
#define S3_FEEDBACK_H
#include "s3_site.h"
typedef enum { S3_RELATIVE_UNSET, S3_RELATIVE_WAITING,
               S3_RELATIVE_ACTIVE, S3_RELATIVE_RETIRED } S3RelativePhase;
typedef struct {
    S3RelativePhase phase;
    float origin_rev;
    uint32_t sequence, request_ms, reply_ms, begin_ms;
} S3RelativeAnchor;
/* Cold init only. Begin follows successful explicit Prepare reader start.
 * End retains the historical anchor and permanently retires this boot. */
void S3Feedback_Init(void);
bool S3Feedback_BeginRelative(uint32_t now_ms);
void S3Feedback_EndRelative(void);
S3RelativeAnchor S3Feedback_GetAnchor(void);
/* Actual main-loop snapshots. Unavailable reference/readback/stop stays false. */
bool S3Feedback_Read(const S3SiteProfile *profile, uint32_t now_ms, S3SiteSample *out);
#endif
