#ifndef CONSOLE_COMMANDS_H
#define CONSOLE_COMMANDS_H
#include <stdint.h>
/* L4: main-loop-only, read-only S1 command interface. */
void ConsoleCommands_Init(void);
void ConsoleCommands_Poll(uint32_t now_ms, uint32_t scheduler_overruns);
#endif
