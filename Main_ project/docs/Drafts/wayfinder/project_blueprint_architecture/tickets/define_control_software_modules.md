---
title: 划分控制与嵌入式软件模块
type: grilling
status: closed
parent: ../map.md
blocked_by:
  - define_architecture_viewpoints
claimed_by: codex
---

## Question

如何以 ExoKit 的 Joint、Condition、Reaction 和 Event 为主线，划分状态采集、重力补偿、动作放大、保持、命令合成、状态管理、安全监督、通信和硬件适配模块？

## Discussion order

本票据按以下顺序自上而下确认，每轮集中处理同一层级的多个问题：

1. 总体程序架构与主要分层；
2. 层间依赖、控制请求和状态数据流；
3. 各层内部模块及其所有权；
4. 裸机调度与模块运行关系；
5. 执行器、通信和平台适配边界。

已经提前讨论的状态快照等细节保留为候选方案，待推进到对应层级时再统一确认。

## ExoKit findings so far

- ExoKit 源码使用 `SHOULDER_BACK`、`SHOULDER_SIDE`、`ELBOW` 作为软件关节类型。
- ExoKit 同时提供单关节命令和整臂命令。整臂 Amplify/Lock 会为手臂中每个主动关节生成一个关节 Reaction，并组合为并行 Reaction。
- 串口命令由单一 `CmdReactionRunner` 管理；启动新的命令 Reaction 前会取消此前正在运行的 Reaction。
- 本项目直接使用 ExoKit 语义关节类型，不再为当前系统维护数字关节别名。
- 已确认本项目的软件关节为 `RIGHT/SHOULDER_SIDE`、`RIGHT/ELBOW`，不配置 `SHOULDER_BACK`。
- 已确认执行器绑定为 `SHOULDER_SIDE -> GIM6010-8 + GDS68`、`ELBOW -> 裸 M3508 + C620`；上层使用统一关节接口，硬件差异由两个执行器适配模块承担。
- 已确认正常运行使用整臂命令，同时作用于 `SHOULDER_SIDE` 和 `ELBOW`；单关节命令仅保留给标定、调试和单独验证流程。
- 已确认正常使用只设置一个整臂总体助力等级；`SHOULDER_SIDE`和`ELBOW`根据各自独立标定参数，将总体等级换算为各自实际助力输出。
- ExoKit 的 `CmdReactionRunner` 同一时刻只保留一个命令 Reaction，新 Reaction 启动前取消旧 Reaction；Amplify 和 Lock 会保存并在退出时恢复先前电机状态。ExoKit 没有独立的持续重力补偿层，因此本项目不能原样继承这一输出生命周期。
- ExoKit 同时提供两种动作入口：固定串口命令直接创建并运行已知 Reaction；Arduino API 可通过 `Condition + Reaction + Event + ActionBuilder` 组合触发、取消、循环、顺序和并行动作。

## Confirmed overall software architecture

- 采用分层架构作为程序主体，ExoKit 的 `Condition`、`Event`、`Reaction`只作为领域与控制层内部的事件驱动动作机制，不作为跨越全部层级的系统总线。
- 顶层领域对象保留为 `ExoskeletonSystem -> RightArm -> SHOULDER_SIDE / ELBOW`。`ExoskeletonSystem`是单右臂系统的软件根对象，不预留左臂或双臂运行路径。
- 主体采用四层，而不是五层：STM32 平台层、设备与通信驱动层、领域与控制层、系统应用层。领域对象和控制动作合并在同一层，但后续仍须明确对象所有权，避免 Joint 同时承担驱动、算法、状态机和安全职责。
- Reaction 只产生动作或增量助力请求；命令合成器组合持续重力补偿、Amplify 和 Lock 请求；独立安全门拥有最终输出许可，Reaction 不得直接操作执行器。
- 层间采用严格单向依赖。上层只通过接口使用下层能力；下层只发布状态或实现接口，不得反向调用控制动作或系统应用逻辑，也不得用全局可写变量绕过边界。
- 对应索引模块：1. 系统对象与功能术语；3. STM32F446 工程底座；7. 统一关节执行器接口；21. 助力命令合成；26. 系统状态机；27. 动作管理器；28. 安全监督与故障管理。
- 总体架构相对 ExoKit 预计改写量为 40-55%。主要保留对象语义和动作生命周期；主要重写硬件依赖、最终输出所有权、静态对象组织和层间接口。

