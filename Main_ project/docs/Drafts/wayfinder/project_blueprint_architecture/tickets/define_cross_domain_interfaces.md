---
title: 定义跨域接口与关系
type: grilling
status: closed
parent: ../map.md
blocked_by:
  - define_mechanical_modules
  - define_electrical_modules
  - define_control_software_modules
  - define_operating_states_and_safety_authority
  - encode_hardware_interfaces
claimed_by: codex
---

## Question

机械、电气、反馈、控制和安全模块之间交换哪些物理量、状态、命令和控制权；如何表达机械载荷路径、能量流、信息流与控制流而不混为一谈？

## Canonical naming and system boundary

- 当前系统只使用 ExoKit 语义关节 `RIGHT/SHOULDER_SIDE` 和 `RIGHT/ELBOW`；项目文档、图表、接口和后续软件不再使用数字关节别名。
- `SHOULDER_SIDE`绑定 GIM6010-8/GDS68，`ELBOW`绑定裸 M3508/C620。
- 本项目不配置 `SHOULDER_BACK`。ExoKit 原有的 `SHOULDER_BACK -> flexible chain link -> SHOULDER_SIDE` 上游结构由台架固定座直接连接 `SHOULDER_SIDE` 固定侧的方式取代。
- 从 `SHOULDER_SIDE`向下保留 ExoKit 的结构语义：上臂刚性连接组件连接 `ELBOW`，`ELBOW`输出侧通过前臂柔性链节、前臂转接件和臂箍连接台架假肢或末端负载。
- 当前第一验收对象是台架假肢或连杆。本文描述架构关系，不构成人体穿戴、承载能力或临床安全验证。

## Relationship types

跨域关系分为四类，禁止在同一箭头中混用不同含义：

| 类型 | 接口前缀 | 表达内容 | 不表达的内容 |
|---|---|---|---|
| 机械载荷路径 | `XDOM-MECH-*` | 结构连接、作用力、反力、转矩和负载接入位置 | 电流、报文、软件调用或安全权限 |
| 能量流 | `XDOM-ENERGY-*` | 电源域、驱动器能量输入及电能到机械输出的转换 | 控制命令含义或反馈状态 |
| 信息流 | `XDOM-INFO-*` | 原始反馈、统一关节状态、命令字节、辅助监测和遥测 | 输出许可或机械承载能力 |
| 控制与权限流 | `XDOM-CTRL-*` | 动作请求、候选输出、限制、许可和安全撤销 | 具体线缆、CAN 字段或机械反力 |

每条接口合同必须同时给出来源、目标、方向、交换内容、所有者、有效条件、失效处理和验证状态。当前只建立逻辑合同；具体引脚、端子、CAN ID、报文字段、周期、阈值和线束仍受接线门禁约束。

## Mechanical load path

ExoKit 手册把 `joint_rod_long`定义为稳定连接肩部与肘部的刚性连杆，把 `chain_mm`、`chain_mf`定义为柔性链节；装配步骤显示两侧刚性连杆插入 `arm_rail`，肘部输出通过链节连接前臂臂箍。本项目继承这些下游关系，只替换 `SHOULDER_SIDE`上游固定方式。

| 接口 | 来源 | 目标 | 交换内容与方向 | 当前边界 |
|---|---|---|---|---|
| `XDOM-MECH-01` | 台架固定基座/支撑杆 | `SHOULDER_SIDE`固定座和固定侧 | 支撑载荷与执行反力传回台架；台架约束固定侧 | 固定座强度和允许载荷未做实物验证 |
| `XDOM-MECH-02` | `SHOULDER_SIDE`输出侧 | `joint_rod_long + arm_rail`上臂刚性连接组件 | 肩侧关节输出作用进入上臂结构 | 物理正方向、零位和活动范围待受限台架试验 |
| `XDOM-MECH-03` | 上臂刚性连接组件 | `ELBOW`固定侧 | 上臂结构支撑肘关节并传递载荷与反力 | `ELBOW`最终减速、输出轴和轴承方案仍为 `TBD` |
| `XDOM-MECH-04` | `ELBOW`输出侧 | `chain_mm/chain_mf -> forearm_chain_adapter -> forearm_cuff` | 肘部输出作用经前臂柔性链节传到前臂负载接入点 | 链节数量、轴承和实物承载能力待验证 |
| `XDOM-MECH-05` | 上臂/前臂臂箍 | 台架假肢或末端负载 | 假肢或负载通过臂箍接入结构主链 | 当前只验证台架配置，不等同人体载荷路径验证 |

载荷或反力可沿结构链反向返回台架，但这不表示软件角度正方向。软件方向必须通过固定基座、限制输出和小角度命令的独立试验确定。

## Energy flow

