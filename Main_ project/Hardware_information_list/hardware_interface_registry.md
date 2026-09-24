# 硬件接口编码表

> 状态：型号资料级接口登记稿，尚未进行项目接线分配。
>
> 本文件是当前项目的权威接口登记。历史目录中的同名文件仅作为旧副本，不再被项目文档引用。
>
> 硬件范围：严格来自同目录 `hardware_components_reference.md`。
>
> 规则：厂家资料只证明设备本身具有什么接口；本表中的接口编码不表示已经选定 STM32 引脚、线束、CAN ID、供电支路或控制参数。

## 1. 编码与证据规则

| 对象 | 格式 | 示例 | 含义 |
|---|---|---|---|
| 设备 | `类别-序号` | `ACT-01` | 沿用 Hardware List 的设备编号 |
| 物理接口组 | `设备.IF序号` | `ACT-01.IF01` | 连接器、焊盘或固定线缆接口 |
| 接口信号 | `接口组.S序号` | `ACT-01.IF01.S01` | 接口内的一条厂家定义信号 |
| 板上操作件 | `设备.CTL序号` | `ESC-01.CTL01` | 按键、选择开关等非线缆接口 |
| 软件资源槽 | `设备.LR序号` | `MCU-01.LR01` | 尚未分配到物理引脚的 MCU 逻辑需求 |
| 电源域 | `PD-名称` | `PD-24V-MOTOR` | 具有共同电气性质的电源范围 |
| 逻辑网络 | `NET-名称` | `NET-CAN-ELBOW` | 后续连接映射使用的逻辑网络身份 |

编码只表达稳定身份。厂家端子名称可以记录，但“某信号接到另一设备的哪一针”必须在后续接线映射表中单独确认。

### 1.1 状态

| 状态 | 含义 |
|---|---|
| `CONFIRMED` | 对应型号官方资料明确给出，且型号身份已确认 |
| `REFERENCE_ONLY` | 资料来自参考板、芯片或商品页，尚不能证明到货实物完全一致 |
| `PLANNED` | 当前架构需要，但尚未分配物理资源 |
| `TBD` | 缺少型号、版本、实物标识或对应资料，不能继续细化 |
| `INACTIVE` | 设备存在，但当前编码器控制架构不使用 |

### 1.2 证据等级

| 等级 | 证据 | 可确认范围 |
|---|---|---|
| `A` | 对应型号的厂家官方手册、数据手册或官方原理图 | 可确认该型号的接口名称、信号职责和厂家限制 |
| `B` | 对应芯片数据手册或官方参考板资料 | 只能确认芯片或参考板，不能自动等同于当前模块 |
| `C` | 用户提供的商品页、订单截图或商家资料 | 只记录直接可见的信息，必须核对实物 |
| `D` | 项目规划或待验证假设 | 不能作为接线依据 |

## 2. 型号与资料绑定

| 设备 | 已确认型号 | 主资料 | 证据 | 资料边界 |
|---|---|---|---|---|
| `MCU-01` | NUCLEO-F446RE；MB1136 rev C；STM32F446RET6 | ST `UM1724 Rev 17` | A | 未确认 C03/C04，暂不使用版本专属原理图锁定焊桥差异 |
| `ACT-01` | GIM6010-8；GDS68；订单 Y-48、12–48 V 选项 | SteadyWin《GIM6010-8 使用手册 rev2.2》 | A+C | 扩展口是否存在取决于驱动板硬件版本 |
| `ESC-01` | RoboMaster C620 | DJI《C620 无刷电机调速器使用说明 V1.01》 | A | 接口以中文版第 23–25 页为准 |
| `MOTOR-01` | 裸 RoboMaster M3508 | DJI《M3508 直流无刷电机使用说明 v1.0 2025.10》 | A | 不使用 P19 减速器资料 |
| `FORCE-01` | 5 kg 直梁式四线全桥应变称重传感器 | 随购买套装提供的技术参数表、安装图和接线图 | C | 已确认所购规格与资料基准；尺寸、线色和桥阻仍需到货实测 |
| `ADC-01` | HX711-BF / HX711AD 模块；资料原理图版本 | 随购买套装原理图及 Avia《HX711 数据手册》 | B+C | 原理图确认 `RATE`接地、固定10 SPS；实物排针和PCB一致性待核对 |
| `CAN-01/02` | 商品页展示为 Waveshare SN65HVD230 CAN Board 样式 | Waveshare 参考板原理图；TI 数据手册 | B+C | 到货板是否同版尚未确认 |
| `PWR-01` | 商品选择 S-250-24、24 V/10 A | 用户提供商品截图 | C | 没有对应厂家手册或实物铭牌照片 |
| `IMU-01/02` | WitMotion WT901C-TTL | WitMotion 官方产品页、协议和教程 | A | 当前停用；排针物理顺序仍需实物核对 |

