/* Host-only check for the isolated 30 deg/s S4 bench candidate. */
#include "s4_site.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static float rad_s(float deg_s)
{ return deg_s * 0.01745329251994329577f; }

int main(void)
{
    const S4SiteProfile *site = S4Site_GetProfile();
    assert(site != NULL && S4Site_SessionZeroReady(site));
    assert(fabsf(site->s3.safety.joint[0].max_speed_rad_s - rad_s(30.0f)) < 0.000001f);
    assert(fabsf(site->s3.safety.joint[0].lower_rad - rad_s(20.0f)) < 0.000001f);
    assert(fabsf(site->s3.safety.joint[0].upper_rad - rad_s(30.0f)) < 0.000001f);
    assert(site->s3.safety.joint[0].max_motor_command == 0.040f);
    assert(site->assist_gain == 0.36f && site->session_ms == 10000U);
    assert(site->ramp_max_ms == 1000U && site->release_slew_nm_s == 0.080f);

    S4SiteProfile ready = *site;
    ready.s3.rotor_reference_confirmed[0] = true;
    ready.s3.safety.joint[0].mapping.rotor_zero_rad = 1.234f;
    assert(S4Site_ProfileReady(&ready));

    SafetyInputs in = {0};
    in.transport_healthy = true;
    in.drivers_ready = true;
    in.joint[0].valid = true;
    in.joint[0].received_ms = 100U;
    in.joint[0].position_rad = rad_s(25.0f);
    in.joint[0].velocity_rad_s = rad_s(29.61159f);
    assert(SafetySupervisor_Check(&ready.s3.safety, &in, 100U) == SAFETY_OK);
    in.joint[0].velocity_rad_s = rad_s(30.1f);
    assert(SafetySupervisor_Check(&ready.s3.safety, &in, 100U) == SAFETY_SPEED);
    in.joint[0].velocity_rad_s = rad_s(34.07f);
    assert(SafetySupervisor_Check(&ready.s3.safety, &in, 100U) == SAFETY_SPEED);

    in.joint[0].velocity_rad_s = 0.0f;
    in.joint[0].position_rad = rad_s(19.9f);
    assert(SafetySupervisor_Check(&ready.s3.safety, &in, 100U) == SAFETY_RANGE);
    in.joint[0].position_rad = rad_s(30.1f);
    assert(SafetySupervisor_Check(&ready.s3.safety, &in, 100U) == SAFETY_RANGE);

    puts("speed30_trial_gate=PASS 29.61159_allowed=1 30.1_and_34.07_rejected=1 range_unchanged=1");
    return 0;
}
