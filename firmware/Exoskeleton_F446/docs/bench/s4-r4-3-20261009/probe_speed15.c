/* Host-only check of the user's isolated 15 deg/s R4-3 trial gate. */
#include "s4_site.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    const S4SiteProfile *site = S4Site_GetProfile();
    assert(site != NULL);
    assert(S4Site_ReadReady(site));
    assert(!S4Site_ProfileReady(site));
    assert(S4Site_SessionZeroReady(site));
    assert(site->s3.safety.enabled_axes == SAFETY_SHOULDER_AXES);
    assert(site->s3.safety.joint[0].max_speed_rad_s > 0.26179f);
    assert(site->s3.safety.joint[0].max_speed_rad_s < 0.26181f);
    assert(site->s3.safety.joint[0].lower_rad > 0.349f);
    assert(site->s3.safety.joint[0].upper_rad < 0.524f);
    assert(site->s3.safety.joint[0].max_motor_command == 0.020f);
    assert(site->s3.shoulder_standstill.max_velocity_rev_s == 0.02f);
    assert(site->s3.shoulder_standstill.dwell_ms == 200U);
    assert(site->s3.safety.feedback_timeout_ms == 150U);
    assert(site->s3.shoulder_property_window_ms == 35000U);
    assert(site->session_ms == 10000U);
    assert(site->ramp_max_ms == 1000U);

    SafetyInputs in = {0};
    in.transport_healthy = true;
    in.drivers_ready = true;
    in.operator_stop_clear = true;
    in.joint[0].valid = true;
    in.joint[0].position_rad = 0.4f;
    in.joint[0].received_ms = 1000U;
    in.joint[0].velocity_rad_s = 0.2321944f; /* Attempt 6: 13.304 deg/s. */
    assert(SafetySupervisor_Check(&site->s3.safety, &in, 1000U) == SAFETY_OK);
    in.joint[0].velocity_rad_s = 0.263544f; /* 15.1 deg/s. */
    assert(SafetySupervisor_Check(&site->s3.safety, &in, 1000U) == SAFETY_SPEED);
    in.joint[0].velocity_rad_s = -0.263544f;
    assert(SafetySupervisor_Check(&site->s3.safety, &in, 1000U) == SAFETY_SPEED);
    in.joint[0].velocity_rad_s = 0.2321944f;
    in.joint[0].position_rad = 0.34f;
    assert(SafetySupervisor_Check(&site->s3.safety, &in, 1000U) == SAFETY_RANGE);
    printf("speed15_trial_gate=PASS observed_13_304_allowed=1 over_15_fault=1 "
           "range_gate_preserved=1 output_cap_nm=0.020\n");
    return 0;
}