## 3. 电源域

| 编码 | 名称 | 已知范围 | 状态与边界 |
|---|---|---|---|
| `PD-AC-MAINS` | 台架交流输入域 | `PWR-01` 交流输入、保护接地和防触碰边界 | `TBD`：实物端子与输入档位未核对 |
| `PD-24V-MOTOR` | 24 V 动力域 | `PWR-01` 计划向 `ACT-01`、`ESC-01` 供电 | `PLANNED`：未设计支路、保护、开关和线径 |
| `PD-LOGIC-3V3` | 3.3 V 逻辑域 | `CAN-01/02` 和可能的 MCU 扩展逻辑 | `PLANNED`：来源和容量未分配 |
| `PD-MCU-5V` | MCU 板 5 V 域 | NUCLEO 的 USB 5 V、E5V 或板载 5 V 路径 | `PLANNED`：尚未选择供电路径 |
| `PD-SENSOR-LOW-VOLTAGE` | 传感器低压域 | `ADC-01`、`FORCE-01`；IMU 库存保留 | `PLANNED`：HX711资料标示VCC/GND，但项目供电来源尚未分配 |

## 4. 设备接口详细编码

### 4.1 MCU-01：NUCLEO-F446RE

#### 4.1.1 板级接口

| 接口编码 | 官方连接器 | 作用 | 状态 | 官方依据 |
|---|---|---|---|---|
| `MCU-01.IF01` | `CN1` USB Mini-B | ST-LINK 调试、下载、Mass Storage、VCP，并可供电 | `CONFIRMED` | UM1724 第 7.4、7.5.1 节 |
| `MCU-01.IF02` | `CN2` ST-LINK/NUCLEO 跳线 | 选择 ST-LINK 与板载目标 MCU 的连接关系 | `CONFIRMED` | UM1724 第 7.4 节、表 4 |
| `MCU-01.IF03` | `CN4` SWD | 板载 ST-LINK 调试外部目标板 | `CONFIRMED` | UM1724 第 7.4.5 节、表 5 |
| `MCU-01.IF04` | `CN5/CN6/CN8/CN9` | Arduino Uno V3 扩展接口组 | `CONFIRMED` | UM1724 第 7.13 节、表 19 |
| `MCU-01.IF05` | `CN7/CN10` | ST Morpho 扩展接口组 | `CONFIRMED` | UM1724 第 7.14 节、表 29 |
| `MCU-01.IF06` | 板载 VCP 内部路径 | 默认连接 `USART2 PA2/PA3`，受焊桥配置影响 | `CONFIRMED` | UM1724 第 7.10、7.11 节 |
| `MCU-01.IF07` | `B1` USER | 用户输入，连接 `PC13` | `CONFIRMED` | UM1724 第 7.7 节 |
| `MCU-01.IF08` | `B2` RESET | 目标 MCU 复位 | `CONFIRMED` | UM1724 第 7.7 节 |
| `MCU-01.IF09` | `LD2` USER LED | 用户输出，连接 D13 / `PA5` | `CONFIRMED` | UM1724 第 7.6 节 |

`CN4` 用于板载 ST-LINK 调试另一块目标板。UM1724 要求此时移除 CN2 两只跳线；当前不把 CN4 与板载目标调试路径同时使用。

#### 4.1.2 CN4 信号

