#!/usr/bin/env python3
"""Check generated configuration and user-code integration after CubeMX generation."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
main = (root / "Core/Src/main.c").read_text()
body = main[main.index("int main(void)"):]
body = body[:body.index("void SystemClock_Config(void)")]
sequence = ["MX_DMA_Init();", "MX_USART2_UART_Init();", "MX_CAN1_Init();", "MX_CAN2_Init();", "BSP_PB_Init(BUTTON_USER, BUTTON_MODE_EXTI);",
            "BoardRuntime_FinalizeInterrupts()", "App_Init();", "App_Poll();"]
positions = [body.index(item) for item in sequence]
assert positions == sorted(positions)
assert "HAL_Delay" not in body
assert body.index("/* USER CODE BEGIN WHILE */") < positions[-3] < body.index("while (1)")
assert "huart2.Init.BaudRate = 115200;" in main
assert "huart2.Init.Parity = UART_PARITY_NONE;" in main
assert "huart2.Init.WordLength = UART_WORDLENGTH_8B;" in main
assert "huart2.Init.StopBits = UART_STOPBITS_1;" in main
assert "huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;" in main
msp = (root / "Core/Src/stm32f4xx_hal_msp.c").read_text()
assert "__HAL_RCC_USART2_CLK_ENABLE();" in msp
assert "__HAL_RCC_DMA1_CLK_ENABLE();" in main
for direction, stream, mode in [("rx", 5, "CIRCULAR"), ("tx", 6, "NORMAL")]:
    assert f"hdma_usart2_{direction}.Instance = DMA1_Stream{stream};" in msp
    assert f"hdma_usart2_{direction}.Init.Channel = DMA_CHANNEL_4;" in msp
    assert f"hdma_usart2_{direction}.Init.Mode = DMA_{mode};" in msp
irq = (root / "Core/Src/stm32f4xx_it.c").read_text()
for name, call in [("USART2", "HAL_UART_IRQHandler(&huart2);"),
                   ("DMA1_Stream5", "HAL_DMA_IRQHandler(&hdma_usart2_rx);"),
                   ("DMA1_Stream6", "HAL_DMA_IRQHandler(&hdma_usart2_tx);")]:
    assert re.search(r"void " + name + r"_IRQHandler\(void\)\s*\{[^}]*" + re.escape(call), irq)
    assert f"HAL_NVIC_SetPriority({name}_IRQn, 5, 0);" in main + msp
assert ("#define HAL_UART_MODULE_ENABLED" in (root / "Core/Inc/stm32f4xx_hal_conf.h").read_text())
assert "stm32f4xx_hal_uart.c" in (root / "cmake/stm32cubemx/CMakeLists.txt").read_text()
for p in (root / "App/application").glob("*.[ch]"):
    assert not re.search(r'#include\s+[<"](?:main\.h|stm32.*\.h)', p.read_text()), p
for bus, prescaler, pins in [(1, 6, "GPIO_PIN_8|GPIO_PIN_9"), (2, 3, "GPIO_PIN_12|GPIO_PIN_13")]:
    for field, value in {"Prescaler": str(prescaler), "Mode": "CAN_MODE_NORMAL",
                         "SyncJumpWidth": "CAN_SJW_1TQ", "TimeSeg1": "CAN_BS1_11TQ",
                         "TimeSeg2": "CAN_BS2_2TQ", "AutoBusOff": "DISABLE",
                         "AutoRetransmission": "ENABLE", "ReceiveFifoLocked": "ENABLE"}.items():
        assert f"hcan{bus}.Init.{field} = {value};" in main
    assert f"__HAL_RCC_CAN{bus}_CLK_ENABLE();" in msp
    assert f"GPIO_InitStruct.Pin = {pins};" in msp
    assert f"GPIO_InitStruct.Alternate = GPIO_AF9_CAN{bus};" in msp
    for vector in [f"CAN{bus}_RX{bus - 1}", f"CAN{bus}_SCE"]:
        assert f"HAL_NVIC_SetPriority({vector}_IRQn, 5, 0);" in msp
        assert f"HAL_NVIC_EnableIRQ({vector}_IRQn);" in msp
        assert re.search(r"void " + vector + r"_IRQHandler\(void\)\s*\{[^}]*"
                         + re.escape(f"HAL_CAN_IRQHandler(&hcan{bus});"), irq)
    section = re.search(r"/\* USER CODE BEGIN CAN" + str(bus) + r"_Init 0 \*/(.*?)/\* USER CODE END", main, re.S)[1]
    assert "BoardCan_InitBegin" in section
    section = re.search(r"/\* USER CODE BEGIN CAN" + str(bus) + r"_Init 2 \*/(.*?)/\* USER CODE END", main, re.S)[1]
    assert "BoardCan_InitEnd" in section
assert "__HAL_RCC_GPIOB_CLK_ENABLE();" in msp
assert "__HAL_RCC_CAN1_CLK_ENABLE();" in msp[msp.index("else if(hcan->Instance==CAN2)"):]
assert "#define HAL_CAN_MODULE_ENABLED" in (root / "Core/Inc/stm32f4xx_hal_conf.h").read_text()
assert "stm32f4xx_hal_can.c" in (root / "cmake/stm32cubemx/CMakeLists.txt").read_text()
assert "BoardCan_HandleInitFailure()" in main
print("PASS generated USART/DMA/dual CAN/IRQ configuration, startup order and application header boundary")