## Confirmed four-layer ownership

- 系统应用层负责启动、校准流程、系统状态、动作管理、裸机任务调度和外部命令协调；控制算法与设备协议不放入该层。
- 领域与控制层采用按关节自治的组织方式：`SHOULDER_SIDE`和`ELBOW`各自具有 Joint/JointController，拥有本关节状态、重力补偿分量、Amplify、Lock 和关节级命令合成。`RightArm`只负责整臂命令分发、双关节开始/取消和结果汇总，不把两个关节合并成一个控制器。
- Joint/JointController 不得直接依赖 GDS68、C620、CAN、Flash 或串口；其状态输入和输出提交均通过设备接口完成，因此按关节自治不改变已经确认的严格单向依赖。
- 裸机调度策略属于系统应用层；STM32 平台层只提供单调时间、定时器和中断机制。系统应用层持有静态任务表并按已确认的独立周期模块方式调度。
- 通信按层拆分：设备与通信驱动层只负责 UART 字节流、DMA 和缓冲；系统应用层负责固定文本命令解析、语义校验和动作管理器调用；遥测只读取公开状态，不直接访问驱动内部数据。
- 以上决定对应索引模块：4. 主循环与任务调度；6. 串口 DMA 与环形缓冲；7. 统一关节执行器接口；20. 动作放大式顺势助力；22. 双关节动作协调；23. 通电保持；27. 动作管理器；30. 固定文本命令解析；31. 遥测与调试界面。

## Confirmed safety ownership from the index

- 直接采用索引模块 28 的架构结论：系统应用层设置独立 `SafetySupervisor`，其控制权高于所有 Condition、Reaction、重力补偿、Lock 和外部命令。
- Reaction 和 JointController 只能提交候选命令；最终输出必须经过不可绕过的安全门。驱动器自身限幅和保护继续保留，但只作为底层保护，不能替代系统级安全监督。
- 直接采用索引模块 29 的健康监测边界：看门狗、任务周期健康和设备在线/超时状态独立于 Reaction 管理，任何单个动作对象都不得自行决定是否喂狗。
- 模块 28 主参考为 BetaRobot 与 StateSmith，思路借鉴 GALAXEA-A1Z，改写量为 60-75%；模块 29 主参考为 STM32CubeF4 与 BetaRobot，改写量为 20-35%。具体故障条件和故障后输出行为仍由“确定运行状态与安全控制权”票据细化，不再重新选择是否需要独立监督层。

## Secondary reference findings

- 《开源项目借鉴索引表》将连续重力补偿、助力命令合成和通电保持分别指向 GALAXEA-A1Z，并要求重力补偿作为所有 ExoKit 动作之下的持续基础层。
- GALAXEA-A1Z 的周期控制先读取反馈并执行安全检查，再计算动力学项，将额外前馈项与重力项相加，统一执行方向换算和逐关节限幅后发送。
- GALAXEA-A1Z 的位置保持由位置/速度控制项与重力补偿共同构成，因此本项目的 Lock 不能替代或关闭重力补偿基线。
- BetaRobot 的 daemon 提供模块在线/超时监测和故障回调入口，但故障后的关节输出策略仍需由本项目安全监督层决定。
- StateSmith 只提供状态机表达与代码生成机制，不替本项目决定状态语义或故障输出行为。

## Confirmed control composition

- 基础随动状态持续提供重力补偿。
- Amplify 在重力补偿之上增加动作放大输出。
- Lock 取消 Amplify，并使用重力补偿与当前位置保持共同形成通电保持。
- Cancel 只取消当前 Amplify 或 Lock，随后返回基础重力补偿状态；Cancel 不等同于故障停机。

## Confirmed action framework

- 采用“固定动作，内部保留生命周期”方案。
- 第一版保留 ExoKit 风格的 `Condition`、`Reaction`、`Event`接口和开始、循环、取消、结束、恢复职责。
- 第一版只预定义 Amplify、Lock、Cancel 等系统动作，不开放 ActionBuilder 式运行时自由组合。
- 动作对象和组合关系采用静态配置，不继承 ExoKit 的动态分配与 TaskManagerIO 实现。
- 对应索引模块：27. 动作管理器；预计该模块相对 ExoKit 改写量为 40-50%。

## Confirmed intent-detection ownership

