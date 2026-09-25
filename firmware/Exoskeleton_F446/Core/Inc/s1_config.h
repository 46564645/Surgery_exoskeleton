#ifndef S1_CONFIG_H
#define S1_CONFIG_H

/* S1 bench design values, approved 2026-09-25; not hardware-validated limits.
 * Peripheral clock/pin/DMA configuration is owned by Exoskeleton_F446.ioc.
 * IRQ values below are the runtime verification contract for that configuration.
 */
#define S1_RX_DMA_SIZE          512U
#define S1_RX_STORAGE_SIZE     1024U /* LwRB usable capacity: size - 1 */
#define S1_TX_STORAGE_SIZE     2048U
#define S1_TX_CHUNK_SIZE        256U
#define S1_REPLY_SIZE           256U
#define S1_LINE_MAX              96U
#define S1_RX_BUDGET            128U
#define S1_SERVICE_MS             1U
#define S1_LED_MS               500U
#define S1_FAULT_LED_MS         100U
#define S1_TX_TIMEOUT_MS        100U
#define S1_IRQ_COMM               5U
#define S1_IRQ_BUTTON            10U
#define S1_IRQ_TICK               0U

#endif
