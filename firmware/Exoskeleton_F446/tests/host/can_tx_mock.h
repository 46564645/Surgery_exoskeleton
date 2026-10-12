#ifndef CAN_TX_MOCK_H
#define CAN_TX_MOCK_H
#include "can_mock.h"
void TestCanTx_Reset(void);
void TestCanTx_FailNext(BoardCanBus bus);
void TestCanTx_Busy(BoardCanBus bus, bool abort_completes);
void TestCanTx_Fail(BoardCanBus bus, bool fail_add, bool fail_abort);
unsigned TestCanTx_Sent(BoardCanBus bus);
unsigned TestCanTx_Aborts(BoardCanBus bus);
const BoardCanFrame *TestCanTx_Last(BoardCanBus bus);
void TestCanTx_Complete(BoardCanBus bus);
void TestCanTx_CompleteFailed(BoardCanBus bus);
void TestCanTx_LoseArbitration(BoardCanBus bus);
void TestCanTx_CompleteUnknown(BoardCanBus bus);
void TestCanTx_CompleteOnAdd(BoardCanBus bus);
void TestCanTx_AbortCompletes(BoardCanBus bus, bool completes_immediately);
void TestCanTx_CompleteAbort(BoardCanBus bus);
const BoardCanFrame *TestCanTx_Pending(BoardCanBus bus, unsigned slot);
#endif
