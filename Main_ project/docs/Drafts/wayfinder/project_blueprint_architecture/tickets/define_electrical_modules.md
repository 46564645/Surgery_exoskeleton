---
title: 划分驱动供电与通信模块
type: grilling
status: closed
parent: ../map.md
blocked_by:
  - define_architecture_viewpoints
claimed_by: codex
---

## Question

如何划分控制器、驱动器、电机、电源、通信链路、反馈信号和辅助传感，使电气模块职责清晰，并且不虚构尚未确认的接线、保护器件或接口参数？

## ExoKit structure retained first

ExoKit 的具体 Arduino、Dynamixel Shield 和 Dynamixel 总线不能用于本项目，但其电气对象归属继续作为本项目的第一结构参考：

- `Exoskeleton -> RightArm -> Joint`仍是硬件能力的上层归属关系；每个主动 Joint 对外提供统一的位置、速度、使能和输出能力，不让 Reaction 接触具体总线。
- `ExoskeletonJointActiveHandle`把执行器反馈和命令封装在 Joint 内部的思想保留；其 `DynamixelShield + dxl_id`实现分别替换为 `SHOULDER_SIDE`和`ELBOW`执行器适配器。
- `ArmBuilder`允许每个 Joint 关联可选HX711负载传感器的思想保留；本项目的一套FORCE-01 + ADC-01固定10 SPS，只作低速辅助监测，不参与编码器触发助力的主判断。
- ExoKit 的串口命令入口保留为整机外部命令与状态接口；Arduino Serial 替换为 NUCLEO-F446RE 的 UART/VCP 适配。
- ExoKit 的系统级急停输入和关节范围关闭输出思想保留，但其“GPIO 触发后软件关闭 Dynamixel torque”不能直接视为本项目完整急停。最终是否切断动力电源以及所需器件由安全票据决定。

已核对的 ExoKit 位置：`ExoskeletonJointHandle.h`、`ExoskeletonJointActiveHandle.h`、`ArmBuilder.h`、`ExoskeletonBuilder.h`、`ExoskeletonHandle.cpp`和`ForceCondition.cpp`。

## Hardware-adapted electrical modules

| 模块 | 当前硬件与职责 | 保留的 ExoKit 结构 | 当前边界 |
|---|---|---|---|
| 主控制器 | NUCLEO-F446RE / STM32F446RET6，运行右臂系统、两个 Joint、通信和安全监督 | 替代 Arduino 控制器，但继续承载 Exoskeleton/Arm/Joint 对象 | 具体引脚、CubeMX 外设实例和中断优先级为 `TBD` |
| `SHOULDER_SIDE`执行链 | GDS68 驱动 GIM6010-8；驱动器反馈作为该 Joint 的原始位置、速度和设备状态来源 | 替代一个 ExoKit 主动 Joint 的 Dynamixel 执行器 | GDS68 报文字段、设备 ID、方向和零位必须依据官方手册与实物验证 |
| `ELBOW`执行链 | C620 驱动裸 M3508；C620 提供转子位置、速度、电流和温度等原始反馈 | 替代另一个 ExoKit 主动 Joint 的 Dynamixel 执行器 | 不采用 P19 参数；转子反馈到关节角度的换算只能使用已确认的实际机械关系 |
| CAN 物理层 | 两块 SN65HVD230 模块分别承接两个异构驱动器的 CAN 链路 | Joint 仍只面对统一执行器接口，不知道 CAN 拓扑 | 模块实物版本、CANH/CANL、终端位置和断电电阻需接线前验证 |
| 力监测链 | 5 kg直梁式FORCE-01 -> 固定10 SPS的HX711 -> STM32采集适配器，发布低速辅助监测值 | 保留ExoKit“Joint可选load cell”关系 | 具体关联哪个Joint、安装受力路径、模块供电和MCU引脚为`TBD`；不得触发主助力逻辑或安全动作 |
| 外部命令与遥测 | PC/调试工具通过 ST-LINK VCP 对应的 UART 路径发送 ExoKit 风格文本命令并接收状态 | 保留 ExoKit 串口命令语义 | 初期采用开发板调试通道；最终接口、波特率和连接器为 `TBD` |
| IMU | 当前运行架构不接入 WT901C-TTL | 无 | 两枚现有 IMU 不进入本版数据流、控制流或重力补偿输入 |

## Electrical and communication topology

当前蓝图采用两条相互独立的逻辑 CAN 链路，符合硬件清单中的两块 SN65HVD230 及当前建议连接关系：

1. `STM32 CAN controller -> CAN-01/SN65HVD230 -> GDS68 -> GIM6010-8 -> SHOULDER_SIDE Joint feedback`；
2. `STM32 CAN controller -> CAN-02/SN65HVD230 -> C620 -> M3508 -> ELBOW Joint feedback`。

该分路把两种设备协议和反馈解析限制在各自适配器内，同时继续对上层表现为两个 ExoKit 风格主动 Joint。蓝图不提前指定 `CAN1/CAN2`、GPIO 复用、过滤器编号、报文 ID、发送周期或终端安装方式；这些内容必须在资源锁定和实物核对后填写。

外部通信链为：

`PC/Serial Studio -> ST-LINK USB/VCP -> NUCLEO UART 字节流 -> 固定文本解析 -> ExoKit 风格 Arm/Joint 命令`。

