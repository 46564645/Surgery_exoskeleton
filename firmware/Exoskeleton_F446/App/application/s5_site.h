#ifndef S5_SITE_H
#define S5_SITE_H
/* L4: one isolated site profile. Production values never grant motor output. */
#include "s3_site.h"
#include "gravity_compensation.h"
#include "s5_elbow_support.h"

typedef struct {
    S3SiteProfile s3;
    GravityCompensationConfig shoulder_gravity; /* S4 local shoulder model. */
    float shoulder_gain;
    S5ElbowSupportConfig elbow_support;         /* Local command in C620 A. */
    S5FollowEffort release_slew_per_s;          /* Nm/s shoulder, A/s elbow. */
    uint32_t send_interval_ms, session_ms, ramp_max_ms;
    uint32_t shoulder_zero_dwell_ms;            /* Dual mode only. */
    bool output_enabled;                        /* No CLI setter. */
} S5SiteProfile;

const S5SiteProfile *S5Site_GetProfile(void);
#ifdef S5_SITE_HOST_TEST
void S5Site_TestUseProfile(const S5SiteProfile *profile);
#endif
bool S5Site_ReadReady(const S5SiteProfile *profile);   /* Before RAM references. */
bool S5Site_ProfileReady(const S5SiteProfile *profile); /* After RAM references. */
#endif
