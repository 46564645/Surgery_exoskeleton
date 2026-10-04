#include "stop_input_hal.h"
#include <assert.h>
#include <string.h>

static GPIO_TypeDef ports[8];
static bool clocks[8];
static unsigned clock_checks, port_lookups, reads;

void TestStopInputMock_Reset(void)
{
    memset(ports, 0, sizeof(ports));
    memset(clocks, 0, sizeof(clocks));
    clock_checks = port_lookups = reads = 0U;
}
GPIO_TypeDef *TestStopInputMock_GPIO(unsigned index)
{
    assert(index < 8U); return &ports[index];
}
void TestStopInputMock_SetClock(unsigned index, bool enabled)
{
    assert(index < 8U); clocks[index] = enabled;
}
GPIO_TypeDef *StopInputMock_Port(unsigned index)
{
    assert(index < 8U); port_lookups++; return &ports[index];
}
bool StopInputMock_ClockEnabled(unsigned index)
{
    assert(index < 8U); clock_checks++; return clocks[index];
}
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *gpio, uint16_t pin)
{
    assert(gpio != NULL && pin != 0U && (pin & (uint16_t)(pin - 1U)) == 0U);
    reads++;
    return (gpio->IDR & pin) != 0U ? GPIO_PIN_SET : GPIO_PIN_RESET;
}
unsigned TestStopInputMock_ClockChecks(void) { return clock_checks; }
unsigned TestStopInputMock_PortLookups(void) { return port_lookups; }
unsigned TestStopInputMock_Reads(void) { return reads; }