- 采用接近 ExoKit 的方案：`SHOULDER_SIDE`、`ELBOW`各自的 `AmplifyReaction`读取所属关节状态，并在 Reaction 内判断运动方向、启动速度、停止和反向退出。
- 不新增独立动作意图判断模块，也不使用一个整臂意图统一触发两个关节。
- 整臂 Amplify 仍同时启动两个关节 Reaction，但每个关节根据自身反馈独立决定当前是否产生增量助力。
- 对应索引模块：19. 动作意图判断；该部分预计相对 ExoKit 改写量为 30-45%。

## Confirmed joint-state velocity source

- 采用统一关节状态提供处理后速度的方案：执行器适配层保留设备原始反馈，关节状态层统一完成单位、方向、有效性、时间戳和可选速度滤波，`AmplifyReaction`读取统一关节速度。
- `SHOULDER_SIDE`与`ELBOW`的速度滤波参数分别配置，具体滤波参数必须根据实物反馈与台架数据确定。
- 对应索引模块：10. 编码器状态与多圈累计；11. 关节角度统一换算；19. 动作意图判断。
- 模块 10 主参考按索引采用 BetaRobot 与 Rhino-Pi-X1：分别借鉴统一电机状态/多圈累计组织，以及 GIM6010-8 原始位置与速度反馈解析结构。
- 模块 11 主参考按索引采用 BetaRobot 与 ExoKit：保留统一换算结构，以及 Joint 向 Reaction 提供角度、速度和方向的关系。
- 模块 19 主参考按索引采用 ExoKit 与 CMSIS-DSP：保留运动方向、起始速度、停止和反向退出语义，并仅使用必要的基础滤波函数。
- 改写量按索引分别记录：模块 10 编码器状态与多圈累计为 30-45%；模块 11 关节角度统一换算为 35-50%；模块 19 动作意图判断为 30-45%。不再用一个合并百分比覆盖三个模块。

## Confirmed persistence scope

- 第一版只持久化 `SHOULDER_SIDE`、`ELBOW`的关节零位，不持久化方向、活动范围、助力参数、重力参数、当前动作、助力等级或保持目标。
- 除零位外的参数暂时作为固件配置管理；修改后需要重新构建固件。
- 上电不得恢复断电前的 Amplify、Lock 或其他运行状态。
- 对应索引模块：14. 参数持久化。主参考按索引为 STM32 EEPROM Emulation 与 STM32CubeF4；ExoKit 只提供关节配置语义，不作为 Flash 实现参考。
- 参数持久化模块改写量按索引为 20-35%；零位有效性和启动校准流程分别按索引模块 15、16 记录。

## Confirmed calibration behavior

- 校准与加载行为完整继承 ExoKit 的三种策略：`ASK_CREATE`询问加载还是重标定，`PREFER_LOAD`优先加载有效持久化零位、无效时转入标定，`CREATE`强制重新标定。
- 进入校准流程时，先禁止 `SHOULDER_SIDE`、`ELBOW`主动输出；随后按右臂 `SHOULDER_SIDE`和`ELBOW`逐关节提示用户摆到规定参考姿态、确认并执行零位设置。
- 持久化零位带可用标志、数据长度/版本信息和完整性校验；校验失败时不得应用。
- 保留 ExoKit 的行为语义，但受已确认持久化范围约束，只保存 `SHOULDER_SIDE`、`ELBOW`零位，不扩展保存整臂尺寸或运行参数。
- 对应索引模块：15. 编码器零位标定流程，主参考为 OpenExo 与 StateSmith；16. 启动参数检查，主参考为 StateSmith 与 STM32 EEPROM Emulation。ExoKit 的三种加载策略作为本项目已确认的行为继承，不替换索引规定的实现参考。
- 改写量按索引分别记录：模块 15 编码器零位标定流程为 55-70%；模块 16 启动参数检查为 55-65%。
- 正常上电默认采用 `PREFER_LOAD`：有效零位自动加载，无有效零位时进入人工标定；`ASK_CREATE`和`CREATE`保留为人工选择或调试入口。

## Confirmed calibration ownership

- 直接沿用 ExoKit 将校准作为系统级流程、由流程调用各 Joint `tare`能力的职责关系，但不沿用其阻塞串口等待和直接访问 EEPROM 的耦合实现。
- 系统应用层持有启动检查和校准流程；领域与控制层的 Joint/JointController 只提供禁止主动输出、读取当前状态和设置零位的领域操作；持久化适配器负责 Flash/EEPROM 仿真实现。
- 校准流程只能通过公开接口协调上述能力，不允许 Joint 依赖串口、Flash 或用户交互，也不新增独立配置层。
- 对应索引模块：14. 参数持久化；15. 编码器零位标定流程；16. 启动参数检查。行为主参考为 ExoKit `ExoskeletonCalibrator`；存储实现主参考为 STM32 EEPROM Emulation；流程结构参考 OpenExo 与 StateSmith。

