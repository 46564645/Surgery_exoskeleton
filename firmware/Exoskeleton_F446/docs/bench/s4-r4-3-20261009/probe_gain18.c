/* Host-only checks for a doubled, still 20 mNm-capped trial gain. */
#include "s4_site.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

int main(void)
{
    const S4SiteProfile *site = S4Site_GetProfile();
    assert(site != NULL && S4Site_ReadReady(site));
    assert(site->s3.safety.enabled_axes == SAFETY_SHOULDER_AXES);
    assert(site->s3.safety.joint[0].max_motor_command == 0.020f);
    assert(site->s3.safety.joint[0].lower_rad > 0.349f);
    assert(site->s3.safety.joint[0].upper_rad < 0.524f);
    assert(site->s3.safety.joint[0].max_speed_rad_s > 0.26179f);
    assert(site->s3.safety.joint[0].max_speed_rad_s < 0.26181f);
    assert(site->session_ms == 10000U && site->ramp_max_ms == 1000U);
    assert(S4Site_SessionZeroReady(site));

    S4SiteProfile p = *site;
    p.s3.rotor_reference_confirmed[0] = true; /* Simulate valid RAM session zero. */
    p.assist_gain = 0.18f;
    if (!S4Site_ProfileReady(&p)) {
        fputs("gain18_trial_gate=REJECTED_BY_PROFILE\n", stderr);
        return 2;
    }
    double rotor_at_25_nm = (double)p.gravity.amplitude_nm * (double)p.assist_gain
        * sin(25.0 * 3.14159265358979323846 / 180.0)
        / (double)p.s3.safety.joint[0].mapping.rotor_per_joint;
    double rotor_at_30_nm = (double)p.gravity.amplitude_nm * (double)p.assist_gain
        * sin((double)p.s3.safety.joint[0].upper_rad - (double)p.gravity.gravity_zero_rad)
        / (double)p.s3.safety.joint[0].mapping.rotor_per_joint;
    assert(rotor_at_25_nm > 0.015f && rotor_at_25_nm < 0.016f);
    assert(rotor_at_30_nm < 0.020f);

    p.assist_gain = 0.20f; /* 30 deg would require more than 20 mNm. */
    assert(!S4Site_ProfileReady(&p));
    p.assist_gain = 0.18f;
    p.s3.safety.joint[0].upper_rad = 1.57079633f; /* Includes sin peak. */
    assert(!S4Site_ProfileReady(&p));
    p.s3.safety.joint[0].upper_rad = site->s3.safety.joint[0].upper_rad;
    p.gravity.gravity_zero_rad = -1.13446401f; /* Relative 85-95 deg spans peak. */
    assert(!S4Site_ProfileReady(&p));

    printf("gain18_trial_gate=PASS range_peak_mNm=%.3f rotor_25_mNm=%.3f "
           "command_cap_mNm=20.000\n", rotor_at_30_nm * 1000.0,
           rotor_at_25_nm * 1000.0);
    return 0;
}
