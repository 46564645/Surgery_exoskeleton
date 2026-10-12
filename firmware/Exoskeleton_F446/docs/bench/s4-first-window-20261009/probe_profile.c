/* Host-only gate probe for the materialized site. No hardware access. */
#include "s4_site.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

int main(void)
{
    const S4SiteProfile *site = S4Site_GetProfile();
    assert(site != NULL);
    assert(S4Site_ReadReady(site));
    assert(!S4Site_ObservationReady(site)); /* Cold boot has no rotor origin. */
    assert(!S4Site_ProfileReady(site));
    assert(S4Site_SessionZeroReady(site));

    S4SiteProfile after_zero = *site;
    after_zero.s3.rotor_reference_confirmed[0] = true;
    after_zero.s3.safety.joint[0].mapping.rotor_zero_rad = 1.234f;
    assert(S4Site_ObservationReady(&after_zero));
    assert(S4Site_ProfileReady(&after_zero));
    assert(site->s3.safety.enabled_axes == SAFETY_SHOULDER_AXES);
    assert(site->s3.safety.joint[0].max_motor_command == 0.020f);
    assert(site->s3.safety.joint[0].lower_rad > 0.0f);
    assert(site->s3.safety.joint[0].upper_rad < 0.524f);
    assert(site->session_ms == 3000U && site->ramp_max_ms == 1000U);

    double peak_nm = (double)site->gravity.amplitude_nm * site->assist_gain
        / site->s3.safety.joint[0].mapping.rotor_per_joint;
    double tail_min_ms = 2.0 * GDS_PROPERTY_COUNT * site->s3.shoulder_property_reply_ms
        + site->s3.shoulder_property_period_ms + site->s3.safety.service_limit_ms
        + site->s3.shoulder_standstill.dwell_ms;
    double fixed_ms = 2.0 * site->send_interval_ms + site->stop_submit_budget_ms;
    unsigned steps = (site->ramp_max_ms - (unsigned)fixed_ms) / site->send_interval_ms;
    double removable_nm = steps * site->send_interval_ms * site->release_slew_nm_s / 1000.0;
    printf("read_ready=1 cold_profile_ready=0 session_zero_ready=1 after_zero_ready=1 "
           "peak_rotor_nm=%.5f cap_nm=%.3f tail_min_ms=%.0f stop_window_ms=%u "
           "ramp_steps=%u removable_nm=%.5f\n",
           peak_nm, site->s3.safety.joint[0].max_motor_command, tail_min_ms,
           site->s3.shoulder_stop_window_ms, steps, removable_nm);
    return 0;
}