## Confirmed command grammar

- 保留 ExoKit 完整文本命令格式和命令名称，包括 `ExoID`、`armSide`和语义关节类型参数。
- 本系统只接受目标为 `right`、`shoulderside`和`elbow`的有效组合；`left`和`shoulderback`返回明确的不支持错误。
- 保留 `armamplify`、`jointamplify`、`armlock`、`jointlock`和`cancel`等外部语义；只实现本项目已确认的动作集合。
- 对应索引模块：6. 串口 DMA 与环形缓冲；30. 固定文本命令解析。
- 命令语义主参考为 ExoKit；解析器实现主参考为 CLIP；串口接收主参考为 stm32-usart-uart-dma-rx-tx 与 LwRB。
- 使用静态命令表和固定缓冲，不继承 Arduino `String`、正则解析或动态内存；预计命令解析模块改写量为 15-30%。

## Confirmed scheduling model

- 采用裸机固定周期调度，不引入 RTOS，也不移植 ExoKit 使用的 TaskManagerIO。
- ExoKit 通过 Arduino `loop()`持续调用 `taskManager.runLoop()`，并用 `scheduleFixedRate()`或重复 Event 周期执行串口读取、编码器速度采样、条件检查和 Reaction 更新；本项目保留这些周期职责和非阻塞运行语义。
- 采用各模块独立周期运行：关节状态采集、安全检查、重力补偿、Reaction 更新、最终命令输出和通信服务分别登记为静态周期任务，各自按到期条件执行，不建立单一中央控制流水线。
- STM32F446RE 侧使用静态裸机任务表和单调时间基准。中断只负责更新时间、收发数据和设置就绪标志；到期任务在主循环中协作式运行，不允许阻塞等待。
- 控制与安全任务必须具有确定周期、数据时间戳和超时检查；各任务的最终周期值不在架构阶段臆定，按硬件反馈频率和后续台架测量确定。
- 对应《开源项目借鉴索引表》模块：4. 主循环与任务调度。实现主参考按索引采用 STM32CubeF4 的裸机时基与周期任务范例；索引列出的 FreeRTOS 路线因已选择裸机而不采用。ExoKit 只提供需要保留的周期条件检查、Reaction 更新和通信处理职责。
- 改写量按索引模块 4 的裸机路线记录为 30-45%。优点是职责划分接近 ExoKit、模块可采用不同更新周期且便于独立增减；缺点是必须明确共享状态的时间戳、有效性和任务先后关系，避免重力补偿、Reaction 与最终输出读取到不同时间点的数据。

## Open-source index adoption audit

以下内容直接采用《开源项目借鉴索引表》的参考路线，不再作为用户选择题。尚未展开的模块只锁定来源、复用判断和架构位置，不提前填写协议字段、控制参数或安全阈值。

