/* Host-only gate check for the isolated 0.36 gain / 40 mNm trial candidate. */
#include "s4_site.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

int main(void)
{
    const S4SiteProfile *site = S4Site_GetProfile();
    assert(site != NULL && S4Site_ReadReady(site));
    assert(!S4Site_ProfileReady(site)); /* The rotor origin is RAM-only. */
    assert(S4Site_SessionZeroReady(site));
    assert(site->s3.safety.enabled_axes == SAFETY_SHOULDER_AXES);
    assert(fabsf(site->s3.safety.joint[0].lower_rad - 0.34906585f) < 0.000001f);
    assert(fabsf(site->s3.safety.joint[0].upper_rad - 0.52359878f) < 0.000001f);
    assert(fabsf(site->s3.safety.joint[0].max_speed_rad_s - 0.26179939f) < 0.000001f);
    assert(site->session_ms == 10000U && site->ramp_max_ms == 1000U);
    assert(site->assist_gain == 0.36f);
    assert(site->s3.safety.joint[0].max_motor_command == 0.040f);
    assert(site->release_slew_nm_s == 0.080f);

    S4SiteProfile p = *site;
    p.s3.rotor_reference_confirmed[0] = true; /* Simulate a valid session zero. */
    p.s3.safety.joint[0].mapping.rotor_zero_rad = 1.234f;
    assert(S4Site_ProfileReady(&p));

    double rotor_at_26_6_nm = (double)p.gravity.amplitude_nm * (double)p.assist_gain
        * sin(26.6 * 3.14159265358979323846 / 180.0)
        / (double)p.s3.safety.joint[0].mapping.rotor_per_joint;
    double rotor_at_30_nm = (double)p.gravity.amplitude_nm * (double)p.assist_gain
        * sin((double)p.s3.safety.joint[0].upper_rad - (double)p.gravity.gravity_zero_rad)
        / (double)p.s3.safety.joint[0].mapping.rotor_per_joint;
    assert(rotor_at_26_6_nm > 0.0325 && rotor_at_26_6_nm < 0.0335);
    assert(rotor_at_30_nm > 0.0368 && rotor_at_30_nm < 0.0370);

    p.s3.safety.joint[0].max_motor_command = 0.020f;
    assert(!S4Site_ProfileReady(&p)); /* 20 mNm cannot cover this gain. */
    p.s3.safety.joint[0].max_motor_command = site->s3.safety.joint[0].max_motor_command;
    p.release_slew_nm_s = 0.040f;
    assert(!S4Site_ProfileReady(&p)); /* Old release budget cannot remove 40 mNm. */

    printf("gain36_trial_gate=PASS rotor_26_6_mNm=%.3f rotor_30_mNm=%.3f "
           "command_cap_mNm=40.000 old_cap_rejected=1 old_release_rejected=1\n",
           rotor_at_26_6_nm * 1000.0, rotor_at_30_nm * 1000.0);
    return 0;
}
