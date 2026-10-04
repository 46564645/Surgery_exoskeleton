/* L4: HAL-free optional L1 binding, preserving the gateway's selected axes. */
#include "output_can_transport.h"
#include "board_can_tx.h"
#include "s3_shoulder_service.h"
#include "board_runtime.h"
#include <stddef.h>

static bool purge(void *context, uint8_t axes_mask)
{ (void)context; return BoardCanTx_PurgeOutputAxes(axes_mask); }
static bool submit(void *context, const BoardCanFrame frames[2], uint8_t axes_mask, bool stop)
{
    (void)context;
    bool read_stopped = !stop || S3ShoulderService_BeforeOutputStop();
    /* A failed read cancellation must not suppress the actual stop attempt. */
    bool accepted = BoardCanTx_SubmitOutputAxes(frames, axes_mask, stop);
    if (stop) { S3ShoulderService_AfterOutputStop(accepted && read_stopped, BoardRuntime_NowMs()); }
    return accepted && read_stopped;
}

OutputTransport OutputCanTransport_Create(void)
{ return (OutputTransport){purge, submit, NULL}; }