| 编码 | 针位 | 信号 | 方向（相对 ST-LINK） |
|---|---:|---|---|
| `MCU-01.IF03.S01` | 1 | `VDD_TARGET` | 采样输入 |
| `MCU-01.IF03.S02` | 2 | `SWCLK` | 输出 |
| `MCU-01.IF03.S03` | 3 | `GND` | 参考地 |
| `MCU-01.IF03.S04` | 4 | `SWDIO` | 双向 |
| `MCU-01.IF03.S05` | 5 | `NRST` | 输出 |
| `MCU-01.IF03.S06` | 6 | `SWO` | 保留/未用 |

#### 4.1.3 可用供电入口和电源脚

| 编码 | 位置 | 功能与官方范围 | 状态 |
|---|---|---|---|
| `MCU-01.IF01.S01` | `CN1` | USB 5 V 路径，板及扩展负载受 UM1724 USB 电流限制约束 | `CONFIRMED` |
| `MCU-01.IF04.S01` | `CN6 pin 8` | `VIN`，外部 7–12 V 输入 | `CONFIRMED` |
| `MCU-01.IF05.S01` | `CN7 pin 24` | `VIN` | `CONFIRMED` |
| `MCU-01.IF05.S02` | `CN7 pin 6` | `E5V`，外部 4.75–5.25 V 输入 | `CONFIRMED` |
| `MCU-01.IF04.S02` | `CN6 pin 4` | `+3V3` | `CONFIRMED` |
| `MCU-01.IF04.S03` | `CN6 pin 5` | `+5V` | `CONFIRMED` |
| `MCU-01.IF04.S04` | `CN6 pin 6/7` | `GND` | `CONFIRMED` |

本表不选择 USB、VIN、E5V 或 3.3 V 中的任何一种作为最终方案，也不允许并联多个入口。

#### 4.1.4 待分配资源槽

| 编码 | 用途 | 所需能力 | 状态 | 暂不填写 |
|---|---|---|---|---|
| `MCU-01.LR01` | `SHOULDER_SIDE` | 1 路 CAN TX/RX | `PLANNED` | CAN1/2、GPIO、AF、IRQ、滤波器 |
| `MCU-01.LR02` | `ELBOW` | 1 路 CAN TX/RX | `PLANNED` | CAN1/2、GPIO、AF、IRQ、滤波器 |
| `MCU-01.LR03` | 命令与遥测 | 1 路串口，可评估 VCP | `PLANNED` | USART、波特率、DMA |
| `MCU-01.LR04` | HX711 | 数字输出 `PD_SCK`、数字输入 `DOUT` | `PLANNED` | GPIO、上下拉、周期 |
| `MCU-01.LR05` | 未来 `assist_enable_permission` | 安全定义的数字输入 | `PLANNED` | 硬件、GPIO、电平、去抖 |

### 4.2 ACT-01：GIM6010-8 + GDS68

`ACT-01` 绑定右臂 `SHOULDER_SIDE`。接口来自 GDS68，不表示全部启用。

| 编码 | 手册序号 | 连接器/形式 | 功能 | 状态 | 依据 |
|---|---:|---|---|---|---|
| `ACT-01.IF01` | 1 | 板端 `XT30PB(2+2)-M`；线端 `XT30(2+2)-F` | 电源与 CAN/复用通信 | `CONFIRMED` | rev2.2 第 2.4.1 节，第 10 页 |
| `ACT-01.IF02` | 2 | USB Type-C | 调试与上位机通信，不给驱动器供电 | `CONFIRMED` | 第 2.4.2、3.1 节 |
| `ACT-01.IF03` | 3 | 20-pin 扩展插槽 | SPI/USART/I2C/PWM/ADC/GPIO | `TBD` | 第 2.4.3 节；硬件 v3.9+ 无此口 |
| `ACT-01.IF04` | 4 | 2.00 mm 焊盘 | SWD 调试/下载 | `CONFIRMED` | 第 2.4.4 节，第 11–12 页 |
| `ACT-01.IF05` | 5 | 2.00 mm 焊盘 | 第二编码器 USART/I2C | `CONFIRMED` | 第 2.4.5 节，第 12 页 |
| `ACT-01.IF06` | 6 | 焊盘 | 电机 10 kΩ NTC | `CONFIRMED` | 第 2.4.6 节，第 12 页 |
| `ACT-01.IF07` | 7a | 2.00 mm 焊盘 | 抱闸或制动/泄放电阻 | `CONFIRMED` | 第 2.4.7 节，第 13 页 |
| `ACT-01.IF08` | 7b | 2.00 mm 焊盘 | 最小/最大限位及 12 V 辅助电源 | `CONFIRMED` | 第 2.4.8 节，第 13–14 页 |
| `ACT-01.IF09` | U/V/W | 三相绕组焊接孔 | GIM6010-8 三相绕组 | `CONFIRMED` | 第 2.2 节，第 9 页 |

