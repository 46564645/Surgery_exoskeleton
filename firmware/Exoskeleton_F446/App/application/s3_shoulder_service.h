#ifndef S3_SHOULDER_SERVICE_H
#define S3_SHOULDER_SERVICE_H
#include "s3_site.h"
typedef enum { SHOULDER_READER_S3, SHOULDER_READER_S4 } ShoulderReaderMode;
typedef enum { S3_READER_DISABLED, S3_READER_IDLE, S3_READER_ACTIVE,
               S3_READER_STOPPED, S3_READER_FAULT, S3_READER_STOPPING,
               S3_READER_OBSERVING, S3_READER_COMPLETE,
               S3_READER_CANCELLING } S3ReaderState;
typedef enum { S3_READ_OK, S3_READ_HEALTH, S3_READ_REPLY, S3_READ_TX, S3_READ_WINDOW } S3ReadReason;
typedef struct {
    S3ReaderState state; S3ReadReason reason; uint32_t requests;
    bool stop_observed; /* Historical observation, never output permission. */
    uint32_t stop_ms, observed_ms;
} S3ReaderStatus;
bool S3ShoulderService_ProfileReady(const S3SiteProfile *profile);
/* Reader-only configuration gate; never grants output or claims live samples. */
bool S3ShoulderService_ReadProfileReady(const S3SiteProfile *profile);
void S3ShoulderService_Init(const S3SiteProfile *profile, ShoulderReaderMode mode);
bool S3ShoulderService_Start(uint32_t now_ms);
void S3ShoulderService_Poll(uint32_t now_ms);
void S3ShoulderService_Observe(const BoardCanFrame *frame, uint32_t now_ms);
/* Hard stop: cancels reads and permanently invalidates association. */
bool S3ShoulderService_Stop(void);
/* S4 read-only stop: reject new reads now, then await bounded mailbox abort. */
bool S3ShoulderService_BeginReadOnlyStop(uint32_t now_ms);
/* Transport brackets stop submission. Bench keeps existing read association;
 * an old in-flight reply is never paired with a new post-stop request. */
bool S3ShoulderService_BeforeOutputStop(void);
void S3ShoulderService_AfterOutputStop(bool accepted, uint32_t now_ms);
ShoulderStandstillStatus S3ShoulderService_GetStandstill(uint32_t now_ms);
S3ReaderStatus S3ShoulderService_Get(void);
/* Passive budget query; never restarts or extends the reader. */
uint32_t S3ShoulderService_WindowRemaining(uint32_t now_ms);
#endif