辅助力信号链为：

`FORCE-01应变桥 -> 固定10 SPS的HX711 -> STM32 GPIO采集 -> 可选Joint低速力监测状态 -> 遥测/标定观察`。

## Power domains

电源按动力域和低压逻辑域分开描述，避免把同一 24 V 母线误接到逻辑器件：

- **24 V 动力域**：PWR-01（S-250-24，商品选择标称 24 V / 10 A）作为原型台架直流母线，分别为 GDS68 和 C620 提供动力来源。
- **低压逻辑域**：NUCLEO-F446RE、两块 SN65HVD230 和 HX711 所需低压供电。当前可确认 NUCLEO 可通过 ST-LINK USB 调试供电，但完整低压供电方案尚未确定，因此只保留逻辑电源接口，不假定 DC-DC 型号或接线。
- **交流输入与保护域**：PWR-01 的市电输入、保护接地、防触碰、应力释放、分支保护和急停断电接口必须存在于最终安全设计中；现有硬件清单尚未确定具体器件，因此均标记为 `TBD safety hardware`。

PWR-01 当前不能被描述为已支持两关节同时达到额定输出。硬件资料已指出两执行链的额定工作点合计可能超过 10 A；允许的并发输出必须通过限流和低负载台架测试确定。

## Open-source index adoption

| 索引模块 | 主参考与直接采用内容 | 判断与改写量 | 与 ExoKit 的关系 |
|---|---|---|---|
| 3. STM32F446 工程底座 | STM32CubeF4、LEAP upper limb 的 F446/HAL 工程组织 | 需要修改；25-40% | 只替换 ExoKit 的 Arduino 底座 |
| 5. CAN 底层与报文分发 | BetaRobot `bsp_bxcan`分层、STM32CubeF4 HAL CAN 范例 | 需要修改；30-45% | 为两个 ExoKit 风格 Joint 提供解耦通信 |
| 6. 串口 DMA 与环形缓冲 | stm32-usart-uart-dma-rx-tx、LwRB | 直接使用库并修改集成层；20-35% | 承接 ExoKit 文本命令入口 |
| 7. 统一关节执行器接口 | ExoKit Joint 职责、BetaRobot `drv_motor_base`分层 | 需要修改；35-50% | 保留 Joint 对上层隐藏电机差异的结构 |
| 8. GIM6010-8 驱动适配 | Rhino-Pi-X1 报文/反馈骨架、BetaRobot bxCAN 适配 | 需要修改；40-50% | 替换 `SHOULDER_SIDE`的 Dynamixel 实现 |
| 9. M3508 + C620 驱动适配 | BetaRobot、UICRM-Embedded 的 DJI 反馈与组帧结构 | 需要修改；20-35% | 替换 `ELBOW`的 Dynamixel 实现 |
| 10. 编码器状态与多圈累计 | BetaRobot、Rhino-Pi-X1 的状态组织 | 需要修改；30-45% | 继续向 Joint 提供位置和速度 |
| 11. 关节角度统一换算 | BetaRobot 状态层和 ExoKit 方向/零位概念 | 需要修改；35-50% | 保留 Joint 可读状态语义 |
| 12. M3508 外环控制 | BetaRobot PID 组织、CMSIS-DSP 基础函数 | 需要修改；25-40% | 把 Joint 请求转换为 C620 可执行命令 |
| 13. HX711 与力传感器采集 | STM32-Libraries HX711驱动；Auxivo仅作监测思路参考 | 需要修改；20-35% | 按固定10 SPS发布新数据，保留可选Joint低速监测但不改变编码器主逻辑 |

## Resolution

电气架构继续以 ExoKit 的 `Exoskeleton -> RightArm -> Joint`为组织主线，不以总线或电机型号重新定义上层系统。两个主动 Joint 分别映射到两条异构执行链，并通过统一关节执行器接口屏蔽差异：`SHOULDER_SIDE -> GDS68 + GIM6010-8`，`ELBOW -> C620 + 裸 M3508`。

当前基准采用两块SN65HVD230形成两条独立逻辑CAN链路；PWR-01形成共享24 V台架动力母线，低压逻辑电源作为独立域保留但方案未定；PC命令与遥测初期经ST-LINK VCP/UART；一套5 kg直梁式FORCE-01 + 固定10 SPS的HX711作为可选Joint低速辅助监测链；IMU不进入当前架构。

所有具体引脚、CAN 外设实例、设备 ID、报文字段、终端配置、低压电源、保护器件、急停断电器件和允许输出均保持 `TBD`，必须经官方资料、实物检查及后续台架验证后确定。急停后的动力处理交由“确定运行状态与安全控制权”票据，跨域能量流与控制流交由“定义跨域接口与关系”票据。

## Wiring gate

本票据只确定逻辑模块、供电域和通信关系，不授权具体接线。一旦后续工作需要填写 STM32 外设实例或引脚、连接器针脚、驱动器端子、传感器线色、CAN 端点或线束编号，必须停止并先依据 `Main_ project/hardware_list/hardware_components_reference.md`及对应官方资料建立硬件接口编码表。接口编码经用户确认后，才能继续接线映射、CubeMX 资源分配或线束设计。
