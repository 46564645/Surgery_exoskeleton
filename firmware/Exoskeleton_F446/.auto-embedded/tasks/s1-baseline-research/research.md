# S1 基线与串口链路研究证据

日期：2026-09-25。阶段保持 RESEARCH；本轮桌面研究完成，不表示 S1 实现或硬件验证完成。
下列路径以总项目根目录为基准。未作最终方案选择，不包含待执行代码清单。

## 1. 当前事实

| 项目 | 证据 | 结论 |
|---|---|---|
| 工程位置 | firmware/Exoskeleton_F446/README.md、build.sh | 唯一活动固件；桌面备份不用于开发 |
| MCU/库 | Exoskeleton_F446.ioc | STM32F446RET6；CubeMX 6.18.1、CubeF4 V1.28.3 |
| 时钟 | Core/Src/main.c 的 SystemClock_Config、ioc | HSI经PLL，SYSCLK/HCLK 84MHz，APB1 42MHz、APB2 84MHz；属于配置事实，非实测 |
| 主循环 | Core/Src/main.c 的 main | BSP_LED_Toggle + HAL_Delay(500)，没有静态任务调度或状态机 |
| HAL时基 | Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal.c 的 HAL_InitTick/HAL_GetTick/HAL_IncTick；Core/Src/stm32f4xx_it.c 的 SysTick_Handler | 已有毫秒时基和递增中断，可供后续周期调度使用；不等于任务执行时间已验证 |
| UART | main.c 的 MX_GPIO_Init、Core/Inc/stm32f4xx_hal_conf.h、cmake/stm32cubemx/CMakeLists.txt | PA2/PA3配置AF7，但 HAL_UART_MODULE_ENABLED 未启用，无串口初始化/收发及对应构建集成。不能仅连接USB就期待现有固件输出状态 |
| CAN/HX711 | ioc、main.c、App目录 | 尚未实现；本轮不扩展到S2驱动研究 |
| S1状态 | Main_ project/docs/development_schedule.md | 工程迁移与构建已有成果；串口、状态机、调度、下载验证尚未完成，阶段表仍待开始 |

## 2. 本轮执行的验证

工作目录为 firmware/Exoskeleton_F446，除标明绝对路径的命令外均从此目录执行。

| 命令/检查 | 实际结果 | 证明边界 |
|---|---|---|
| ./build.sh Debug | 退出码0；22项编译/链接完成；FLASH 7340 B、RAM 1592 B；本次输出未见编译警告 | 当前LED固件可构建；不是下载或运行证明 |
| aemb doctor . | 输出 ALL OK | 框架文件及Codex配置接线检查通过，不证明本会话hook已执行 |
| STM32_Programmer_CLI --help（下列绝对路径） | 可运行，版本2.23.0；列出 -l st-link-only 为不连接目标的枚举方式 | 工具存在，不证明SWD可用 |
| STM32_Programmer_CLI -l st-link-only | No ST-Link detected! | 本轮未枚举到探针，未尝试连接、擦写或复位目标 |
| ls /dev/cu.*；ioreg -p IOUSB -l -w 0 的ST/USB名称筛选 | 串口只有Bluetooth-Incoming-Port和debug-console；未发现ST-LINK/STM32 | 当前未观察到开发板串口；不据此推断是未插线、线材还是设备问题 |
| command -v openocd | 当前PATH未找到 | 不宣称整台电脑未安装，不为本轮安装工具 |

本机已核实的程序路径：
`/Users/mzc/Library/Application Support/stm32cube/bundles/programmer/2.23.0/bin/STM32_Programmer_CLI`。

产物：`firmware/Exoskeleton_F446/build/Debug/Exoskeleton_F446.elf`。
VS Code当前只有构建任务，未见launch.json或已验证的下载任务。烧录与断点调试均未执行。

## 3. 功能索引与实际阅读的源码

以下仅为复用依据，不表示已经集成。参考均位于 Open-source projects/。

