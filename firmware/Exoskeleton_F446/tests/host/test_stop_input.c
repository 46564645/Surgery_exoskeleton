/* One decision-critical scenario, using the real L1 implementation and fake HAL. */
#include "test_stop_input.h"
#include "s3_stop_input.h"
#include "stop_input_hal.h"
#include <assert.h>
#include <stddef.h>

static void unavailable(S3StopObservation observation)
{
    assert(!observation.available && !observation.operator_stop_clear
           && !observation.independently_stopped);
}

void TestStopInput_Run(void)
{
    BoardStopInputConfig none = {0};
    bool active = true;
    TestStopInputMock_Reset();
    assert(!BoardStopInput_ConfigValid(&none));
    assert(!BoardStopInput_Read(&none, &active) && !active);
    assert(!BoardStopInput_Read(NULL, &active));
    unavailable(S3StopInput_Read(&none, &none));
    BoardStopInputConfig invalid = { (BoardStopPort)99, 1U, true };
    assert(!BoardStopInput_Read(&invalid, &active));
    invalid.port = BOARD_STOP_PORT_A; invalid.pin_mask = 0U;
    assert(!BoardStopInput_Read(&invalid, &active));
    invalid.pin_mask = 3U;
    assert(!BoardStopInput_Read(&invalid, &active));
    assert(TestStopInputMock_ClockChecks() == 0U);
    assert(TestStopInputMock_PortLookups() == 0U);
    assert(TestStopInputMock_Reads() == 0U);

    BoardStopInputConfig operator_input = { BOARD_STOP_PORT_A, 1U << 2U, false };
    BoardStopInputConfig independent_input = { BOARD_STOP_PORT_B, 1U << 15U, true };
    assert(S3StopInput_ConfigReady(&operator_input, &independent_input));
    assert(!S3StopInput_ConfigReady(&operator_input, &operator_input));
    unavailable(S3StopInput_Read(&operator_input, &operator_input));
    assert(!BoardStopInput_Read(&operator_input, NULL));
    assert(TestStopInputMock_ClockChecks() == 0U);
    assert(!BoardStopInput_Read(&operator_input, &active) && !active);
    assert(TestStopInputMock_PortLookups() == 0U);
    assert(TestStopInputMock_Reads() == 0U);

    TestStopInputMock_SetClock(0U, true);
    GPIO_TypeDef *operator_gpio = TestStopInputMock_GPIO(0U);
    for (unsigned mode = 1U; mode <= 3U; mode++) {
        operator_gpio->MODER = mode << 4U;
        active = true;
        assert(!BoardStopInput_Read(&operator_input, &active) && !active);
    }
    assert(TestStopInputMock_Reads() == 0U);
    operator_gpio->MODER = 0U;
    operator_gpio->IDR = operator_input.pin_mask;
    assert(BoardStopInput_Read(&operator_input, &active) && !active);
    operator_input.active_high = true;
    assert(BoardStopInput_Read(&operator_input, &active) && active);
    operator_input.active_high = false;
    operator_gpio->IDR = 0U;
    assert(BoardStopInput_Read(&operator_input, &active) && active);

    /* Same port, different pin is structurally independent; same pin is not. */
    BoardStopInputConfig other = { BOARD_STOP_PORT_A, 1U << 3U, true };
    assert(S3StopInput_ConfigReady(&operator_input, &other));
    TestStopInputMock_SetClock(1U, true);
    GPIO_TypeDef *independent_gpio = TestStopInputMock_GPIO(1U);
    for (unsigned stopped = 0U; stopped < 2U; stopped++) {
        for (unsigned clear = 0U; clear < 2U; clear++) {
            operator_gpio->IDR = clear != 0U ? operator_input.pin_mask : 0U;
            independent_gpio->IDR = stopped != 0U ? independent_input.pin_mask : 0U;
            S3StopObservation observation = S3StopInput_Read(&operator_input, &independent_input);
            assert(observation.available);
            assert(observation.operator_stop_clear == (clear != 0U));
            assert(observation.independently_stopped == (stopped != 0U));
        }
    }
    /* A successful old read cannot survive a current missing or wrong-mode pin. */
    TestStopInputMock_SetClock(1U, false);
    unavailable(S3StopInput_Read(&operator_input, &independent_input));
    TestStopInputMock_SetClock(1U, true);
    for (unsigned mode = 1U; mode <= 3U; mode++) {
        independent_gpio->MODER = mode << 30U;
        unavailable(S3StopInput_Read(&operator_input, &independent_input));
    }
    independent_gpio->MODER = 0U;
    operator_gpio->IDR = operator_input.pin_mask;
    independent_gpio->IDR = independent_input.pin_mask;
    S3StopObservation observation = S3StopInput_Read(&operator_input, &independent_input);
    assert(observation.available && observation.operator_stop_clear && observation.independently_stopped);
}