| 索引模块 | 直接采用的参考与内容 | 判断与改写量 | 在四层架构中的位置 |
|---|---|---|---|
| 1. 系统对象与功能术语 | ExoKit 的 Joint、Arm、Action、Condition、Reaction 概念与生命周期 | 直接使用；0-10% | L3 领域与控制层 |
| 2. 助力与保持命令名称 | ExoKit 的 `jointamplify`、`jointlock`、`cancel`及用户可见语义 | 直接使用；0-10% | L4 命令入口映射到 L3 动作 |
| 3. STM32F446 工程底座 | STM32CubeF4 HAL/CMSIS、CubeMX 工程组织；LEAP upper limb 的 F446 工程参考 | 需要修改；25-40% | L1 STM32 平台层 |
| 4. 主循环与任务调度 | STM32CubeF4 裸机时基与周期任务方式；保留 ExoKit 周期职责，不采用已列出的 FreeRTOS 路线 | 需要修改；30-45% | L4 静态任务表，L1 提供时间机制 |
| 5. CAN 底层与报文分发 | BetaRobot `bsp_bxcan`分层与 STM32CubeF4 HAL CAN 范例 | 需要修改；30-45% | L1 CAN HAL，L2 CAN 分发 |
| 6. 串口 DMA 与环形缓冲 | `stm32-usart-uart-dma-rx-tx`接收范例和 LwRB 环形缓冲库 | 直接使用库、修改集成层；20-35% | L1 UART/DMA，L2 字节流缓冲 |
| 7. 统一关节执行器接口 | BetaRobot `drv_motor_base`分层与 ExoKit Joint 隐藏电机差异的职责 | 需要修改；35-50% | L3 Joint 接口，L2 执行器适配器实现 |
| 8. GIM6010-8 驱动适配 | Rhino-Pi-X1 的报文/反馈骨架与 BetaRobot bxCAN 适配方式；不照搬设备特定倍率 | 需要修改；40-50% | L2 `SHOULDER_SIDE`执行器适配器 |
| 9. M3508 + C620 驱动适配 | BetaRobot 与 UICRM-Embedded 的 DJI 反馈解析、组帧和电机接口组织 | 需要修改；20-35% | L2 `ELBOW`执行器适配器 |
| 10. 编码器状态与多圈累计 | BetaRobot 的 DJI 状态组织与 Rhino-Pi-X1 的 GIM 反馈解析结构 | 需要修改；30-45% | L2 原始反馈处理，结果提交 L3 JointState |
| 11. 关节角度统一换算 | BetaRobot 的统一状态结构与 ExoKit 的方向、零位和可读状态概念 | 需要修改；35-50% | L3 JointState 换算边界 |
| 12. M3508 外环控制 | BetaRobot PID 组织与 CMSIS-DSP 基础函数 | 需要修改；25-40% | L3 `ELBOW` JointController，L2 只发送可执行量 |
| 13. HX711 与力传感器采集 | STM32-Libraries HX711驱动；Auxivo只作辅助监测思路参考 | 需要修改；20-35% | L2按固定10 SPS采集，L3只读低速监测状态；不参与主控制或安全触发 |
| 14. 参数持久化 | STM32 EEPROM Emulation 核心与 STM32CubeF4 Flash 范例 | 需要修改；20-35% | L2 持久化适配器，L4 决定加载/保存流程 |
| 15. 编码器零位标定流程 | OpenExo 与 StateSmith 的流程组织，结果进入 ExoKit 风格 Joint 配置 | 自己编写、开源辅助；55-70% | L4 校准流程调用 L3 Joint `tare` |
| 16. 启动参数检查 | StateSmith 流程与 STM32 EEPROM Emulation 有效性检查 | 自己编写、开源辅助；55-65% | L4 启动检查 |
| 17. 重力模型离线推导 | 直接使用 SymPy/SymPyBotics 工具链，借鉴 GALAXEA-A1Z 动力学思路 | 工具直接用、项目模型改写；45-55% | 离线工具产出 L3 可执行公式 |
| 18. 连续重力补偿 | GALAXEA-A1Z 控制流程与 SymPy 生成公式；不搬用六轴 Python 模型 | 自己编写、开源辅助；55-70% | L3 各 JointController 的重力分量计算 |
| 19. 动作意图判断 | ExoKit `AmplifyReaction`方向、速度、停止和反向退出语义；CMSIS-DSP 基础滤波 | 需要修改；30-45% | L3 各关节 AmplifyReaction |
| 20. 动作放大式顺势助力 | ExoKit Amplify 参数与生命周期；GALAXEA-A1Z 只辅助输出叠加思路 | 自己编写、开源辅助；55-70% | L3 各关节增量助力请求 |
| 21. 助力命令合成 | GALAXEA-A1Z 的先合成、后限幅顺序与 ExoKit 动作语义 | 自己编写、开源辅助；65-80% | L3 各 JointController 的关节级合成器 |
| 22. 双关节动作协调 | ExoKit Arm 级组合与 StateSmith 状态组织 | 自己编写、开源辅助；60-75% | L3 RightArm 负责同时开始、取消和汇总 |
| 23. 通电保持 | ExoKit Lock 生命周期与 GALAXEA-A1Z 重力补偿加位置保持顺序 | 需要修改；40-50% | L3 各关节 LockReaction |
| 24. 模式平滑切换 | GALAXEA-A1Z 与 CMSIS-DSP 提供处理思路和基础函数 | 自己编写、开源辅助；55-70% | L3 输出合成后的变化率处理 |
| 25. 角度、速度与输出限制 | BetaRobot 限幅分层与 GALAXEA-A1Z 逐级检查结构 | 需要修改；30-50% | L3 关节限制加 L4 最终安全门 |
| 26. 系统状态机 | StateSmith 生成流程与 ExoKit 动作生命周期 | 需要修改；25-40% | L4 系统状态机 |
| 27. 动作管理器 | ExoKit `IReaction`、Event、`CmdReactionRunner`生命周期与 StateSmith | 需要修改；40-50% | L4 动作所有权，L3 执行动作逻辑 |
| 28. 安全监督与故障管理 | BetaRobot 在线/超时组织与 StateSmith 故障状态；GALAXEA-A1Z 只作处理顺序参考 | 自己编写、开源辅助；60-75% | L4 独立 SafetySupervisor 和最终安全门 |
| 29. 看门狗与周期健康监测 | STM32CubeF4 IWDG 范例与 BetaRobot daemon | 需要修改；20-35% | L1 看门狗机制，L4 决定健康条件 |
| 30. 固定文本命令解析 | 直接采用 CLIP 的静态注册/解析结构，保留 ExoKit 命令语义 | 需要修改；15-30% | L4 命令解析与分发 |
| 31. 遥测与调试界面 | 直接使用 Serial Studio 作只读监看，沿用 ExoKit Joint/Arm/Action 分组 | 直接使用上位机、修改映射；5-20% | 固件 L4 遥测输出，独立上位机显示 |
| 32. 算法单元验证 | Unity 断言与 Ceedling 构建/mock 流程 | 需要修改；40-50% | 独立测试工程，覆盖 L3/L4 纯逻辑 |