#### 4.2.1 电源与 CAN

| 编码 | 信号 | 功能 | 项目状态 |
|---|---|---|---|
| `ACT-01.IF01.S01` | `VBUS` | 直流母线正端 | `PLANNED` |
| `ACT-01.IF01.S02` | `GND` | 直流母线负端/参考地 | `PLANNED` |
| `ACT-01.IF01.S03` | `CANH` | CAN 高线；其他模式可复用 | `PLANNED`：只计划 CAN |
| `ACT-01.IF01.S04` | `CANL` | CAN 低线；其他模式可复用 | `PLANNED`：只计划 CAN |

GDS68 手册明确说明驱动板没有电源反接保护。本表把它记录为接线验证约束，不据此新增未选型的保护器件。

#### 4.2.2 调试与辅助信号

| 编码 | 信号 | 功能 | 状态 |
|---|---|---|---|
| `ACT-01.IF04.S01` | `SWDIO` | SWD 数据 | `CONFIRMED` |
| `ACT-01.IF04.S02` | `GND` | 调试参考地 | `CONFIRMED` |
| `ACT-01.IF04.S03` | `SWDCLK` | SWD 时钟 | `CONFIRMED` |
| `ACT-01.IF04.S04` | `3.3V` | 调试电平参考 | `CONFIRMED` |
| `ACT-01.IF05.S01` | `GND` | 第二编码器参考地 | `CONFIRMED` |
| `ACT-01.IF05.S02` | `3.3V` | 第二编码器供电 | `CONFIRMED` |
| `ACT-01.IF05.S03` | `TX/SCL` | USART TX 或 I2C SCL | `CONFIRMED` |
| `ACT-01.IF05.S04` | `RX/SDA` | USART RX 或 I2C SDA | `CONFIRMED` |
| `ACT-01.IF06.S01` | `MOT_TEMP` | 10 kΩ NTC 信号 | `CONFIRMED` |
| `ACT-01.IF06.S02` | `GND` | 温度参考地；手册称两线无极性 | `CONFIRMED` |
| `ACT-01.IF07.S01` | `EMG` | 抱闸电流输出或制动电阻端 | `CONFIRMED`：未启用 |
| `ACT-01.IF07.S02` | `GND` | 抱闸/制动回路参考端 | `CONFIRMED`：未启用 |
| `ACT-01.IF08.S01` | `GND` | 限位参考地 | `CONFIRMED`：未启用 |
| `ACT-01.IF08.S02` | `12V` | 限位开关辅助供电 | `CONFIRMED`：未启用 |
| `ACT-01.IF08.S03` | `LW1` | 最小位置限位输入 | `CONFIRMED`：未启用 |
| `ACT-01.IF08.S04` | `LW2` | 最大位置限位输入 | `CONFIRMED`：未启用 |

限位口允许两线开关或三线 NPN 开关，但项目未选定开关。20-pin 扩展口的板端为 `X0812FVS-20CS-9TV01`，扩展板端为 `X0812WVS-20AS-9TV01`；因 ACT-01 板版本未知，不为其内部信号建立项目连接。

### 4.3 ESC-01：RoboMaster C620

`ESC-01` 与 `MOTOR-01` 共同绑定右臂 `ELBOW`。

