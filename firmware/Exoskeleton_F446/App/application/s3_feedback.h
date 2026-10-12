#ifndef S3_FEEDBACK_H
#define S3_FEEDBACK_H
#include "s3_site.h"
#include "elbow_session_angle.h"
typedef enum { S3_RELATIVE_UNSET, S3_RELATIVE_WAITING,
               S3_RELATIVE_ACTIVE, S3_RELATIVE_RETIRED } S3RelativePhase;
typedef struct {
    S3RelativePhase phase;
    float origin_rev;
    uint32_t sequence, request_ms, reply_ms, begin_ms;
} S3RelativeAnchor;
/* Read-only S4 shoulder evidence. The last published property cycle is copied
 * even when stale, with validity carried separately. No field is a permit. */
typedef struct {
    GdsFeedback heartbeat;
    ShoulderPropertySnapshot properties;
    JointState joint;
    bool has_heartbeat, wire_valid, heartbeat_healthy, properties_fresh;
} S3ShoulderObservation;
/* Cold init only. Begin follows successful explicit Prepare reader start.
 * End retains the historical anchor and permanently retires this boot. */
void S3Feedback_Init(void);
bool S3Feedback_BeginRelative(uint32_t now_ms);
void S3Feedback_EndRelative(void);
S3RelativeAnchor S3Feedback_GetAnchor(void);
/* Explicit output-disabled C620 RAM origin for a 1:1 assembled elbow. */
bool S3Feedback_BeginElbowSession(uint32_t now_ms);
void S3Feedback_EndElbowSession(void);
ElbowSessionPhase S3Feedback_ElbowSessionPhase(void);
uint32_t S3Feedback_ElbowSessionSequence(void); /* Last mapped, accepted C620 frame. */
bool S3Feedback_ElbowSessionRotorZeroRad(float *out_zero_rad);
/* Actual main-loop snapshots. Unavailable reference/readback/stop stays false. */
bool S3Feedback_Read(const S3SiteProfile *profile, uint32_t now_ms, S3SiteSample *out);
/* Returns read-channel availability, not model or output readiness. A joint
 * sample requires a fresh complete property cycle and confirmed mapping. */
bool S3Feedback_ReadShoulderObservation(const S3SiteProfile *profile, uint32_t now_ms,
                                        S3ShoulderObservation *out);
#endif
