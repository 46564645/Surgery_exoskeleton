# 上肢助力外骨骼系统架构蓝图

> 本文件是当前项目技术路线与系统架构的权威来源。它定义模块边界、依赖方向、跨域接口和安全控制权，但不构成具体接线图、机械承载证明、人体安全验证或临床验证。需求优先级见 [需求蓝图](Drafts/project_requirements_blueprint.md)，设备端点与证据等级见 [硬件接口表](../Hardware_information_list/hardware_interface_registry.md)。

> 当前本地目录已生成电气与通信架构 SVG，关系线统一为直线或折线。其余四张 SVG 尚未补回。控制与软件章节的 Mermaid 源图及文字规则已修订；对应 SVG 补回并重新渲染前，不应把历史图片视为本版规则的证明。

## 1. 阅读规则与固定边界

- 当前系统是落地承重、单右臂、台架优先的学生原型，第一验收对象是台架假肢或末端负载。
- 关节统一使用 ExoKit 语义名：`RIGHT/SHOULDER_SIDE`和`RIGHT/ELBOW`；机械、电气图使用`SHOULDER_SIDE`和`ELBOW`。
- `SHOULDER_SIDE`使用 GIM6010-8/GDS68；`ELBOW`使用裸 M3508/C620，不采用 P19 总成参数。
- 本系统不配置`SHOULDER_BACK`。台架固定座直接连接`SHOULDER_SIDE`固定侧；从该关节向下继续采用 ExoKit 的上臂刚性连接、`ELBOW`、前臂柔性链节和臂箍关系。
- 持续重力补偿是正常运行基础；Amplify 根据关节编码器方向与速度叠加增量助力；HX711 只作固定 10 SPS 的低速辅助监测；IMU 当前停用。
- 关系状态使用：`CONFIRMED`表示文档层架构决定，`PLANNED`表示已选路线但尚未实物验证，`TBD`表示必须经资料、接线门禁或台架试验确定。
- 图形颜色只作辅助。每条线均带方向、关系编号或文字标签，图后的 Markdown 表是无图形渲染时的完整等价表达。

### 1.1 关系图例

| 关系 | 编号 | 图中表达 | 含义 |
|---|---|---|---|
| 机械载荷 | `XDOM-MECH-*` | 实线、机械标签 | 结构连接、作用力、反力和负载接入 |
| 能量 | `XDOM-ENERGY-*` | 实线、能量标签 | 电源域和电能到机械作用的转换 |
| 信息 | `XDOM-INFO-*` | 虚线、信息标签 | 反馈、状态、命令字节和遥测 |
| 控制与权限 | `XDOM-CTRL-*` | 粗线、控制标签 | 动作请求、候选输出、许可和撤销 |
| 待确定 | `TBD` | 点线或节点内`TBD` | 尚未得到足够证据，不能自行补齐 |

## 2. 系统边界总览

本图回答：系统由哪些专业域组成，用户/台架负载、系统本体和外部调试工具分别位于哪里？

![系统边界总览](architecture_diagrams/system_context.svg)

```mermaid
flowchart TB
    LOAD["台架假肢或末端负载<br/>第一验收对象"]
    MECH["机械与物理系统<br/>落地支撑 + 右臂结构"]
    ELEC["电气与通信系统<br/>供电 + 双 CAN + 辅助采集"]
    CTRL["STM32F446RE 控制系统<br/>状态、控制与安全"]
    TOOL["PC / 调试工具<br/>命令与遥测"]
    HUMAN["未来人体演示<br/>不属于当前验收"]

    LOAD -->|"机械负载与反力"| MECH
    CTRL -->|"许可后的执行命令"| ELEC
    ELEC -->|"电机产生机械作用"| MECH
    ELEC -.->|"执行器和辅助监测信息"| CTRL
    TOOL -.->|"文本动作请求"| CTRL
    CTRL -.->|"状态、限制和故障原因"| TOOL
    HUMAN -.->|"后续阶段，非当前验证"| LOAD

    classDef boundary fill:#f4f7fa,stroke:#425466,stroke-width:2px,color:#17202a;
    classDef mechanical fill:#e8eef5,stroke:#355070,stroke-width:2px,color:#17202a;
    classDef electrical fill:#fff1d6,stroke:#a15c00,stroke-width:2px,color:#17202a;
    classDef control fill:#e5f4ef,stroke:#24735a,stroke-width:2px,color:#17202a;
    classDef future fill:#ffffff,stroke:#7f8c8d,stroke-width:2px,stroke-dasharray:6 4,color:#566573;
    class LOAD boundary;
    class MECH mechanical;
    class ELEC electrical;
    class CTRL,TOOL control;
    class HUMAN future;
```

### 2.1 文字关系图

`PC/调试工具 -> STM32控制系统 -> 电气与通信系统 -> 机械系统 -> 台架假肢或末端负载`

反馈沿独立方向返回：`执行器/辅助监测 -> STM32控制系统 -> PC遥测`。未来人体演示只是一项后续目标，不能替代当前台架验证，也不能由本蓝图推导出人体安全结论。

### 2.2 总览关系表

| 来源 | 目标 | 方向 | 交换内容 | 失效处理 | 验证状态 |
|---|---|---|---|---|---|
| 台架假肢或末端负载 | 机械系统 | 负载进入结构链 | 重力、外力和约束反力 | 停止主动输出并检查结构 | `PLANNED` |
| STM32控制系统 | 电气与通信系统 | 控制器到执行链 | 许可后的关节命令 | 通信或反馈异常进入`FAULT` | `PLANNED` |
| 电气与通信系统 | 机械系统 | 执行器到关节 | 电能转换后的机械作用 | 驱动器保护与系统安全门共同撤销输出 | `PLANNED` |
| 电气与通信系统 | STM32控制系统 | 反馈返回 | 位置、速度、设备状态和辅助监测 | 关键反馈失效进入`FAULT` | `PLANNED` |
| PC/调试工具 | STM32控制系统 | 外部请求 | ExoKit风格固定文本命令 | 无效命令拒绝，不改变当前有效状态 | `CONFIRMED` |
| STM32控制系统 | PC/调试工具 | 遥测输出 | 状态、候选/最终命令、限制和故障 | 遥测丢失不绕过本地控制 | `CONFIRMED` |

### 2.3 边界与 TBD

- 已确认：单右臂、台架优先、落地承重、双主动关节、STM32F446RE 集中控制。
- `TBD`：人体连接与承载能力、物理急停链、最终线束和任何临床使用条件。

## 3. 机械与物理架构

本图回答：机械作用、负载和反力沿哪些结构传递，当前系统相对 ExoKit 替换了哪一段？

![机械与物理架构](architecture_diagrams/mechanical_physical_architecture.svg)