| 编码 | 官方接口 | 功能 | 状态 | 依据 |
|---|---|---|---|---|
| `ESC-01.IF01` | 7-Pin 电机数据端口 | 与 M3508 数据交互 | `CONFIRMED` | C620 中文第 23、25 页 |
| `ESC-01.IF02` | 三相动力线接头 | 向 M3508 输出三相动力 | `CONFIRMED` | 第 23、25 页 |
| `ESC-01.IF03` | 兼容 XT30 的电源线 | 24 V 输入 | `CONFIRMED` | 第 24 页 |
| `ESC-01.IF04` | CAN 信号端口 | 1 Mbps CAN 控制与反馈 | `CONFIRMED` | 第 24–25 页 |
| `ESC-01.IF05` | PWM 信号端口 | PWM 控制或配置/升级 | `CONFIRMED` | 第 24–25、27 页 |
| `ESC-01.CTL01` | 终端电阻开关 | 接入/断开板载 120 Ω | `CONFIRMED` | 第 24 页 |
| `ESC-01.CTL02` | SET 按键 | 设置 ID、快速编号、电机校准 | `CONFIRMED` | 第 24、30–32 页 |

| 编码 | 标记/线色 | 方向（相对 C620） | 项目状态 |
|---|---|---|---|
| `ESC-01.IF03.S01` | 电源正，XT30 极性依连接器 | 输入 | `PLANNED`：核对实物 |
| `ESC-01.IF03.S02` | 电源负，XT30 极性依连接器 | 输入 | `PLANNED` |
| `ESC-01.IF04.S01` | A，黑色，`CAN_L` | 双向总线 | `PLANNED` |
| `ESC-01.IF04.S02` | B，红色，`CAN_H` | 双向总线 | `PLANNED` |
| `ESC-01.IF05.S01` | A，黑色，`GND` | 参考地 | `CONFIRMED`：不作运行控制 |
| `ESC-01.IF05.S02` | B，灰色，`TX` | 输出 | `CONFIRMED`：不作运行控制 |
| `ESC-01.IF05.S03` | C，白色，`PWM/RX` | 输入 | `CONFIRMED`：不作运行控制 |

C620 手册禁止同时接入 CAN 与 PWM，切换方式前必须断电。本项目固定选择 CAN 运行路径。7-Pin 内部针位未在当前官方手册展开，因此不猜测。

### 4.4 MOTOR-01：裸 M3508

| 编码 | 官方接口 | 功能 | 状态 | 依据 |
|---|---|---|---|---|
| `MOTOR-01.IF01` | 7-Pin 数据端口 | 用随附线连接 C620 | `CONFIRMED` | M3508 v1.0 第 2、6 页 |
| `MOTOR-01.IF02` | 三相输入接头 | 连接 C620，相同颜色对应且不可反插 | `CONFIRMED` | M3508 v1.0 第 2、6 页 |

不添加 P19 接口。手册没有公开 7-Pin 内部针位，也未在文字中定义三相 U/V/W，因此只把原厂线束视为完整接口。M3508 的动力必须来自 C620 三相输出，不能直接接 24 V 直流母线。

### 4.5 FORCE-01：5 kg 直梁式应变称重传感器

以下信息来自随购买套装提供的5 kg技术参数表、安装图和接线图，证据等级为商品配套资料。到货后仍需核对实物。

| 编码 | 商品资料线色 | 商品资料定义 | 方向 | 状态 |
|---|---|---|---|---|
| `FORCE-01.IF01.S01` | 红 | `E+` 激励正 | 输入 | `REFERENCE_ONLY` |
| `FORCE-01.IF01.S02` | 黑 | `E-` 激励负 | 输入 | `REFERENCE_ONLY` |
| `FORCE-01.IF01.S03` | 绿 | `A+` 信号正 | 输出 | `REFERENCE_ONLY` |
| `FORCE-01.IF01.S04` | 白 | `A-` 信号负 | 输出 | `REFERENCE_ONLY` |

资料给出额定载荷5 kg、灵敏度1.0 ± 0.1 mV/V、推荐激励3–12 V以及输入/输出电阻1000 ± 50 Ω。安装采用一端固定、另一端悬空的悬臂方式；连接ADC前仍需测量桥路并通过已知方向加载确定正负方向。

### 4.6 ADC-01：HX711 模块

随购买套装资料给出了HX711-BF模块原理图和接线图。原理图明确把`RATE`（15脚）接至`GND`，因此本项目按固定10 SPS的低速采集模块登记；具体排针顺序仍需到货后按板上丝印确认。

