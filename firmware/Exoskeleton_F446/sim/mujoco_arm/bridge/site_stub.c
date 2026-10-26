/* Linker stub for the simulation bridge.
 *
 * s5_site.c references S3ShoulderService_ReadProfileReady() from
 * S5Site_ProfileReady(), which validates the live S3 reader chain. The bridge
 * never calls the readiness checks: it reads the compiled site numbers only,
 * so the real reader (CAN, console, transport) stays out of the shared
 * library. Do not use this stub for anything else. */
#include "s3_shoulder_service.h"
#include <stddef.h>

bool S3ShoulderService_ReadProfileReady(const S3SiteProfile *profile)
{
    (void)profile;
    return false;
}