索引已经确定上述参考路线，因此后续不再询问“使用哪个开源项目”。后续只在参考项目无法决定本项目功能语义、接口所有权、硬件实测参数或验证条件时请求用户确认。

## Reference traceability

| 当前决定 | 索引表对应模块 | 主参考 | 已核对位置 | 借鉴内容 | 与 ExoKit 的关系 |
|---|---|---|---|---|---|
| 持续重力补偿作为基础层 | 18. 连续重力补偿 | GALAXEA-A1Z | `a1z/robots/arm_robot.py::_update()` | 每周期读取编码器状态、计算动力学项并形成逐关节重力输出 | ExoKit 未提供该层；新增为所有正常动作之下的常驻服务 |
| 动作放大叠加到基础输出 | 20. 动作放大式顺势助力；21. 助力命令合成 | ExoKit；GALAXEA-A1Z | `AmplifyReaction.cpp`；`arm_robot.py::_update()` | 保留方向/速度触发和 Reaction 生命周期；借鉴额外前馈与重力项先合成、后限幅的顺序 | 保留 Amplify 语义，替换其直接操作 Dynamixel 的输出方式 |
| 通电保持保留重力补偿 | 23. 通电保持 | ExoKit；GALAXEA-A1Z | `JointLockReaction.cpp`；`examples/position_hold.py`；`arm_robot.py::_update()` | 进入时取当前位置，保持期间组合位置保持项与重力项，退出时恢复基础状态 | 保留 Lock 的进入/退出语义，修改为不关闭重力补偿基线 |
| 新动作取消旧动作，Cancel 恢复基础状态 | 27. 动作管理器 | ExoKit；StateSmith | `CmdReactionRunner.cpp`；`Event.cpp` | 保留开始、运行、取消、清理和恢复职责；状态机实现方式后续决定 | 保留动作生命周期，但取消后恢复本项目的持续重力补偿状态 |
| 系统状态与动作生命周期分离 | 26. 系统状态机 | StateSmith；ExoKit | StateSmith 状态图生成流程；ExoKit Reaction 生命周期 | 用明确系统状态承载动作结果，禁止动作对象直接改写系统安全状态 | 保留 ExoKit 动作生命周期，由独立系统状态机约束其可运行状态 |
| 故障拥有高于动作的控制权 | 28. 安全监督与故障管理 | BetaRobot；StateSmith | `drv/drv_daemon/drv_daemon.c`；StateSmith 状态图生成流程 | 借鉴模块在线/超时监测和故障状态组织；详细故障输出行为交由安全票据细化 | ExoKit Reaction 只能提交或取消动作，不能决定故障状态与最终输出 |
| 编码器状态与多圈累计 | 10. 编码器状态与多圈累计 | BetaRobot；Rhino-Pi-X1 | `drv/drv_motor/drv_motor_base.h`；`gim6010_drv.c/.h` | 统一保存位置、速度、时间戳、多圈状态和原始 GIM 反馈 | 替代 ExoKit 独立编码器实现，继续向 Joint 提供状态 |
| 关节角度统一换算 | 11. 关节角度统一换算 | BetaRobot；ExoKit | `drv_motor_base.h`；`ConfiguredEncoder.cpp` | 统一处理单位、方向、零位、有效性和时间戳 | 保留 ExoKit Joint 对上层提供统一角度和速度的关系 |
| 动作意图判断 | 19. 动作意图判断 | ExoKit；CMSIS-DSP | `AmplifyReaction.cpp`；CMSIS-DSP 基础滤波函数 | 使用处理后速度判断方向、启动、停止和反向退出 | 直接继承 Amplify 行为语义，不引入 IMU |
| 裸机独立周期模块调度 | 4. 主循环与任务调度 | STM32CubeF4 | STM32CubeF4 HAL 时基/裸机周期范例；`MotionGuidance.cpp::loop()`；`SerialCmdReader.cpp`；`ConfiguredEncoder.cpp`；`Event.cpp` | 采用静态任务表、单调时基、就绪标志及周期健康检查；保留各职责独立周期运行 | 不移植 TaskManagerIO；不采用索引中的 FreeRTOS 候选路线 |