| 编码 | 逻辑接口组 | 芯片功能 | 模块状态 |
|---|---|---|---|
| `ADC-01.IF01` | 通道 A 称重桥输入 | `E+`、`E-`、`A+`、`A-` | `REFERENCE_ONLY`：按套装接线图，待实物丝印确认 |
| `ADC-01.IF02` | 通道 B 输入 | `B+`、`B-` | `INACTIVE`：当前不使用 |
| `ADC-01.IF03` | MCU 数字接口 | `DT/DOUT`、`SCK/PD_SCK`、`GND` | `REFERENCE_ONLY`：待实物排针确认 |
| `ADC-01.IF04` | 模块供电 | `VCC`、`GND` | `REFERENCE_ONLY`：供电来源尚未分配 |
| `ADC-01.CFG01` | HX711 `RATE`硬件配置 | 15脚接`GND`，固定10 SPS | `REFERENCE_ONLY`：套装原理图已确认，实物PCB待核对 |

ADC-01只发布低速辅助力监测值，用于显示、记录、标定观察和后续研究。它不作为快速助力触发、关节主控制反馈、安全触发或高带宽力闭环输入。软件不得把重复读取旧样本描述为高于10 SPS的新测量。

| 参考编码 | HX711 针位 | 信号 | 官方功能 |
|---|---:|---|---|
| `ADC-01.IF01.S01` | 8 | `INA+` | 通道 A 正输入 |
| `ADC-01.IF01.S02` | 7 | `INA-` | 通道 A 负输入 |
| `ADC-01.IF02.S01` | 10 | `INB+` | 通道 B 正输入 |
| `ADC-01.IF02.S02` | 9 | `INB-` | 通道 B 负输入 |
| `ADC-01.IF03.S01` | 12 | `DOUT` | 数据输出；低电平表示就绪 |
| `ADC-01.IF03.S02` | 11 | `PD_SCK` | 时钟兼断电控制；高电平超过 60 µs 断电 |

依据为 Avia HX711 数据手册第 2、4–6、10 页。数据手册要求 DOUT 对应的 MCU 输入不接内部上拉/下拉；本地开源 STM32 示例 README 建议上拉，与芯片手册冲突，后续必须以芯片手册和实际模块电路为先。

### 4.7 CAN-01 / CAN-02：SN65HVD230 CAN Board 样式模块

两块模块结构相同。排针顺序来自 Waveshare 参考板原理图，实物未核对，全部为 `REFERENCE_ONLY`。

下表使用公共后缀压缩重复内容。例如 `.IF01.S01` 分别展开为 `CAN-01.IF01.S01` 和 `CAN-02.IF01.S01`，两者仍是独立且唯一的信号编码。

| 信号后缀 | 参考板针位 | 信号 | 方向（相对模块） |
|---|---:|---|---|
| `.IF01.S01` | P1-1 | `CAN_TX`，接收发器 D | 输入 |
| `.IF01.S02` | P1-2 | `GND` | 参考地 |
| `.IF01.S03` | P1-3 | `3.3V` | 电源输入 |
| `.IF01.S04` | P1-4 | `CAN_RX`，来自收发器 R | 输出 |
| `.IF02.S01` | can1-1 | `CANL` | 双向总线 |
| `.IF02.S02` | can1-2 | `CANH` | 双向总线 |

Waveshare 原理图确认：`Rs` 经 10 kΩ 接地，CANH/CANL 固定跨接 120 Ω；未显示终端使能开关、隔离器或 TVS。TI 数据手册第 5 页确认 SN65HVD230 芯片信号和 3.3 V 供电。接线前必须逐块核对芯片顶标、P1 丝印方向、CANH/CANL 丝印、3.3 V/GND 极性及断电端间电阻。

### 4.8 PWR-01：S-250-24

| 编码 | 功能组 | 状态 | 当前证据 |
|---|---|---|---|
| `PWR-01.IF01` | 交流输入与保护接地 | `TBD` | S-250-24 商品截图没有显示实物端子 |
| `PWR-01.IF02` | 24 V 直流输出 | `TBD` | 商品选项标称 24 V/10 A，端子数量/顺序未知 |
| `PWR-01.IF03` | 输出电压调节 | `TBD` | 只有另一型号 S-350-24 截图出现 `ADJ` |

