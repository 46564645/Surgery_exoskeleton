# S1 候选方案比较

日期：2026-09-25；阶段：INNOVATE；trace_id：T-s1-baseline-research。
本文件是候选与推荐，不是已批准实施清单；不包含最终引脚/DMA/IRQ/缓冲长度/周期配置。研究证据见 research.md，开发进度只记总项目 development_schedule.md。

## 固定边界

沿用 STM32F446RE、HAL/CMSIS、裸机、双经典CAN、项目四层架构和统一输出许可；不重新选择RTOS或控制平台。S1解决开发板启动、串口状态/命令、调度及最小状态组织，不实现电机运动。先前构建成功和无探针结果是RESEARCH时的检查结果，本轮没有重测硬件。

## 候选比较

| 议题 | 候选与代价 | 推荐方向（待PLAN落实） |
|---|---|---|
| 串口接收 | 逐字节中断较易起步，但每字节打断CPU，且偏离现有UART/DMA蓝图；循环DMA减少逐字节搬运，需处理回绕、连续流、错误与溢出 | 保留USART2/VCP设计；HAL循环DMA配合IDLE及半满/满事件，有界搬运，主循环解析。不能只靠IDLE发现连续数据，也不能把IDLE当命令结束符 |
| 串口发送 | 中断发送不占TX DMA但仍有逐字节CPU成本；DMA发送更适合后续周期遥测，但需要稳定缓冲、忙状态与完成管理 | 优先DMA发送加固定容量缓冲；满队列可丢弃/合并低优先级遥测并计数，命令回复需明确失败或背压，不能静默丢弃后伪装执行成功 |
| 缓冲实现 | 自建环形缓冲需要自行验证并发与边界；LwRB已有静态存储和读写接口，但仍需限定调用上下文 | 优先评估LwRB复用。DMA接收数组和软件环形缓冲职责不同；不得让DMA/UART两个可互相抢占的回调成为无保护多生产者 |
| 调度时基 | HAL毫秒时基不增加定时器，适合S1状态/通信；专用定时器能提供更细时刻基准，但增加资源和测量工作 | S1优先HAL_GetTick驱动固定周期任务；每项任务有界、无阻塞等待。时间差需处理计数回绕；超期需记录并限定补跑，避免积压。后续控制频率根据预算和实测确定，不能拿S1作为实时性证明 |
| 命令解析 | Arduino原实现依赖String、阻塞读和动态对象，不适合直接移植；CLIP有静态命令描述和参数解析；自写解析器需遵守自主实现前调查与确认规则 | 优先CLIP适配，保留ExoKit命令语义；上游固定长度收行、超长整行拒绝，调用前保证NUL终止；回调检查确切参数个数和范围，未知/未实现命令明确拒绝。S1不一次接入全部动作 |
| 系统状态机 | StateSmith生成C可追溯状态图与转移，但增加生成工具依赖；参考其模式手写小状态机直观，却增加自有转移逻辑及验证负担 | 优先核实StateSmith可复现生成的小型C状态机；先描述启动检查、安全关闭与故障。无驱动反馈的S1不能进入真实FOLLOW或授予电机输出许可。若生成路线不适用，先继续公开参考调查，再讨论自主实现 |
| 下载路径 | 本机CubeProgrammer已可执行；OpenOCD适合后续GDB调试但当前PATH未发现，配置也未验证 | S1优先用现有CubeProgrammer验证下载；断点调试需要时再核实OpenOCD或ST调试服务。烧录、复位、串口观察放到用户授权的实现/验证阶段 |
| 框架架构 | 强行套通用六层会改变项目已有四层职责；只保留模板不映射又可能产生相互矛盾的门禁 | 保留项目四层，后续把通用约束映射到实际目录/职责；不为满足层编号或main行数规则手改生成库，不通过关闭检查掩盖冲突 |

## 本轮补充核查

- 本工程尚未生成/复制HAL UART源文件。本机 `STM32Cube_FW_F4_V1.28.3/Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_uart.c` 实际包含 `HAL_UARTEx_ReceiveToIdle_DMA`、`HAL_UART_Transmit_DMA` 和 `HAL_UART_Transmit_IT`；已读ReceiveToIdle_DMA实现，确认有忙/错误返回，不能忽略状态。未向工程复制文件。
- 已读 `Open-source projects/clip/src/clip_cmd_parse.c`、`clip_cmd_call.c`、`clip_utils_arg.c`、`clip_config.h`。解析会原地修改以NUL结尾的行，参数数组有上限；不能据此宣称自动覆盖超长行、额外参数和所有非法输入。帮助文本格式化辅助函数还需独立边界审查，本轮不认定整个库已通过安全验证。
- 已读 `StateSmith/src/test-misc/examples/Blinky1Printf/CodeGen/CodeGen.csproj` 和README：该本地旧例使用net6.0及StateSmith 0.9.4-alpha-fix-1。这里只证明示例工具依赖存在，不决定本项目必须安装旧.NET或采用该版本；生成工具版本与本机可用性留待PLAN前核实。
- 联网核对作者仓库说明：[MaJerle UART DMA](https://github.com/MaJerle/stm32-usart-uart-dma-rx-tx)、[StateSmith](https://github.com/StateSmith/StateSmith)。未更新本地参考库；实际适配仍以本地源码和本工程库版本为依据。

## 进入PLAN之前需要收敛的内容

推荐组合：HAL + UART RX/TX DMA + 有界静态缓冲（优先LwRB）+ CLIP命令适配 + HAL毫秒时基裸机任务 + StateSmith小型状态机候选，沿用四层架构。

上述是选型方向，尚未批准实施。PLAN需要把资源映射、时钟/波特率、缓冲预算、超时/错误路径、状态转移和验证标准具体化，并确认StateSmith生成链及CLIP边界可用；不能带着这些未知项直接编写驱动。硬件接线实物验证可独立安排，不阻塞当前方案比较。

本轮没有修改固件、ioc、硬件资源锁或共享spec；没有编译、烧录或硬件测试。任务停在INNOVATE，等待用户启动PLAN。
