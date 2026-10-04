#ifndef S3_CONSOLE_H
#define S3_CONSOLE_H
#include <stdbool.h>
#include <stdint.h>
/* Main-loop queries copy existing caches; no read scheduling or permission. */
bool S3Console_Command(const char *command, uint32_t now_ms);
void S3Console_QualificationField(unsigned field);
#endif
