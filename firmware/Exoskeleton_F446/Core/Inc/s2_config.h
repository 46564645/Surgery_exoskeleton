#ifndef S2_CONFIG_H
#define S2_CONFIG_H
/* S2 receive-only bench budgets. Bit timing and pins are owned by the .ioc. */
#define S2_CAN_QUEUE_CAPACITY 64U
#define S2_CAN_ISR_BUDGET      3U
#define S2_CAN_POLL_BUDGET    16U
#define S2_CAN_FILTER_SPLIT  14U
/* Observed 0x201 on the isolated C620 bus, 2026-10-01. Not a joint identity. */
#define S2_C620_ID            1U
/* Bench software freshness budget, ~100 observed 1 kHz periods; verify on target. */
#define S2_C620_TIMEOUT_MS  100U
/* Read-only archived SIMPLE observation, never a compatibility confirmation.
 * Node1 is the actual USB readout; 100ms is the existing bench freshness policy. */
#define S2_GDS_NODE 1U
#define S2_GDS_OBSERVATION_TIMEOUT_MS S2_C620_TIMEOUT_MS
#endif
