#ifndef CAN_MOCK_H
#define CAN_MOCK_H
#include "main.h"
#include "board_can.h"
extern CAN_HandleTypeDef hcan1, hcan2;
void TestCan_Inject(BoardCanBus bus, uint32_t id, uint32_t dlc, uint32_t ide, uint32_t rtr, uint8_t value);
void TestCan_InjectData(BoardCanBus bus, uint32_t id, uint32_t dlc,
                        uint32_t ide, uint32_t rtr, const uint8_t data[8]);
void TestCan_Interrupt(BoardCanBus bus);
void TestCan_Fail(BoardCanBus bus, const char *operation);
uint32_t TestCan_Notifications(BoardCanBus bus);
void TestCan_Run(const char *name);
void TestClock_Set(uint32_t value);
#endif