```mermaid
flowchart TB
    BASE["台架固定基座 / 支撑杆"]
    MOUNT["SHOULDER_SIDE 固定座与固定侧"]
    SHOULDER["SHOULDER_SIDE<br/>GIM6010-8 机械关节"]
    UPPER["上臂刚性组件<br/>joint_rod_long + arm_rail + 上臂臂箍"]
    ELBOW["ELBOW<br/>输出轴/减速/轴承 TBD"]
    FORE["前臂柔性链节<br/>chain_mm / chain_mf"]
    ADAPTER["forearm_chain_adapter<br/>+ forearm_cuff"]
    LOAD["台架假肢或末端负载"]
    NO_BACK["无 SHOULDER_BACK<br/>无其上游柔性链节"]

    BASE -->|"XDOM-MECH-01 支撑载荷 / 反力返回"| MOUNT
    MOUNT --> SHOULDER
    SHOULDER -->|"XDOM-MECH-02 肩侧输出"| UPPER
    UPPER -->|"XDOM-MECH-03 支撑 ELBOW 固定侧"| ELBOW
    ELBOW -->|"XDOM-MECH-04 肘部输出"| FORE
    FORE --> ADAPTER
    ADAPTER -->|"XDOM-MECH-05 负载接入"| LOAD
    NO_BACK -.->|"由台架直接固定替代"| MOUNT

    classDef mechanical fill:#e8eef5,stroke:#355070,stroke-width:2px,color:#17202a;
    classDef tbd fill:#ffffff,stroke:#7f8c8d,stroke-width:2px,stroke-dasharray:6 4,color:#566573;
    class BASE,MOUNT,SHOULDER,UPPER,FORE,ADAPTER,LOAD mechanical;
    class ELBOW,NO_BACK tbd;
```

### 3.1 文字关系图

`台架固定基座/支撑杆 -> SHOULDER_SIDE固定座 -> SHOULDER_SIDE -> joint_rod_long + arm_rail -> ELBOW -> chain_mm/chain_mf -> forearm_chain_adapter + forearm_cuff -> 台架假肢或末端负载`

载荷和执行反力可沿同一结构链反向返回台架。这个反向机械反力不定义软件角度正负，软件方向必须通过固定基座、低输出和小角度命令的台架试验确认。

### 3.2 机械接口表

| 接口 | 来源 | 目标 | 方向与交换内容 | 失效处理 | 验证状态 |
|---|---|---|---|---|---|
| `XDOM-MECH-01` | 台架固定基座/支撑杆 | `SHOULDER_SIDE`固定座和固定侧 | 支撑载荷进入结构；执行反力返回台架 | 发现松动、变形或异常位移时停止主动输出 | `PLANNED`，强度未验证 |
| `XDOM-MECH-02` | `SHOULDER_SIDE`输出侧 | 上臂刚性组件 | 肩侧机械作用进入`joint_rod_long + arm_rail` | 方向、零位或范围不明时禁止正常控制 | `PLANNED` |
| `XDOM-MECH-03` | 上臂刚性组件 | `ELBOW`固定侧 | 支撑肘关节并传递载荷和反力 | 未确定输出支撑结构前不得声明承载能力 | `PLANNED` |
| `XDOM-MECH-04` | `ELBOW`输出侧 | 前臂链节与转接件 | 肘部输出经柔性链节传向前臂 | 异常间隙、干涉或卡滞时停止测试 | `PLANNED` |
| `XDOM-MECH-05` | 上臂/前臂臂箍 | 台架假肢或末端负载 | 负载通过臂箍接入主链 | 滑移或松脱时停止主动输出 | `PLANNED`，非人体验证 |

### 3.3 边界与 TBD

- 已确认：只替换 ExoKit `SHOULDER_SIDE`上游的背架、`SHOULDER_BACK`和肩部柔性链节；其下游结构语义保持一致。
- `TBD`：`ELBOW`最终减速、独立输出轴、轴承、机械限位、结构强度、允许载荷和活动范围。

## 4. 电气与通信架构

本图回答：电能、执行器反馈、辅助监测和外部命令分别沿哪些独立网络流动？

