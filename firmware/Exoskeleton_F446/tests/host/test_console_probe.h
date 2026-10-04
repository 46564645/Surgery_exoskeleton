#ifndef TEST_CONSOLE_PROBE_H
#define TEST_CONSOLE_PROBE_H
/* Query only, fixed MCU time; no simulated CAN tick or transmitter completion. */
const char *TestConsole_QueryCached(const char *command);
#endif
