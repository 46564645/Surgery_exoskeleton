/* L4: HAL-free optional L1 binding, preserving the gateway's selected axes. */
#include "output_can_transport.h"
#include "board_can_tx.h"
#include "s3_shoulder_service.h"
#include "board_runtime.h"
#include <stddef.h>

static bool purge(void *context, uint8_t axes_mask)
{ (void)context; return BoardCanTx_PurgeOutputAxes(axes_mask); }
static bool drained(void *context, uint8_t axes_mask)
{ (void)context; return BoardCanTx_OutputAxesDrained(axes_mask); }
static bool failed(void *context, uint8_t axes_mask)
{ (void)context; return BoardCanTx_OutputAxesFailed(axes_mask); }
static bool purge_pending(void *context, uint8_t axes_mask)
{ (void)context; return BoardCanTx_OutputPurgePending(axes_mask); }
static bool pending(void *context, uint8_t axes_mask)
{ (void)context; return BoardCanTx_OutputAxesPending(axes_mask); }
static bool submit(void *context, const BoardCanFrame frames[2], uint8_t axes_mask, bool stop)
{
    (void)context;
    bool shoulder_stop = stop && (axes_mask & SAFETY_SHOULDER_AXES) != 0U;
    bool read_stopped = !shoulder_stop || S3ShoulderService_BeforeOutputStop();
    /* A failed read cancellation must not suppress the actual stop attempt. */
    bool accepted = BoardCanTx_SubmitOutputAxes(frames, axes_mask, stop);
    if (shoulder_stop) {
        S3ShoulderService_AfterOutputStop(accepted && read_stopped, BoardRuntime_NowMs());
    }
    return accepted && read_stopped;
}

OutputTransport OutputCanTransport_Create(void)
{ return (OutputTransport){purge, submit, NULL, drained, failed, purge_pending, pending}; }