器件逐线连接见[电机与 CAN 模块图纸的详细文字描述](../Hardware_information_list/motor_can_diagrams_text.md)，STM32 项目接口见[C04 AI 结构化 JSON](../Hardware_information_list/nucleo_f446re_mb1136_c04_ai.json)（非整板完整网表）。现行引脚分配、逐线接线和实物验证状态以[硬件接口登记第10节](../Hardware_information_list/hardware_interface_registry.md#10-台架接线设计分配2026-09-24)为准。AI 查手册时优先读取对应型号/版本且正文可读的 Word，入口见文字说明第0.1节；接头方向、针号和线路同时核对原 PDF/原理图。

![电气与通信架构](architecture_diagrams/electrical_communication_architecture.svg)

```mermaid
%%{init: {"flowchart": {"curve": "linear"}}}%%
flowchart TB
    AC["台架交流输入<br/>保护与急停硬件 TBD"]
    PWR["PWR-01<br/>S-250-24 24 V / 10 A"]
    MOTORBUS["NET-PWR-24V-MOTOR<br/>24 V 动力母线"]
    LV["低压逻辑电源<br/>来源与容量 TBD"]
    LOGICBUS["低压逻辑供电域<br/>分支与保护 TBD"]
    MCU["MCU-01<br/>NUCLEO-F446RE"]
    CAN1["CAN-01<br/>SN65HVD230"]
    GDS["ACT-01<br/>GDS68 + GIM6010-8"]
    SHOULDER["SHOULDER_SIDE<br/>机械作用"]
    CAN2["CAN-02<br/>SN65HVD230"]
    C620["ESC-01<br/>C620"]
    M3508["MOTOR-01<br/>裸 M3508"]
    ELBOW["ELBOW<br/>机械作用"]
    FORCE["FORCE-01<br/>5 kg 直梁式传感器"]
    HX["ADC-01<br/>HX711 固定 10 SPS"]
    PC["PC / 调试工具"]
    IMU["IMU-01/02<br/>当前停用"]

    AC -->|"XDOM-ENERGY-01"| PWR
    PWR -->|"XDOM-ENERGY-02"| MOTORBUS
    MOTORBUS -->|"24 V 动力"| GDS
    MOTORBUS -->|"24 V 动力"| C620
    LV -->|"XDOM-ENERGY-05"| LOGICBUS
    LOGICBUS --> MCU
    LOGICBUS --> CAN1
    LOGICBUS --> CAN2
    LOGICBUS --> HX
    GDS -->|"XDOM-ENERGY-03"| SHOULDER
    C620 -->|"XDOM-ENERGY-04 三相驱动"| M3508
    M3508 -->|"XDOM-ENERGY-06"| ELBOW

    MCU -->|"NET-CAN-SHOULDER-SIDE 命令"| CAN1
    CAN1 --> GDS
    GDS -.->|"XDOM-INFO-01 反馈"| CAN1
    CAN1 -.-> MCU
    MCU -->|"NET-CAN-ELBOW 命令"| CAN2
    CAN2 --> C620
    C620 -.->|"XDOM-INFO-02 反馈"| CAN2
    CAN2 -.-> MCU
    C620 -->|"NET-M3508-DATA / PHASE"| M3508

    FORCE -->|"NET-FORCE-BRIDGE"| HX
    HX -.->|"XDOM-INFO-04 辅助监测"| MCU
    PC -.->|"XDOM-INFO-05 文本命令"| MCU
    MCU -.->|"XDOM-INFO-06 遥测"| PC
    IMU -.->|"不进入当前信息流"| MCU

    classDef power fill:#fff1d6,stroke:#a15c00,stroke-width:2px,color:#17202a;
    classDef device fill:#e5f4ef,stroke:#24735a,stroke-width:2px,color:#17202a;
    classDef monitor fill:#e8eef5,stroke:#355070,stroke-width:2px,color:#17202a;
    classDef tbd fill:#ffffff,stroke:#7f8c8d,stroke-width:2px,stroke-dasharray:6 4,color:#566573;
    class AC,LV,LOGICBUS tbd;
    class PWR,MOTORBUS power;
    class MCU,CAN1,CAN2,GDS,C620,M3508 device;
    class FORCE,HX,PC,SHOULDER,ELBOW monitor;
    class IMU tbd;
```

### 4.1 文字关系图

动力路径：`交流输入 -> PWR-01 24 V -> GDS68/GIM6010-8 与 C620/M3508 -> 两个机械关节`。

通信路径：`STM32F446RE -> CAN-01 -> GDS68/GIM6010-8`，以及`STM32F446RE -> CAN-02 -> C620 -> M3508`。辅助监测路径为`直梁式传感器 -> HX711固定10 SPS -> STM32`，不参与主动助力主判断或安全触发。

### 4.2 能量接口表

| 接口 | 来源 | 目标 | 方向与交换内容 | 失效处理 | 验证状态 |
|---|---|---|---|---|---|
| `XDOM-ENERGY-01` | 台架交流输入域 | `PWR-01` | 交流输入与保护接地边界 | 保护器件未确定前不形成最终接线 | `TBD safety hardware` |
| `XDOM-ENERGY-02` | `PWR-01` | GDS68与C620 | 计划共享24 V动力来源 | 分支、保险、线径和容量未确认前限制测试 | `PLANNED` |
| `XDOM-ENERGY-03` | GDS68 | GIM6010-8/`SHOULDER_SIDE` | 电能转换为肩侧机械作用 | 驱动异常由安全门撤销输出 | `PLANNED` |
| `XDOM-ENERGY-04` | C620 | 裸M3508 | 原厂三相驱动链 | 禁止M3508直接连接24 V母线 | `CONFIRMED`设备关系 |
| `XDOM-ENERGY-05` | 低压逻辑电源域 | MCU、CAN模块、HX711 | 逻辑供电 | 来源、容量和接线确定前保持`TBD` | `TBD` |
| `XDOM-ENERGY-06` | 两个执行器 | 对应机械关节 | 电磁输出转换为机械作用 | 实际输出只由受限台架试验确认 | `PLANNED` |

### 4.3 电气信息接口表

| 接口 | 来源 | 目标 | 方向与交换内容 | 失效处理 | 验证状态 |
|---|---|---|---|---|---|
| `XDOM-INFO-01` | GDS68/GIM6010-8 | `SHOULDER_SIDE`适配器 | 原始位置、速度和可用设备状态 | 关键反馈无效或超时进入`FAULT` | `PLANNED`，字段待验证 |
| `XDOM-INFO-02` | C620/M3508 | `ELBOW`适配器 | 转子位置、速度、电流、温度等反馈 | 关键反馈无效或超时进入`FAULT` | `PLANNED`，换算依赖机械关系 |
| `XDOM-INFO-04` | FORCE-01/HX711 | 辅助监测与遥测 | 10 SPS样本、标定状态、有效性和时间戳 | 只报告监测降级，不进入`FAULT` | `PLANNED` |
| `XDOM-INFO-05` | PC/调试工具 | UART缓冲与文本解析器 | ExoKit风格命令字节 | 无效命令拒绝；串口不是控制心跳 | `CONFIRMED`逻辑合同 |
| `XDOM-INFO-06` | 系统公开状态 | PC/调试工具 | 状态、命令、限制和故障原因 | 遥测丢失不影响本地安全门 | `CONFIRMED`逻辑合同 |

### 4.4 边界与 TBD

- 已确认：两个异构执行链分别使用独立 CAN 逻辑网络；C620与M3508保留原厂数据和三相链。
- `TBD`：MCU外设实例和引脚、CAN ID/字段/波特率、终端电阻、线束、保险、连接器、低压电源及物理急停断电器件。

## 5. 功能、控制与安全权限

本图回答：状态如何形成候选控制量，谁拥有最终输出许可，以及异常如何覆盖正常动作？

![功能、控制与安全权限](architecture_diagrams/functional_control_architecture.svg)

```mermaid
flowchart TB
    EXT["外部固定文本命令"]
    RAW["执行器原始反馈<br/>XDOM-INFO-01/02"]
    FORCE["HX711辅助监测<br/>XDOM-INFO-04"]
    JSTATE["JointState<br/>单位/方向/零位/有效性/时间戳"]
    ACTION["动作管理器 + RightArm<br/>Condition / Event / Reaction"]
    GRAV["持续重力补偿"]
    AMP["AmplifyReaction<br/>方向与速度判断"]
    LOCK["LockReaction<br/>位置保持"]
    COMPOSE["关节命令合成器"]
    LIMIT["角度 / 速度 / 输出限制"]
    PERMIT["系统状态许可<br/>assist_enable_permission"]
    HEALTH["健康监测<br/>XDOM-INFO-07"]
    SAFE["唯一最终输出入口<br/>SafetySupervisor 检查许可与限制 / 可否决"]
    ADAPTER["两个执行器适配器"]
    TELE["遥测与故障原因"]

    RAW -.->|"XDOM-INFO-03 统一状态"| JSTATE
    JSTATE -->|"XDOM-CTRL-03"| GRAV
    JSTATE -->|"XDOM-CTRL-03"| AMP
    JSTATE -->|"XDOM-CTRL-03"| LOCK
    EXT -->|"XDOM-CTRL-01 动作请求"| ACTION
    ACTION -->|"XDOM-CTRL-02 启动/取消"| AMP
    ACTION -->|"XDOM-CTRL-02 启动/取消"| LOCK
    GRAV -->|"XDOM-CTRL-04 补偿力矩 Nm"| COMPOSE
    AMP -->|"XDOM-CTRL-04 增量请求 / 数值合同 TBD"| COMPOSE
    LOCK -->|"XDOM-CTRL-04 保持角度 rad"| COMPOSE
    COMPOSE -->|"XDOM-CTRL-05 候选命令"| LIMIT
    PERMIT ==>|"XDOM-CTRL-06 状态许可"| SAFE
    LIMIT ==>|"XDOM-CTRL-07 受限候选"| SAFE
    HEALTH ==>|"故障可撤销许可"| SAFE
    SAFE ==>|"XDOM-CTRL-08 最终命令/撤销"| ADAPTER
    FORCE -.->|"仅监测，不进入主控制"| TELE
    JSTATE -.-> TELE
    SAFE -.-> TELE

    classDef info fill:#e8eef5,stroke:#355070,stroke-width:2px,color:#17202a;
    classDef control fill:#e5f4ef,stroke:#24735a,stroke-width:2px,color:#17202a;
    classDef safety fill:#fde8e7,stroke:#a93226,stroke-width:3px,color:#17202a;
    class EXT,RAW,FORCE,JSTATE,HEALTH,TELE info;
    class ACTION,GRAV,AMP,LOCK,COMPOSE,LIMIT,PERMIT,ADAPTER control;
    class SAFE safety;
```

### 5.1 文字关系图

实现查阅：[动作入口 REF-EXO-01](#ref-exo-01)、[Event REF-EXO-02](#ref-exo-02)、[生命周期 REF-EXO-03](#ref-exo-03)；完整路线见[实现索引](#implementation-reference)。

正常控制链：`执行器反馈 -> 原始解析与有效性 -> JointState -> 重力补偿/Amplify/Lock -> 命令合成 -> 关节限制 -> 唯一最终输出入口（SafetySupervisor 检查） -> 执行器适配器 -> CAN`。

控制权固定为：`SafetySupervisor > 系统状态许可 > 关节限制 > 命令合成器 > Reaction > 外部命令`。任何 Reaction 都只能形成候选请求，不能直接发送 CAN 命令。

唯一最终输出入口是输出的唯一提交者，不是新增一套复杂框架。它在提交前检查当前许可、健康状态和限制结果，再调用两个执行器适配器；普通控制、调试、标定、使能/禁用及动作取消都不得另设直接发送路径。CAN 中断或发送队列只搬运已批准的报文，不拥有独立控制权；许可撤销时不得继续重放旧的正常命令，队列作废机制与时序为 `TBD`。独立硬件停止手段不依赖此软件入口，也不视为控制旁路。

### 5.2 功能信息接口表

| 接口 | 来源 | 目标 | 方向与交换内容 | 失效处理 | 验证状态 |
|---|---|---|---|---|---|
| `XDOM-INFO-03` | 两个执行器适配器 | JointState | 统一单位、方向、零位、有效性和时间戳 | 无效状态不得进入正常控制 | `CONFIRMED`逻辑合同 |
| `XDOM-INFO-07` | 设备在线、任务健康和状态检查 | `SafetySupervisor` | 关键反馈、任务失期和故障原因 | 无法证明健康时撤销两个关节输出 | `CONFIRMED`逻辑合同 |

### 5.3 控制与权限接口表

| 接口 | 来源 | 目标 | 方向与交换内容 | 失效处理 | 验证状态 |
|---|---|---|---|---|---|
| `XDOM-CTRL-01` | 外部文本命令 | 动作管理器 | 请求Amplify、Lock、Cancel、校准或调试动作 | 外部请求不能直接写执行器 | `CONFIRMED` |
| `XDOM-CTRL-02` | 动作管理器/RightArm | 两个关节Reaction | 启动、取消和协调动作生命周期 | 新动作先取消并清理旧动作 | `CONFIRMED` |
| `XDOM-CTRL-03` | JointState | 重力补偿、Amplify、Lock | 提供当前有效关节状态 | 状态无效时不计算正常候选量 | `CONFIRMED` |
| `XDOM-CTRL-04` | 重力补偿与Reaction | 命令合成器 | 关节侧补偿力矩、带类型的增量请求或保持角度；见5.7 | 普通Cancel只移除当前动作；健康且有许可才保留基线 | 行为`CONFIRMED`；增量数值合同`TBD` |
| `XDOM-CTRL-05` | 命令合成器 | 关节限制层 | 提交各关节候选命令 | 超界候选被限制或拒绝 | `CONFIRMED`，阈值`TBD` |
| `XDOM-CTRL-06` | 系统状态和许可 | `SafetySupervisor` | 表示是否允许主动输出 | 无许可不得进入或保持主动状态 | `CONFIRMED` |
| `XDOM-CTRL-07` | 受限候选命令 | `SafetySupervisor` | 请求最终发送许可 | 安全门可以拒绝、替换或撤销 | `CONFIRMED` |
| `XDOM-CTRL-08` | 唯一最终输出入口（`SafetySupervisor`检查） | 两个执行器适配器 | 下发批准的命令或停止/禁用请求 | 不允许旁路；请求已发出不等于电机已停止 | 入口规则`CONFIRMED`；设备执行行为`TBD` |

### 5.4 系统状态与转换

| 当前状态 | 进入条件 | 允许行为 | 主要退出方向 |
|---|---|---|---|
| `STARTUP_CHECK` | 上电或复位 | 先保持输出未许可，再检查复位原因、配置与关键反馈；不恢复旧动作 | 有效零位到`SAFE_OFF`；无有效零位到`CALIBRATION`；异常到`FAULT`；故障恢复限制不能因重启清除 |
| `CALIBRATION` | 强制标定或零位无效 | 逐关节tare；主动助力与保持关闭 | 标定成功到`SAFE_OFF`；异常到`FAULT` |
| `SAFE_OFF` | 启动检查后安全等待、人工停止或恢复检查 | 主动输出未许可 | 启动/恢复规则满足且`assist_enable_permission`成立到`FOLLOW`；异常到`FAULT`；规则见5.9 |
| `FOLLOW` | 正常许可且无额外动作 | 持续重力补偿 | Amplify请求到`ASSIST`；Lock请求到`LOCK`；许可撤销到`SAFE_OFF`；严重故障到`FAULT` |
| `ASSIST` | Amplify动作有效 | 有许可的重力补偿，加当前助推条件允许的增量；暂停时增量为零 | 暂停不结束动作，仍留在`ASSIST`；Cancel回`FOLLOW`；Lock替换到`LOCK`；许可撤销到`SAFE_OFF`；严重故障到`FAULT` |
| `LOCK` | Lock动作有效 | 重力补偿加当前位置保持 | Cancel回`FOLLOW`；Amplify请求按动作替换规则进入`ASSIST`；许可撤销到`SAFE_OFF`；严重故障到`FAULT` |
| `FAULT` | 关键反馈、状态、任务、驱动或急停故障 | 锁存原因、取消动作、撤销两个关节主动输出许可；实际停止待确认 | 原因解除并检查后到`SAFE_OFF`等待重新确认；重启走`STARTUP_CHECK`，不得自动恢复出力 |

### 5.5 安全覆盖规则

| 事件 | 处理 | 是否进入`FAULT` |
|---|---|---|
| Cancel或新动作替换 | 清理当前Reaction；健康且有许可才返回`FOLLOW`或进入新动作；不得解除故障 | 否 |
| Amplify低速、反向暂停或助推速度上限触发 | 只撤掉增量，动作继续观察；恢复条件满足后可重新助推 | 否；若同时触发系统安全限值则按故障处理 |
| 无效文本命令 | 拒绝并报告，不改变当前状态 | 否 |
| HX711无效或超时 | 报告辅助监测降级，主控制不变 | 否 |
| 任一执行器关键反馈失效 | 撤销两个关节主动输出 | 是 |
| 关节状态无效、越界或不可解释跳变 | 拒绝候选命令并撤销输出 | 是 |
| 控制或安全任务失期 | 能执行时先撤销输出，并停止喂狗 | 是或看门狗复位 |
| 紧急停止 | 最高优先级取消动作并请求关闭主动输出 | 是，普通Cancel不能清除 |

### 5.6 边界与 TBD

- 已确认：健康且有许可时重力补偿是基线；Amplify只增加增量；Lock保留基线；安全监督可通过唯一输出入口否决所有正常输出。这里的确认仅指文档规则，不是实现或台架验证。
- `TBD`：控制周期、滤波参数、角度/速度/输出阈值、增量数值合同、控制环部署、两个驱动器控制能力与换算、故障撤销时序、独立停止手段与负载承托、复位原因识别及恢复限制的保留机制。

### 5.7 功能输出类型与合成边界

实现查阅：[Amplify REF-EXO-04](#ref-exo-04)、[Lock REF-EXO-06](#ref-exo-06)、[GALAXEA合成 REF-GAL-01](#ref-gal-01)、[M3508外环 REF-BETA-02](#ref-beta-02)、[GIM适配 REF-GIM-01](#ref-gim-01)。

| 功能 | 候选请求及单位 | 合成要求 | 当前结论 |
|---|---|---|---|
| 持续重力补偿 | 关节侧补偿力矩，Nm | 仅与同坐标、同单位的力矩项相加 | 接口语义已定；模型、参数、符号与有效范围`TBD` |
| Amplify | 有方向的增量助力请求 | 与Lock互斥；若输出力矩，应先统一到关节侧Nm再叠加基线 | 最终数值合同`TBD`；不得把ExoKit电流百分比直接当Nm或C620命令 |
| Lock | 进入时记录的关节保持角度，rad | 保持目标与重力前馈同时存在；位置值不能与电流或力矩直接相加 | 位置控制环在STM32还是驱动器内为`TBD` |
| 命令合成器 | 带模式和单位的关节候选命令 | 先处理动作互斥与量纲，再执行限制；不是把所有输入数值相加 | 最终字段与各执行器映射`TBD` |

文字关系：`重力力矩 + 同单位的附加力矩 -> 力矩前馈`；`保持角度 -> 位置控制目标`。两者如何形成设备命令，取决于已核实的驱动模式。

两条路线均仅为候选：在STM32计算保持控制项后与重力力矩合成；或将位置/速度/增益与前馈分别交给支持该模式的驱动器。不得预先假定GDS68和C620具有相同能力，也不强制两个关节采用同一种底层控制方式。确认协议、单位、符号、传动关系及控制环分工后，才能冻结实现合同。不能照搬Dynamixel工作模式或参考项目参数。

### 5.8 暂时不助推与取消动作

实现查阅：[Amplify生命周期 REF-EXO-04](#ref-exo-04)、[命令参数 REF-EXO-05](#ref-exo-05)、[取消入口 REF-EXO-01](#ref-exo-01)。

本项目用`ASSIST`表示Amplify动作有效，另行记录“当前是否助推”，不新增顶层状态。这是本项目的状态表达选择，不是ExoKit已有的七状态实现。

| 情况 | Amplify生命周期 | 增量输出 | 重力补偿 |
|---|---|---|---|
| 低速、停止、反向暂停或助推速度上限触发 | 动作保留，继续检查条件 | 撤掉增量 | 健康且有许可时保留 |
| 再次满足方向、速度及暂停结束条件 | 同一动作继续，无需新命令 | 按经验证的进入规则恢复 | 健康且有许可时保留 |
| Cancel或被Lock替换 | 取消并清理，不自行重启 | 撤掉增量 | 健康且有许可时保留 |
| 严重故障或安全许可撤销 | 取消动作，不自行恢复 | 禁止 | 同样撤销，不承诺电机已物理停止 |

正常退出只指退出当前助推，不等于取消Amplify。起步速度、低速处理时序、反向暂停和有效最高助推速度优先适配ExoKit逻辑，数值通过台架调整；系统安全速度限制独立于助推条件，不能因暂停助推而忽略故障。

编码器只能确认运动方向与速度，不能证明用户正在主动推动。需专门验证松手后是否被系统自身输出维持运动、反向时是否及时撤掉旧方向增量，以及暂停与恢复是否振荡。HX711仍只作低速辅助观察，不进入主动助力主判断。

### 5.9 正常启动与故障恢复

实现查阅：[先关闭输出再标定 REF-EXO-08](#ref-exo-08)。故障复位后的限制与重新确认是本项目新增规则，不标为ExoKit已有能力。

| 场景 | 固定规则 | 未确定项 |
|---|---|---|
| 正常冷启动 | 先保持输出关闭，再检查和必要标定；通过后到`SAFE_OFF` | 正常启动许可是否可按配置自动授予，或需要人工使能，仍为`TBD`；未定义前不得默认许可 |
| 故障后恢复、看门狗等故障复位 | 不恢复旧动作、旧使能请求；故障原因解除并重新检查后，必须重新确认才能允许出力 | 复位原因识别、跨复位/断电保留恢复限制、确认入口和执行时序`TBD` |
| 无法证明是正常启动 | 不按正常自动启动处理，保持未许可并等待重新确认 | 异常原因记录与提示方式`TBD` |

重新上电不是绕过故障确认的办法。重新确认不能代替故障原因解除和健康检查。上述规则并不强制所有正常冷启动都人工使能，也不允许在正常启动策略尚未确定时自行自动出力。

### 5.10 软件撤销与实际停止

实现查阅：[ExoKit全局关闭 REF-EXO-07](#ref-exo-07)、[超时监测 REF-BETA-01](#ref-beta-01)、[控制循环异常 REF-GAL-01](#ref-gal-01)、[GIM停止请求 REF-GIM-01](#ref-gim-01)。

`检测故障 -> 撤销两个关节许可并取消动作 -> 程序与通信可用时提交停止/禁用请求 -> 确认驱动及机械状态`。

| 层次 | 能说明什么 | 不能据此声称什么 |
|---|---|---|
| 软件许可已撤销 | 后续正常命令被禁止，旧候选/排队命令需作废 | 电机已经收到停止命令 |
| 停止/禁用请求已提交 | 软件已尝试通过驱动链处理故障 | 断线驱动已执行、实际力矩已消失或关节不再运动 |
| 设备与台架验证 | 用有效反馈和独立观察核对停止出力、残余运动及负载承托 | 仅凭命令发送成功即可证明安全 |

严重异常按两个关节一起撤销输出处理。GDS68、C620在断线、最后命令保持和MCU复位期间的实际行为、超时参数及使能状态必须分别查证并验证，当前均不得假定已具备自动停机保障。台架须保留可用的独立停止手段和负载承托；失去电机力矩不等于机械制动，也可能导致负载下落。停止手段形式、有效性和承托能力均待确认，不在本轮文档修订中宣称测试通过。

### 5.11 Reference依据与适配差异

| 本地参考源码 | 实际提供 | 本项目保留或新增 |
|---|---|---|
| ExoKit `CmdReactionRunner.cpp` | 启动新动作前取消旧动作 | 保留生命周期；新增唯一输出入口，不照搬Reaction直接操作执行器 |
| ExoKit `AmplifyReaction.cpp` | 方向/速度判断、反向暂停、条件不满足时关闭扭矩；`finishCondition()`返回false | 保留判断思路；改为正常暂停仅撤增量，不关闭获许可的重力基线 |
| ExoKit `JointLockReaction.cpp` | 切位置模式、记录当前位置；退出恢复原状态 | 保留保持语义；故障时禁止清理流程恢复旧输出，控制模式另行适配 |
| GALAXEA-A1Z `a1z/robots/arm_robot.py` | 合成动力学前馈与附加力矩，同时发送位置、速度、kp、kd和力矩字段 | 借鉴类型分离与输出顺序；不能据此认定所有保持控制都在主机转成力矩；其角度越界检查部分仅告警，不照搬为本项目安全保证 |
| BetaRobot `drv/drv_daemon/drv_daemon.c` | 在线/超时检测及回调 | 借鉴健康监测，不等同于不可绕过的安全门或断线后的硬件停机保证 |

源码根目录见[借鉴索引](../../Open-source%20projects/开源项目借鉴索引表.md)。以上为当前本地源码审阅结论，不表示这些项目已实现本项目的故障重启保护，也不表示已在本项目硬件验证。

<a id="implementation-reference"></a>

### 5.12 实现时的Reference索引

以下编号用于源码导航，不增加或替代26个`XDOM-*`接口编号。文件链接指向本地参考副本；以函数名定位，避免行号随更新失效。先看“可借鉴部分”，再看“必须改写/不能据此认定”的边界，不把参考实现直接当成本项目验证结果。

#### 五个问题的阅读顺序

| 要实现的规则 | 先读ExoKit | 再读其他reference | 本项目需补齐与验证 |
|---|---|---|---|
| 唯一最终输出入口（5.1、6.1） | [REF-EXO-01](#ref-exo-01)、[REF-EXO-02](#ref-exo-02)、[REF-EXO-03](#ref-exo-03) | [REF-GAL-01](#ref-gal-01)、[REF-BETA-02](#ref-beta-02) | 新增统一提交权限；验证Reaction、Cancel、调试和故障清理都不能旁路，撤销后不重放旧命令 |
| 功能输出类型与合成（5.7） | [REF-EXO-04](#ref-exo-04)、[REF-EXO-06](#ref-exo-06) | [REF-GAL-01](#ref-gal-01)、[REF-GAL-02](#ref-gal-02)、[REF-BETA-02](#ref-beta-02)、[REF-GIM-01](#ref-gim-01) | 确认驱动能力、单位、符号、控制环部署；验证互斥、量纲和限幅，不照搬参数 |
| 断线或卡死后的停止（5.10） | [REF-EXO-07](#ref-exo-07) | [REF-BETA-01](#ref-beta-01)、[REF-GAL-01](#ref-gal-01)、[REF-GIM-01](#ref-gim-01) | 软件请求与实际停止分开；查证两驱动超时行为，再进行有承托的台架故障验证；本轮未执行 |
| 故障重启不自动出力（5.9） | [REF-EXO-08](#ref-exo-08) | 以上参考没有提供已确认适配本项目的完整实现 | 本项目新增复位分类、恢复限制与重新确认；正常开机策略仍TBD；验证旧动作/旧许可不能恢复 |
| 暂停助推与取消（5.8） | [REF-EXO-04](#ref-exo-04)、[REF-EXO-05](#ref-exo-05)，再看[REF-EXO-01](#ref-exo-01) | 不需要另造动作框架 | 测试停止、反向、重新起步、Cancel、替换、故障；正常暂停留基线，故障全部撤销 |

#### 源码入口与借鉴边界

| 编号 | 可点击源码文件 | 函数或定位点 | 可借鉴部分 | 必须改写/不能据此认定 |
|---|---|---|---|---|
| <a id="ref-exo-01"></a>`REF-EXO-01` | [CmdReactionRunner.cpp](../../Open-source%20projects/ExoKit-main/Arduino%20API/lib/exokit_api/src/com/serialreactions/CmdReactionRunner.cpp) | `CmdReactionRunner::launchReaction()`、`cancelReaction()` | 新动作先取消旧动作，Event与Reaction清理 | 保留生命周期；动态分配和调度需适配；此入口不覆盖所有电机访问，不等于安全门 |
| <a id="ref-exo-02"></a>`REF-EXO-02` | [Event.cpp](../../Open-source%20projects/ExoKit-main/Arduino%20API/lib/exokit_api/src/event/Event.cpp) | `Event::performRunning()`、`cancel()` | Condition触发、运行、取消与结束状态 | 只借鉴动作流程，不把Event状态当系统安全状态；配套阅读IReaction |
| <a id="ref-exo-03"></a>`REF-EXO-03` | [IReaction.cpp](../../Open-source%20projects/ExoKit-main/Arduino%20API/lib/exokit_api/src/event/reaction/IReaction.cpp) | `doRunLoop()`、`notifyCancel()`、`notifyFinish()`、`evalFinishCondition()` | 动作开始、循环、结束与取消的封装 | 迁移时保留语义，清理不得直接恢复执行器旧输出；故障优先于恢复 |
| <a id="ref-exo-04"></a>`REF-EXO-04` | [AmplifyReaction.cpp](../../Open-source%20projects/ExoKit-main/Arduino%20API/lib/exokit_api/src/event/concept/EffortModulation/AmplifyReaction.cpp) | `runLoop()`、`finishCondition()`、`startup()`、`shutdown()`、`configure()` | 方向、起始速度、反向暂停、低速与最高速度判断；finishCondition返回false | 正常暂停改为撤增量，不关闭重力基线；不照搬Dynamixel模式、电流比例、目标范围或退出时恢复扭矩 |
| <a id="ref-exo-05"></a>`REF-EXO-05` | [JointAmplifyCmd.cpp](../../Open-source%20projects/ExoKit-main/Arduino%20API/lib/exokit_api/src/com/serialreactions/cmd/JointAmplifyCmd.cpp) | `JointAmplifyCmd`参数解析与创建AmplifyReaction的代码 | 外部助力命令的参数及动作入口 | 源代码最高速度默认值为0，runLoop有启用条件；不能把参考默认值当成已启用超速保护，阈值与单位须重定 |
| <a id="ref-exo-06"></a>`REF-EXO-06` | [JointLockReaction.cpp](../../Open-source%20projects/ExoKit-main/Arduino%20API/lib/exokit_api/src/event/reaction/JointLockReaction.cpp) | `startup()`、`finishCondition()`、`shutdown()` | 进入时保持当前位置；退出时恢复原状态 | 只保留通电位置保持语义；位置控制环部署TBD，故障清理不能恢复旧模式/扭矩 |
| <a id="ref-exo-07"></a>`REF-EXO-07` | [ExoskeletonHandle.cpp](../../Open-source%20projects/ExoKit-main/Arduino%20API/lib/exokit_api/src/exo/ExoskeletonHandle.cpp) | `ExoskeletonRomKillSwitchExecutable::exec()`、`checkJoint()`；`ExoskeletonEmergencyBtnExecutable::exec()`、`shutdownJoint()` | 越界或按钮触发后，遍历主动关节并关闭扭矩 | 本项目严重故障作用于右臂两个关节；不移植左臂/SHOULDER_BACK或exit(1)停机方式；软件发送依赖程序与通信仍可用 |
| <a id="ref-exo-08"></a>`REF-EXO-08` | [ExoskeletonCalibrator.cpp](../../Open-source%20projects/ExoKit-main/Arduino%20API/lib/exokit_api/src/exo/builder/ExoskeletonCalibrator.cpp) | 构造函数、`calibrate()`、`isPersistedConfigAvailable()` | 标定前关闭主动关节输出；加载配置或执行tare | 仅借鉴先关输出再检查/标定；Arduino EEPROM与阻塞等待重写；不是故障复位锁定的现成实现 |
| <a id="ref-gal-01"></a>`REF-GAL-01` | [arm_robot.py](../../Open-source%20projects/GALAXEA-A1Z/a1z/robots/arm_robot.py) | `JointCommand`、`ArmRobot._update()`、`_control_loop()`、`_check_runtime_safety()`、`_check_runtime_joint_limits()` | 带类型命令、前馈力矩合成、位置/速度/增益分字段发送、异常处理 | 六轴Python/Pinocchio流程只作设计参考；保持环不全部在主机折算为力矩；运行角度越界部分仅告警，不能照搬为安全保证 |
| <a id="ref-gal-02"></a>`REF-GAL-02` | [gravity_model.py](../../Open-source%20projects/GALAXEA-A1Z/a1z/dynamics/gravity_model.py) | `GravityModel.compute_gravity_torque()`、`compute_inverse_dynamics()` | 由关节状态和模型得到Nm力矩；静态重力与完整逆动力学的区别 | 重建本项目双关节模型，不能复制其URDF、质量、参数或补偿系数 |
| <a id="ref-beta-01"></a>`REF-BETA-01` | [drv_daemon.c](../../Open-source%20projects/BetaRobot/drv/drv_daemon/drv_daemon.c) | `DaemonReload()`、`DaemonIsOnline()`、`DaemonTask()` | 在线更新、超时计数及掉线回调 | 只复用监测组织；不是独立硬件停机，也不是唯一输出权限的实现 |
| <a id="ref-beta-02"></a>`REF-BETA-02` | [drv_djimotor_broadcast.c](../../Open-source%20projects/BetaRobot/drv/drv_motor/drv_djimotor/drv_djimotor_broadcast.c) | `DJIMotorBroadcast_Calculate()`、`DJIMotorBroadcast_SetRef()`、`DJIMotorBroadcast_Disable()`、`DJIMotorBroadcast_Send()` | 位置/速度外环、力矩到电流/报文值换算、禁用状态与组帧发送 | 核对裸M3508、C620的参数与量纲；计算与发送需按本项目权限分开；写入禁用标志不证明电机已停止 |
| <a id="ref-gim-01"></a>`REF-GIM-01` | [gim6010_drv.c](../../Open-source%20projects/Rhino-Pi-X1-ROS2HomeRobots/Code/STM32/ros2-wheel-foot-car/User/Devices/SW_Motor/gim6010_drv.c) | `SWMitControl()`、`SWSetControllMode()`、`SWSetAxisState()`、`SWEstop()`、`GIM6010_fbdata()` | 控制字段封装、轴状态、停止请求和反馈解析的适配入口 | 这是第三方代码而非本机GDS68协议证明；不得照搬ffTorque乘8、ID、范围或模式；SWEstop仍依赖CAN |

#### 使用与追溯

- 核对范围：2026-09-24当前本地源码。Git子模块以主仓库记录的提交为基线；ExoKit是目录快照，不推断其上游版本。更新参考源码后，须重新核对本表结论。
- 本表覆盖上述五项规则及必要驱动入口，不表示已经完成任何模块移植。
- 实现时在对应模块记录使用的`REF-*`编号、原文件/函数、实际采用版本和改写点；不要只写“参考ExoKit”。
- 软件验证先用单元测试或模拟适配器检查候选值、调用次数、取消/故障顺序；设备协议与停止行为另外核验。本表“需验证”均为待做事项，不是通过记录。
- 官方硬件证据入口见[硬件资料](../Hardware_information_list/hardware_components_reference.md)和[接口登记表](../Hardware_information_list/hardware_interface_registry.md)。第三方驱动存在某函数，并不能证明本机固件支持相同协议或失效行为。

## 6. 嵌入式软件依赖

本图回答：四层软件分别拥有什么职责，依赖方向如何避免动作逻辑绕过硬件抽象和安全门？

![嵌入式软件依赖](architecture_diagrams/embedded_software_dependencies.svg)

```mermaid
flowchart TB
    L4["L4 系统应用层<br/>ExoskeletonSystem + RightArm<br/>启动 / 校准 / 系统状态机<br/>动作管理器 / 文本命令 / 裸机任务表<br/>健康监测 / SafetySupervisor / 遥测"]
    L3["L3 领域与控制层<br/>RIGHT/SHOULDER_SIDE + RIGHT/ELBOW<br/>Joint / JointController / JointState<br/>Condition / Event / Reaction<br/>重力补偿 / Amplify / Lock / 合成与限制"]
    L2["L2 设备与通信驱动层<br/>GIM6010-8/GDS68 适配器<br/>M3508/C620 适配器<br/>CAN分发 / UART环形缓冲<br/>HX711低速采集 / 持久化适配"]
    L1["L1 STM32平台层<br/>HAL / CMSIS / 时基 / 中断<br/>CAN / UART-DMA / GPIO / Flash / 看门狗"]
    REQUEST["外部命令<br/>XDOM-CTRL-01"]
    REQUEST -->|"动作请求"| L4
    L4 -->|"动作生命周期、状态许可、调度"| L3
    L3 -->|"只读设备状态接口，不提交执行命令"| L2
    L2 -->|"只使用平台机制"| L1
    L4 ==>|"唯一最终输出入口使用执行接口 / XDOM-CTRL-08"| L2

    classDef application fill:#fde8e7,stroke:#a93226,stroke-width:2px,color:#17202a;
    classDef domain fill:#e5f4ef,stroke:#24735a,stroke-width:2px,color:#17202a;
    classDef driver fill:#fff1d6,stroke:#a15c00,stroke-width:2px,color:#17202a;
    classDef platform fill:#e8eef5,stroke:#355070,stroke-width:2px,color:#17202a;
    class L4,REQUEST application;
    class L3 domain;
    class L2 driver;
    class L1 platform;
```

### 6.1 文字关系图

本图只表达模块依赖，不混画反馈数据流。依赖方向只向下：`L4系统应用 -> L3领域与控制 -> L2设备与通信驱动 -> L1 STM32平台`；L4中的唯一最终输出入口另依赖L2执行接口。这是明确限定的向下跨层调用，不是要求每次调用都经过相邻层。L3读取L2状态，但没有提交电机命令的权限。下层不得反向调用上层动作或通过全局可写变量绕过边界。

运行数据流另述：`UART/DMA字节流 -> 环形缓冲 -> L4固定文本解析 -> 动作管理器 -> RightArm/Joint Reaction -> L3候选请求`。L4调度L3计算并取得受限候选（`XDOM-CTRL-07`），再经`唯一最终输出入口（SafetySupervisor检查） -> L2执行器适配器`（`XDOM-CTRL-08`）。计算返回值及上层读取下层状态，不等于下层反向依赖上层。

### 6.2 软件依赖表

| 来源 | 目标 | 方向 | 交换内容 | 失效处理 | 验证状态 |
|---|---|---|---|---|---|
| L4系统应用 | L3领域与控制 | 上层调用下层接口 | 状态许可、动作生命周期、调度调用 | 禁止应用层直接访问CAN字段 | `CONFIRMED`架构 |
| L3领域与控制 | L2设备适配 | 只读统一状态接口 | 状态读取，不提交候选执行请求给驱动 | Reaction、控制器与合成器均不得直接写执行器 | `CONFIRMED`架构 |
| L4唯一最终输出入口 | L2设备适配 | 向下调用执行接口，`XDOM-CTRL-08` | 检查许可、健康与限制后提交命令或停止/禁用请求 | 其他调用者无发送权限；拒绝旧命令重放 | 规则`CONFIRMED`，代码隔离方式`TBD` |
| L2设备适配 | L1平台 | 驱动使用平台机制 | CAN、UART、GPIO、Flash和时间 | 中断只收发数据和设置就绪标志 | `CONFIRMED`架构 |
| L4遥测与安全 | L3/L2公开状态接口 | 上层读取下层；数据返回上层 | 时间戳、有效性、健康和故障 | 不允许遥测直接控制驱动 | `CONFIRMED`架构 |

### 6.3 层级职责

| 层 | 拥有的职责 | 明确不拥有的职责 |
|---|---|---|
| L4 | 启动检查、校准、状态机、动作管理、裸机任务表、遥测、健康监测和最终安全门 | 设备协议解析、重力补偿数学和直接CAN输出 |
| L3 | ExoKit领域对象、JointState、重力补偿、Amplify、Lock、命令合成和平滑限幅 | CAN/UART/Flash细节、最终安全许可 |
| L2 | CAN分发、串口缓冲、两个执行器适配器、原始状态、HX711采集和持久化适配 | 系统模式、Reaction生命周期和安全状态 |
| L1 | HAL/CMSIS、时基、中断、外设、Flash与看门狗平台机制 | 关节、动作和助力语义 |

### 6.4 ExoKit继承与替换

| 保留 | 替换或新增 |
|---|---|
| `ExoskeletonSystem -> RightArm -> Joint`对象归属 | Arduino/Dynamixel替换为STM32F446RE和两个异构执行器适配器 |
| `Condition / Event / Reaction`和开始、循环、取消、清理语义 | TaskManagerIO和动态对象替换为静态裸机任务表与静态动作集合 |
| 新动作取消旧动作、Lock与Cancel行为语义 | 新增持续重力补偿、关节命令合成、系统状态机和最终安全门 |
| ExoKit文本命令名称和右臂/语义关节参数 | 固定缓冲解析器；拒绝`left`和`shoulderback`组合 |

### 6.5 边界与 TBD

- 已确认：不使用RTOS；各周期模块独立调度；中断不执行控制算法；只持久化两个关节零位。
- `TBD`：任务周期、缓冲区尺寸、协议字段、Flash布局、看门狗时限和所有控制参数。

## 7. 跨视图映射

| 领域对象 | 机械 | 电气与反馈 | 软件与控制 | 最终权限 |
|---|---|---|---|---|
| `RIGHT/SHOULDER_SIDE` | 台架固定座 -> GIM6010-8关节 -> 上臂刚性组件 | GDS68/GIM6010-8 -> CAN-01 -> 执行器适配器 -> JointState | 肩侧重力分量、Amplify、Lock、合成与限制 | `SafetySupervisor`许可后返回肩侧适配器 |
| `RIGHT/ELBOW` | 上臂刚性组件 -> ELBOW -> 前臂链节和臂箍 | C620/裸M3508 -> CAN-02 -> 执行器适配器 -> JointState | 肘部重力分量、Amplify、Lock、合成与限制 | `SafetySupervisor`许可后返回肘部适配器 |

## 8. 集中 TBD 与验证门禁

| 类别 | 尚未确定 | 解除条件 |
|---|---|---|
| 机械 | ELBOW减速、输出轴、轴承、限位、强度、载荷和活动范围 | CAD审查、装配检查和分级静载/低输出台架测试 |
| 接线 | MCU引脚、外设实例、连接器、线束、保险、终端和物理急停 | 依据硬件接口登记表完成实物识别并由用户确认 |
| 通信 | GDS68/C620设备ID、字段、倍率、方向、周期和超时 | 官方资料核对、只解析反馈、断连测试 |
| 控制 | 零位、正方向、传动比、重力参数、滤波、限幅和增益 | 固定基座、低输出、小角度和已知负载试验 |
| 安全 | 急停是否物理切断24 V、撤销时序和故障恢复方式 | 确定安全硬件并完成故障注入测试 |
| 人体 | 对齐、绑缚、舒适性、快速释放和允许载荷 | 台架阶段完成后另行设计；本蓝图不构成验证 |

## 9. 证据与复用追踪

- ExoKit：保留右臂/Joint对象、语义关节名、Reaction生命周期、文本命令和下游机械关系；不移植Arduino、Dynamixel、TaskManagerIO和动态内存实现。
- OpenExo：参考`Exo -> Side -> Joint -> Controller/Motor/Sensor`领域分层及校准流程组织。
- UICRM-Embedded与BetaRobot：参考统一电机接口、CAN分发、M3508/C620状态组织、设备在线与超时监测。
- GALAXEA-A1Z：参考重力基线、附加控制项、统一合成和逐关节限制的顺序；不复用其参数。
- StateSmith：参考系统状态机表达和流程结构，不由其决定本项目状态语义。
- STM32CubeF4、LwRB、CMSIS-DSP及设备官方资料：分别用于平台机制、静态缓冲、必要滤波和硬件事实；具体采用范围以开源索引及对应Wayfinder票据为准。

任何参考项目中的参数、接线、协议字段和测试结果都不能自动成为本项目事实。