| 对应表模块 | 实际读取文件/函数 | 可借鉴部分与边界 |
|---|---|---|
| 3 工程底座 | STM32CubeF4/Projects/STM32F446ZE-Nucleo/Examples/UART/UART_Printf/Src/main.c 的 main、PUTCHAR_PROTOTYPE；同目录 stm32f4xx_hal_msp.c 的 HAL_UART_MspInit | HAL UART初始化、GPIO/外设时钟接入。该例为ZE板、180MHz、9600奇校验且阻塞发送；不复制板针脚、时钟或通信参数 |
| 3 工程底座 | LEAP-upper-limb-main/stm32/hi-level/Core/Src/main.c 的UART句柄、静态接收数组、初始化声明 | HAL工程组织可参考；多UART与该项目传感/执行器绑定不可直接迁入本项目 |
| 4 主循环与调度 | 本工程ST HAL的 HAL_InitTick/HAL_GetTick/HAL_IncTick；ExoKit-main/Arduino API/src/MotionGuidance.cpp 的 loop；com/SerialCmdReader.cpp 构造函数 | HAL提供时基；ExoKit通过TaskManager周期执行职责。本项目保留周期职责，不能直接引入Arduino/TaskManagerIO；FreeRTOS不在已定裸机路线内 |
| 6 串口DMA与缓冲 | stm32-usart-uart-dma-rx-tx/projects/usart_rx_polling_F4/src/main.c；usart_rx_idle_line_irq_F4/src/main.c 的 usart_init/usart_rx_check/usart_process_data | 循环DMA位置差、跨缓冲尾部处理、单一处理上下文可借鉴。实例为LL/USART3/PD8-PD9；IRQ版在中断路径回显且忙等，需改为有界搬运、主循环解析；不能复制它的DMA分配 |
| 6 环形缓冲 | lwrb/lwrb/src/lwrb/lwrb.c 的 lwrb_init/lwrb_write_ex/lwrb_read_ex 及原子操作宏 | 用户提供存储区、返回实际读写量、读写指针同步可借鉴；编译器原子支持、单生产者/单消费者、满缓冲处理需在集成时核实，未执行库测试 |
| 26 系统状态机 | StateSmith/src/test-misc/examples/Blinky1Printf/src/main.c、blinky1_printf_sm.c 的 ctor/start/dispatch_event、LED_OFF_do | 静态状态实例、事件分发、按时间差转移可参考；是LED示例，没有本项目STARTUP_CHECK/SAFE_OFF/FAULT和许可合同，不声称可直接复制完整状态机 |
| 27 动作生命周期 | ExoKit-main/Arduino API/lib/exokit_api/src/com/serialreactions/CmdReactionRunner.cpp 的 launchReaction/cancelReaction | 新动作先取消旧动作；原实现new/delete须替换。动作生命周期不等于系统许可/故障状态机 |
| 30 固定文本命令 | ExoKit-main/Arduino API/lib/exokit_api/src/com/SerialCmdReader.cpp 的 exec/registerCmd；clip/examples/BasicCLI/cli.c 的命令/参数注册宏 | ExoKit按换行分命令并分派，CLIP提供声明式命令结构。Arduino String/readStringUntil及动态列表不直接用；本轮只读CLIP使用示例，尚未审完解析器内部边界，不宣称CLIP已适配 |

本地已有上述候选；本轮未进入“无合适参考后自主实现”的分支，也未从零实现。若后续候选被证实不适用，按AGENTS.md继续检索公开资料并说明依据。

## 4. 资源与框架差异

| 资源 | 当前证据与状态 |
|---|---|
| PA2/PA3 USART2 | 接口登记记录板载VCP默认路径；代码已有AF7引脚配置，UART本体未配置；实际焊桥和通信未验证 |
| PB9/PB8 CAN1；PB13/PB12 CAN2 | 接口登记的设计已选，未进入本工程ioc；本轮不修改 |
| PC3 SCK；PC2 DOUT | 接口登记的设计已选，带相反方向电平转换；未进入固件，转换模块未定 |
| SysTick | 当前HAL时基已接入；不预设额外硬件定时器 |
| DMA、UART IRQ、波特率、队列长度、周期 | 尚未形成此次S1实施决策；进入INNOVATE比较后，PLAN需给出具体配置和验证标准 |

`spec/hardware/hw-lock.yaml`仍为空。本轮将资源证据列在这里，不将未选择的DMA/IRQ或未验证接线冻结为事实；这意味着硬件资源锁尚未就绪，不能声称已通过硬件合规验收。后续落实具体外设配置时，按接口登记同步机器可读表并明确设计/实测状态。

框架通用架构是L1~L6；项目蓝图是L1平台/L2设备/L3控制/L4应用。层编号不同，不能直接套用模板命名或为过门禁重构。通用实时调度参考中还有把控制环放ISR的建议，本项目明确控制计算留主循环，按项目规则执行。

框架配置文件存在且doctor通过，但本次会话没有自动注入证据；不修改全局信任或宣称嵌套hook已启用。旧P0-00任务元数据标active，研究正文为空；本轮保留旧文件，创建s1-baseline-research并由task.py设置当前指针。不将旧任务误标完成。

## 5. 研究出口及未决项

- 本轮可完成的桌面调查已完成：知道当前固件能做什么、串口为何尚不能工作、工具/设备现状及可复用源码边界。
- 尚未完成硬件资源冻结、实现方案选择、完整S1状态机/许可设计、下载与通信实测；这些不是本轮已完成事项。
- 进入INNOVATE可比较的事项：S1串口收发和缓冲实现、裸机任务与状态机组织、框架规范如何对应项目已有分层。此处不选择优劣、不承诺接口或参数。
- 进入硬件验证前需开发板可枚举并核实VCP实际连接；当前不阻塞方案讨论，不要求用户重复确认已选CAN/HX711引脚。
- 参数、接线、固件源码、ioc、共享spec未修改；没有烧录、停核、复位、电机或人体测试。
