#ifndef S4_SITE_H
#define S4_SITE_H
/* L4: one site configuration. Read/model checks grant no output permission. */
#include "gravity_compensation.h"
#include "s3_site.h"

#define S4_WARNING_LEAD_MS 2000U

typedef struct {
    S3SiteProfile s3;
    GravityCompensationConfig gravity;
    float assist_gain;
    float release_slew_nm_s; /* Rotor-side Nm/s for a normal stop only. */
    uint32_t send_interval_ms, session_ms, ramp_max_ms, stop_submit_budget_ms;
    uint32_t session_zero_dwell_ms; /* Two fresh position requests; separate from stop dwell. */
    bool output_enabled; /* Build-time site decision; no CLI setter. */
    bool session_zero_on_prepare; /* Explicit prepare captures a RAM-only vertical origin. */
} S4SiteProfile;

const S4SiteProfile *S4Site_GetProfile(void);
#ifdef S4_SITE_HOST_TEST
/* Legacy S3 host cases keep their cold-start reader; the production S4 path
 * is explicitly selected by the S4 integration case. Firmware has no switch. */
void S4Site_TestUseProduction(bool enabled);
#endif
/* These inspect only configuration. Live samples and their ages are checked
 * by the reader and safety supervisor in later S4 stages. */
bool S4Site_ReadReady(const S4SiteProfile *profile);
bool S4Site_ObservationReady(const S4SiteProfile *profile);
bool S4Site_ProfileReady(const S4SiteProfile *profile);
/* All output checks except the intentionally unset rotor reference must pass.
 * This is only a cold-start transport prebind check, never an Arm permit. */
bool S4Site_SessionZeroReady(const S4SiteProfile *profile);

#endif
