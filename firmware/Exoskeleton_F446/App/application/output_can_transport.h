#ifndef OUTPUT_CAN_TRANSPORT_H
#define OUTPUT_CAN_TRANSPORT_H
/* L4: optional binding factory; constructing it performs NO hardware access.
 * Purge/submit preserve the caller's explicit shoulder/elbow axes mask. */
#include "output_gateway.h"
OutputTransport OutputCanTransport_Create(void);
#endif