当前不能给 `L/N/PE/+V/-V/ADJ` 分配信号编码：端子说明图对应 S-350-24，不是所选 S-250-24。需提供 PWR-01 到货铭牌与端子照片，或完全匹配厂家/型号的手册。

### 4.9 IMU-01 / IMU-02：WT901C-TTL

IMU 当前不进入控制架构，仅登记库存能力。

下表公共后缀分别展开到 `IMU-01` 与 `IMU-02`，例如 `.IF01.S02` 对应两个设备各自独立的 TX 信号。

| 信号后缀 | 官方信号 | 方向（相对 IMU） | 状态 |
|---|---|---|---|
| `.IF01.S01` | `VCC` | 电源输入 | `INACTIVE`：电压按实物版本复核 |
| `.IF01.S02` | `TX` | 串口输出 | `INACTIVE` |
| `.IF01.S03` | `RX` | 串口输入 | `INACTIVE` |
| `.IF01.S04` | `GND` | 参考地 | `INACTIVE` |

WitMotion 官方教程确认串口交叉连接，即传感器 TX 对主控 RX、传感器 RX 对主控 TX。当前没有本批产品排针实物证据，不登记物理针序；未来启用时须先读取当前波特率和输出配置。

## 5. 逻辑网络

| 编码 | 功能 | 端点角色 | 状态 | 尚未决定 |
|---|---|---|---|---|
| `NET-PWR-24V-MOTOR` | 24 V 动力母线 | `PWR-01.IF02`、`ACT-01.IF01`、`ESC-01.IF03` | `PLANNED` | 分支、保护、线径、连接器、同时输出能力 |
| `NET-CAN-SHOULDER-SIDE` | SHOULDER_SIDE CAN | `MCU-01.LR01`、`CAN-01`、`ACT-01.IF01` | `PLANNED` | MCU 资源、终端、线束、CAN 参数 |
| `NET-CAN-ELBOW` | ELBOW CAN | `MCU-01.LR02`、`CAN-02`、`ESC-01.IF04` | `PLANNED` | MCU 资源、终端、线束、C620 ID |
| `NET-M3508-DATA` | M3508 数据链 | `ESC-01.IF01`、`MOTOR-01.IF01`、原厂 7-Pin 线 | `CONFIRMED` | 不拆未公开针位 |
| `NET-M3508-PHASE` | M3508 三相链 | `ESC-01.IF02`、`MOTOR-01.IF02` | `CONFIRMED` | 实物线色和接头检查 |
| `NET-FORCE-BRIDGE` | 力传感器桥路 | `FORCE-01.IF01`、`ADC-01.IF01/04` | `PLANNED` | 实物端子、激励、桥阻和方向标定 |
| `NET-HX711-DIGITAL` | HX711 10 SPS低速数字采集 | `ADC-01.IF03`、`MCU-01.LR04` | `PLANNED` | 实物排针、GPIO、非阻塞读取和超时 |
| `NET-DEBUG-VCP` | 调试与遥测 | PC、`MCU-01.IF01/06`、`MCU-01.LR03` | `PLANNED` | USART2 是否占用、波特率、格式 |

## 6. 无法由型号资料确认的项目

1. `MCU-01` 背面完整版本贴纸未确认，不能在 C03/C04 原理图之间选定。
2. `ACT-01` 的 GDS68 板版本未确认，不能认定 20-pin 扩展口存在。
3. `FORCE-01`已确定购买5 kg规格，但缺少到货实物、厂家型号和铭牌，线色、桥阻及尺寸仍需实测。
4. `ADC-01`已有套装原理图并确认固定10 SPS，但缺少到货PCB正反面照片，不能把资料排针顺序直接视为实物事实。
5. `CAN-01/02` 未核对到货板，Waveshare 参考板针序不能视为实物事实。
6. `PWR-01` 缺少对应厂家 S-250-24 手册、铭牌和端子照片。
7. `IMU-01/02` 当前停用且缺少本批排针照片，不登记物理针序。