以上“已核对位置”只证明采用的架构和行为模式，不证明其参数、协议或代码可以直接用于本项目硬件。

## Decision-question format

后续只询问会改变系统行为、模块所有权或验证路线，且存在多条合理路径的问题。ExoKit 或索引已有明确适用方案时直接采用并记录，不再把它包装成选择题。确需确认的问题提供 2-3 个方案，逐项说明推荐结论、相对对应模块的 ExoKit 参考实现预计改写量，以及优缺点。百分比只衡量该模块，不外推为整个固件工作量。

## Resolution

控制与嵌入式软件采用四层架构，并以严格单向依赖组织：

1. **L1 STM32 平台层**：提供 HAL/CMSIS、时基、中断、CAN、UART/DMA、Flash 和看门狗等平台机制，不包含关节功能语义。
2. **L2 设备与通信驱动层**：包含CAN分发、串口缓冲、GIM6010-8/GDS68适配、M3508/C620适配、编码器原始状态、多圈累计、固定10 SPS的HX711低速采集和持久化适配；向上提供统一状态与执行接口。
3. **L3 领域与控制层**：保留 `ExoskeletonSystem -> RightArm -> SHOULDER_SIDE / ELBOW`对象关系；两个 Joint/JointController 分别拥有状态换算、重力补偿分量、Amplify、Lock、命令合成、平滑处理和关节限制；`RightArm`只负责整臂动作协调。
4. **L4 系统应用层**：包含启动检查、校准流程、固定文本命令、动作管理器、系统状态机、裸机任务表、遥测服务、健康监测和独立 `SafetySupervisor`。

正常控制数据流固定为：

`执行器/编码器反馈 -> L2 原始反馈解析与有效性标记 -> L3 JointState 统一换算 -> 重力补偿/Amplify/Lock -> 关节级命令合成与平滑限幅 -> L4 SafetySupervisor 最终许可 -> L2 执行器适配 -> 电机驱动器`。

外部命令流固定为：

`UART/DMA 字节流 -> 环形缓冲 -> L4 固定文本解析 -> 动作管理器 -> RightArm 或单关节 Reaction -> L3 候选控制请求`。正常使用整臂命令；单关节命令只用于标定、调试和单独验证。

行为关系固定为：持续重力补偿是正常运行基础层；Amplify 在其上叠加增量助力；Lock 取消 Amplify 并组合重力补偿与当前位置保持；Cancel 取消当前动作并返回重力补偿基础状态。Reaction 不得直接操作执行器，也不得取得最终安全控制权。

调度采用裸机静态周期任务表，不使用 RTOS，不移植 ExoKit 的 TaskManagerIO。中断只更新时间、收发数据和就绪标志，周期任务在主循环中非阻塞运行。任务周期、协议字段、控制参数和安全阈值不在本票据内假定。

模块 1-32 的主参考、拿走部分、复用判断和改写量以本票据的“Open-source index adoption audit”为准。故障条件及故障后的最终输出行为交由“确定运行状态与安全控制权”票据；供电、驱动器、总线和反馈物理连接交由“划分驱动供电与通信模块”票据。