| 接口 | 来源 | 目标 | 能量内容 | 失效与边界 |
|---|---|---|---|---|
| `XDOM-ENERGY-01` | 台架交流输入域 | `PWR-01` | 交流输入及保护接地边界 | 端子、保护和急停断电器件未确定，状态为 `TBD safety hardware` |
| `XDOM-ENERGY-02` | `PWR-01 / PD-24V-MOTOR` | GDS68 与 C620 | 计划共享的 24 V 动力来源 | 分支、保险、线径、连接器和同时输出能力未验证 |
| `XDOM-ENERGY-03` | GDS68 | GIM6010-8 | 驱动器将直流母线能量转换为 `SHOULDER_SIDE`电机驱动 | 只确认设备能力，不声明允许输出 |
| `XDOM-ENERGY-04` | C620 | 裸 M3508 | C620 经原厂三相链为 `ELBOW`电机供能 | M3508 不得直接连接 24 V 母线 |
| `XDOM-ENERGY-05` | 低压逻辑电源域 | MCU-01、CAN-01/02、ADC-01 | 主控、CAN收发器和辅助传感器逻辑供电 | 低压来源、容量和接线未分配 |
| `XDOM-ENERGY-06` | 两个执行器 | 对应机械关节 | 电磁输出转换为关节机械作用 | 实际转矩、效率、温升和安全输出只能由台架试验确定 |

`SafetySupervisor`可以撤销软件候选输出并请求两个执行器关闭主动输出，但当前不能把这一逻辑权限描述为已经具备物理切断 24 V 的能力。

## Information flow

| 接口 | 来源 | 目标 | 信息合同 | 无效或超时处理 |
|---|---|---|---|---|
| `XDOM-INFO-01` | GDS68/GIM6010-8 | L2 `SHOULDER_SIDE`执行器适配器 | 原始位置、速度和可获得的设备状态，带接收时间和有效标记 | 关键反馈失效提交健康故障；字段、倍率和方向仍须官方资料与实物解析验证 |
| `XDOM-INFO-02` | C620/M3508 | L2 `ELBOW`执行器适配器 | 转子位置、速度、电流、温度等原始反馈，带接收时间和有效标记 | 关键反馈失效提交健康故障；转子到关节的换算依赖已确认机械关系 |
| `XDOM-INFO-03` | 两个执行器适配器 | L3 JointState | 统一单位、方向、零位、有效性、时间戳及可选处理后速度 | 无效状态不得进入正常控制计算 |
| `XDOM-INFO-04` | FORCE-01/HX711 | 低速辅助监测与遥测 | 固定10 SPS的新样本、标定状态、有效性和时间戳 | 失效只降低辅助监测能力，不改变编码器主控制或触发安全动作 |
| `XDOM-INFO-05` | PC/调试工具 | UART缓冲与固定文本解析器 | ExoKit风格命令字节和解析结果 | 无效命令被拒绝；调试串口不是持续控制心跳 |
| `XDOM-INFO-06` | 系统公开状态 | PC/调试工具 | 关节状态、候选/最终命令、系统状态、限制器和故障原因 | 遥测丢失不允许绕过本地状态机和安全监督 |
| `XDOM-INFO-07` | 设备在线监测、任务健康和状态有效性检查 | `SafetySupervisor` | 关键反馈健康、任务失期、看门狗资格和故障原因 | 任一关键执行链不可证明健康时撤销两个关节主动输出 |

IMU-01/02 当前为库存能力，不进入本版信息流、控制流或重力补偿输入。

## Control and authority flow

| 接口 | 来源 | 目标 | 控制合同 | 权限边界 |
|---|---|---|---|---|
| `XDOM-CTRL-01` | 外部文本命令 | L4 动作管理器 | 请求 Amplify、Lock、Cancel、校准或调试动作 | 外部命令只能提出请求，不能直接写执行器 |
| `XDOM-CTRL-02` | 动作管理器/RightArm | 两个关节 Reaction | 启动、取消和协调 `SHOULDER_SIDE`、`ELBOW`动作生命周期 | 新动作按 ExoKit 语义取消并清理旧动作 |
| `XDOM-CTRL-03` | JointState | 重力补偿、AmplifyReaction、LockReaction | 为每个关节提供当前有效状态 | Reaction 不拥有安全状态或最终输出权限 |
| `XDOM-CTRL-04` | 重力补偿与当前 Reaction | 关节命令合成器 | 形成重力基线、Amplify增量或Lock保持候选量 | Cancel 只移除当前动作并返回重力补偿基线 |
| `XDOM-CTRL-05` | 关节命令合成器 | 角度/速度/输出限制层 | 提交各关节候选命令 | 所有限制位于 Reaction 之外，具体阈值保持 `TBD` |
| `XDOM-CTRL-06` | 系统状态与 `assist_enable_permission` | `SafetySupervisor`最终安全门 | 表明当前状态是否允许主动输出 | 未获许可时不得进入或保持 FOLLOW、ASSIST、LOCK主动输出 |
| `XDOM-CTRL-07` | 限制后的候选命令 | `SafetySupervisor` | 请求最终发送许可 | 安全门可拒绝、替换或撤销任意候选输出 |
| `XDOM-CTRL-08` | `SafetySupervisor` | L2两个执行器适配器 | 分别下发许可后的命令或安全撤销请求 | 任何Reaction、JointController或外部命令不得绕过该路径 |

