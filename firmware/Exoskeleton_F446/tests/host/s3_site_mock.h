#ifndef S3_SITE_MOCK_H
#define S3_SITE_MOCK_H
#include "s3_site.h"
/* TEST ONLY: production s3_site.c is replaced in the host source list. */
void TestS3Site_Configure(const SafetyConfig *config);
void TestS3Site_SetSample(const S3SiteSample *sample, bool available);
void TestS3Site_Reset(void);
void TestS3Site_ConfigureBench(const SafetyConfig *config);
void TestS3Site_ConfigureShoulderProperties(uint32_t reply_ms);
void TestS3Site_ConfigureShoulderChain(const S3SiteProfile *site);
#endif