## 7. 接线设计前的实物核对

| 对象 | 必须获取或测量 | 解锁工作 |
|---|---|---|
| `MCU-01` | C03/C04 贴纸、当前焊桥/跳线 | CubeMX 资源锁定 |
| `ACT-01` | GDS68 板版本、端子朝向、CAN 模式与配置 | SHOULDER_SIDE 线束 |
| `ESC-01` | XT30 极性、终端开关、ID、CAN/PWM 未同时接入 | ELBOW 线束 |
| `MOTOR-01` | 原厂 7-Pin 与三相线完好、同色对应 | C620-M3508 配套确认 |
| `FORCE-01` | 铭牌/订单型号、5 kg规格、尺寸、线色、桥阻和已知方向加载 | 接HX711并完成方向标定 |
| `ADC-01` | PCB正反面、丝印、芯片顶标，并核对`RATE`确实接地 | 两侧针位编码和10 SPS采样验证 |
| `CAN-01/02` | 正反面、芯片顶标、丝印、断电总线电阻 | CAN 接线与终端 |
| `PWR-01` | 铭牌、输入选择、端子排和丝印 | 交流与 24 V 支路 |
| `IMU-01/02` | 未来启用时核对排针、供电版本和配置 | IMU 资源分配 |

## 8. 本轮不做的决定

- 不指定 STM32 的 CAN1/CAN2、USART、GPIO、AF、DMA、中断和定时器。
- 不把 CAN 报文 ID、波特率、控制周期或限值写成最终参数。
- 不设计 24 V 分线、保险、急停、接触器、DC-DC、保护地和线束。
- 不添加 Hardware List 中不存在的电磁铁、吸附传感器或保护器件。
- 不把商品信息、参考板原理图或开源示例提升为已验证实物结论。

## 9. 资料索引

| 编号 | 本地资料 | 用途 |
|---|---|---|
| `SRC-MCU-01` | `official_documents/um1724-stm32-nucleo64-boards-mb1136-stmicroelectronics.pdf` | NUCLEO 接口、供电、VCP、按键和 LED |
| `SRC-ACT-01` | `official_documents/ABUIABA9GAAgzpi6wQYoh9D9CQ.pdf` | GIM6010-8/GDS68 rev2.2 第 2.2–2.4 节 |
| `SRC-ESC-01` | `official_documents/RoboMaster_C620_Brushless_DC_Motor_Speed_Controller_V1.01.pdf` | C620 接口、线色、终端和限制 |
| `SRC-MOTOR-01` | `official_documents/RoboMaster_M3508_Brushless_DC_Motor_User_Guide_v1.0_2025.10.pdf` | 裸 M3508 接口 |
| `SRC-ADC-01` | `official_documents/电子秤HX71..料包/电子秤HX711AD模块资料包/hx711(海芯).pdf` | HX711 芯片信号与时序 |
| `SRC-FORCE-01` | `official_documents/拉力传感器资料/2.设计开发原理说明/技术参数表.png`及同目录安装、接线图 | 5 kg传感器参数、尺寸、安装与线色 |
| `SRC-ADC-REF` | `official_documents/电子秤HX71..料包/电子秤HX711AD模块资料包/ADC模块原理图.pdf`及`official_documents/拉力传感器资料/6.HX711AD模块原理图/` | 确认资料版本固定10 SPS；不直接证明到货排针 |
| `SRC-CAN-BOARD` | `official_documents/SN65HVD230_CAN_module/Waveshare_SN65HVD230_CAN_Board_Schematic.pdf` | 参考板排针、120 Ω 与 10 kΩ |
| `SRC-CAN-IC` | `official_documents/SN65HVD230_CAN_module/TI_SN65HVD23x_3.3V_CAN_Transceivers_RevO.pdf` | 芯片引脚和职责 |
| `SRC-PWR-01` | `official_documents/S-250-24_power_supply/user_selected_S-250-24_24V_10A.png` | 商品选项，不含端子确认 |
| `SRC-IMU-01` | WitMotion 官方 WT901C 产品页及 WIT Standard Protocol | WT901C-TTL 串口角色；当前停用 |