最终控制权顺序固定为：

`SafetySupervisor > system-state permission > joint angle/velocity/output limits > command composition > Reaction > external command`。

正常控制闭环固定为：

`执行器反馈 -> L2原始解析与有效性 -> L3 JointState -> 重力补偿/Amplify/Lock -> 命令合成 -> 关节限制 -> L4 SafetySupervisor -> L2执行器适配 -> 执行器`。

## Safety override and failure behavior

| 事件 | 信息来源 | 控制结果 | 状态结果 |
|---|---|---|---|
| 普通 Cancel 或新动作替换 | 动作管理器 | 清理当前Reaction并恢复重力补偿基础候选 | 返回 `FOLLOW`或进入新动作，不进入故障 |
| HX711无效或超时 | `XDOM-INFO-04` | 主控制保持编码器路线；停止使用辅助监测值 | 报告监测降级，不进入 `FAULT` |
| 任一执行器关键反馈失效 | `XDOM-INFO-01/02/07` | 撤销 `SHOULDER_SIDE`与`ELBOW`主动输出 | 锁存 `FAULT` |
| 关节状态无效、越界或不可解释跳变 | `XDOM-INFO-03/07` | 拒绝候选命令并撤销两个执行器输出 | 锁存 `FAULT` |
| 控制/安全任务失期 | `XDOM-INFO-07` | 能执行时先撤销输出，并禁止继续喂看门狗 | `FAULT`或看门狗复位 |
| 紧急停止 | 未来确认的急停接口 | 最高优先级取消动作并请求两个执行器关闭主动输出 | 锁存 `FAULT`；普通Cancel不能清除 |

故障阈值、撤销时序、物理急停是否切断24 V及恢复需要人工复位还是重新上电，均保持 `TBD`，不得从软件权限推断为已实现硬件安全链。

## End-to-end joint mapping

| ExoKit joint | Mechanical | Electrical and feedback | Domain control | Final authority |
|---|---|---|---|---|
| `RIGHT/SHOULDER_SIDE` | 台架固定座 -> GIM6010-8关节 -> 上臂刚性连接组件 | `ACT-01` GIM6010-8/GDS68 -> CAN-01 -> `SHOULDER_SIDE`适配器 -> JointState | `SHOULDER_SIDE`重力分量、AmplifyReaction、LockReaction、命令合成和关节限制 | `SafetySupervisor`许可后返回该执行器适配器 |
| `RIGHT/ELBOW` | 上臂刚性连接组件 -> `ELBOW` -> 前臂柔性链节与臂箍 | `ESC-01 + MOTOR-01` C620/裸M3508 -> CAN-02 -> `ELBOW`适配器 -> JointState | `ELBOW`重力分量、AmplifyReaction、LockReaction、命令合成和关节限制 | `SafetySupervisor`许可后返回该执行器适配器 |

## Evidence and reuse traceability

- ExoKit结构与命名：`Open-source projects/ExoKit-main/ExoKit Manual V1.pdf`第2、3章；`3d-prints/README.txt`；`ExoskeletonArmHandle`、`ArmBuilder`和Reaction源码。保留下游关节/连杆/链节关系、Joint归属和Reaction生命周期，替换 `SHOULDER_BACK`上游固定、Arduino/Dynamixel底层和最终输出所有权。
- 机械当前件：`self models/右臂/`及右臂打印清单只证明现有文件与试装范围，不证明承载能力或最终 `ELBOW`传动。
- 电气端点与证据等级：`Main_ project/hardware_list/hardware_components_reference.md`和`hardware_interface_registry.md`。接口编码不等于项目接线已经确定。
- 软件、控制与安全：直接采用已关闭的控制软件、运行状态和安全控制权票据；开源主参考及改写量以控制软件票据中的索引审计为准。

## Resolution

跨域架构采用四类不可混用的关系：机械载荷、能量、信息、控制与权限。反馈属于信息流；安全属于覆盖正常控制流的最终权限，不另画成无含义的“安全数据流”。每条关系使用稳定的 `XDOM-*`编号和单向来源/目标，并保持与 `SHOULDER_SIDE`、`ELBOW`两个ExoKit语义关节的一一映射。

项目只替换 ExoKit `SHOULDER_SIDE`上游的肩背关节与柔性链节：台架固定座直接连接 `SHOULDER_SIDE`固定侧；从该关节向下继续采用 ExoKit 的上臂刚性连接、`ELBOW`、前臂柔性链节和臂箍关系。该描述是架构决定，不是承载或人体安全验证。

正常控制输出必须沿 `外部请求 -> Reaction -> 命令合成 -> 关节限制 -> SafetySupervisor -> 执行器适配器`流动；任何关键执行器反馈失效、无效关节状态、关键任务失期或紧急停止都会撤销两个主动关节输出并锁存 `FAULT`。具体接线、参数、急停断电硬件和实物承载能力继续由后续硬件门禁与台架验证决定。
