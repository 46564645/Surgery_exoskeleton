#ifndef TEST_S3_H
#define TEST_S3_H
#include "safety_supervisor.h"
SafetyConfig TestS3_ConfirmedConfig(void); /* Synthetic host configuration only. */
void TestS3_Run(const char *scenario);
#endif
