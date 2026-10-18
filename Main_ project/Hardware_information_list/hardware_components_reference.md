# 现有硬件基本资料

> 大部分使用文当可以在同文件夹查看，查看技术细节以技术文档为准（文件夹名：official_documents 于该文件同属一个文件夹）

## 1. 配件清单

| 编号           | 配件 | 数量 | 已确认型号 / 标识 |
|--------------|---|---:|---|
| MCU-01       | STM32 Nucleo-64 开发板 | 1 | STMicroelectronics NUCLEO-F446RE；PCB：MB1136 rev C；板载 MCU：STM32F446RET6 |
| PWR-01       | 24 V 开关电源 | 1 | 商品选择为 S-250-24；24 V / 10 A；商品标称 250 W |
| CAN-01       | 3.3 V CAN 总线收发器模块 | 1 | 商品页展示为 Waveshare SN65HVD230 CAN Board；最高 1 Mbps；到货实物待核对 |
| CAN-02       | 3.3 V CAN 总线收发器模块 | 1 | 商品页展示为 Waveshare SN65HVD230 CAN Board；最高 1 Mbps；到货实物待核对 |
| IMU-01       | 九轴姿态传感器 | 1 | WitMotion WT901C-TTL |
| IMU-02       | 九轴姿态传感器 | 1 | WitMotion WT901C-TTL |
| ACT-01       | 集成减速电机 | 1 | SteadyWin GIM6010-8；Y-48 行星绕组；GDS68（12–48 V）；断电记住单圈位置 |
| MOTOR-01     | 直流无刷电机 | 1 | RoboMaster M3508 直流无刷电机 |
| ESC-01       | 无刷电机调速器 | 1 | RoboMaster C620 无刷电机调速器；与电机分开记录 |
| FORCE-01     | 5 kg 直梁式应变称重传感器 | 1 | 四线全桥；与 ExoKit 使用的直梁式负载传感器同类 |
| ADC-01：HX711 | HX711AD 称重传感器 ADC / 放大模块 | 1 | `RATE`接地，固定 10 SPS；仅作低速辅助力监测 |

说明：`MCU-01`、`MB1136 rev C` 和 `STM32F446RET6` 不是三个独立配件。它们分别表示同一块开发板的产品型号、PCB 版本和板载主微控制器。

## 2. MCU-01：STMicroelectronics NUCLEO-F446RE

### 2.1 身份信息

| 项目 | 已确认信息 | 确认依据 |
|---|---|---|
| 制造商 | STMicroelectronics | 开发板丝印和 ST 官方资料 |
| 开发板型号 | NUCLEO-F446RE | 实物照片丝印；ST 官方产品页 |
| 开发板类别 | STM32 Nucleo-64 | ST 官方产品页与 UM1724 |
| PCB 平台 | MB1136 | 实物照片丝印；UM1724 |
| PCB 版本 | rev C；照片未显示可区分 C03 / C04 的背面版本贴纸 | 实物照片丝印 `MB1136 rev C`；UM1724 列出 `MB1136-F446RE-C03` 和 `MB1136-F446RE-C04` |
| 板载主 MCU | STM32F446RET6 | UM1724 对 NUCLEO-F446RE 的官方对应表 |
| MCU 封装 | LQFP64 | ST 官方数据手册 DS10693 |
| 板载调试器 | ST-LINK/V2-1 | 实物照片；UM1724 |
| 扩展接口 | Arduino Uno V3、ST Morpho | 实物照片；ST 官方产品页与 UM1724 |

### 2.2 STM32F446RET6 基本参数

| 参数 | 官方规格 |
|---|---|
| CPU | Arm Cortex-M4，带单精度 FPU 和 DSP 指令 |
| 最高主频 | 180 MHz |
| Flash | 512 KB |
| SRAM | 128 KB |
| Backup SRAM | 4 KB |
| ADC | 3 × 12-bit ADC |
| DAC | 2 × 12-bit DAC |
| 定时器 | 10 个通用定时器、2 个高级控制定时器、2 个基本定时器；另含 SysTick 和看门狗 |
| USART / UART | 4 × USART、2 × UART |
| I2C | 4 个接口 |
| SPI / I2S | 4 × SPI，其中 3 个可复用为 I2S |
| CAN | 2 × CAN 2.0B Active |
| USB | USB OTG FS；USB OTG HS/FS 控制器 |
| MCU 工作电压 | 1.7 V 至 3.6 V；具体工作条件以 DS10693 为准 |

### 2.3 开发板基本配置

| 项目 | 已确认信息 |
|---|---|
| ST-LINK USB 功能 | 调试、下载、Virtual COM Port、Mass Storage |
| 用户 LED | LD2，连接 Arduino D13 / STM32 PA5 |
| 通信状态 LED | LD1，显示 ST-LINK 通信状态 |
| 电源 LED | LD3，指示目标板 5 V 电源状态 |
| 用户按键 | B1，连接 STM32 PC13 |
| 复位按键 | B2，连接 NRST |
| 高速外部时钟来源 | UM1724 说明 MB1136 C02 及以后版本默认由 ST-LINK MCO 提供 8 MHz 时钟 |
| 低速晶振 | 板载 X2，32.768 kHz |
| 默认虚拟串口 | USART2：PA2 / PA3 与 ST-LINK VCP 连接；连接关系受板上焊桥配置影响 |
| BOOT0 | 默认低电平；可通过 ST Morpho 的 CN7 引脚配置，操作前应查看 UM1724 |

### 2.4 官方技术资料

| 资料 | 官方链接 |
|---|---|
| NUCLEO-F446RE 官方产品页 | [ST NUCLEO-F446RE](https://www.st.com/en/evaluation-tools/nucleo-f446re.html) |
| Nucleo-64 / MB1136 用户手册 | [UM1724 - STM32 Nucleo-64 boards (MB1136)](https://www.st.com/resource/en/user_manual/um1724-stm32-nucleo64-boards-mb1136-stmicroelectronics.pdf) |
| MB1136 C03 官方电路原理图 | [MB1136-DEFAULT-C03 Board schematic](https://www.st.com/resource/en/schematic_pack/mb1136-default-c03_schematic.pdf) |
| MB1136 C04 官方电路原理图 | [MB1136-DEFAULT-C04 Board schematic](https://www.st.com/resource/en/schematic_pack/mb1136-default-c04_schematic.pdf) |
| STM32F446RE 官方产品页 | [ST STM32F446RE](https://www.st.com/en/microcontrollers-microprocessors/stm32f446re.html) |
| STM32F446xC/E 数据手册 | [DS10693 - STM32F446xC/E datasheet](https://www.st.com/resource/en/datasheet/stm32f446re.pdf) |
| STM32F446xx 参考手册 | [RM0390 - STM32F446xx reference manual](https://www.st.com/st-web-ui/static/active/en/resource/technical/document/reference_manual/DM00135183.pdf) |
| STM32F446xC/E 芯片勘误表 | [ES0298 - STM32F446xC/E device errata](https://www.st.com/resource/en/errata_sheet/es0298-stm32f446xcxe-device-errata-stmicroelectronics.pdf) |

请先查看开发板背面的完整版本贴纸：如果标注为 C03，下载 C03 原理图；如果标注为 C04，下载 C04 原理图。两份原理图均为 ST 官方资料，可用于以后核对电源、Arduino 接口、ST Morpho、ST-LINK、SWD、USART 和 BOOT0 连接关系。

## 3. IMU-01：WitMotion WT901C-TTL

### 3.1 基本信息

| 项目 | 已确认信息 |
|---|---|
| 制造商 | WitMotion / 维特智能 |
| 型号 | WT901C-TTL |
| 模块类型 | 九轴姿态传感器 / AHRS IMU |
| 数量 | 1 |
| 传感器组成 | 三轴加速度计、三轴陀螺仪、三轴磁力计 |
| 通信接口 | TTL 串口 |
| 可输出数据 | 加速度、角速度、磁场、Euler 角、Quaternion 四元数等；具体输出项由设备配置决定 |
| 加速度量程 | ±16 g |
| 陀螺仪量程 | ±2000 °/s |
| 串口波特率选项 | 4800、9600、19200、38400、57600、115200、230400 bit/s |
| 输出频率选项 | 0.2 Hz 至 200 Hz，具体可选档位以官方协议为准 |

### 3.2 官方技术资料

| 资料 | 官方链接 |
|---|---|
| WT901C-TTL/232 产品资料 | [WitMotion 官方产品资料页](https://wit-motion.yuque.com/wumwnr/docs/dawcwq?singleDoc) |
| TTL/232 快速上手 | [WitMotion 官方快速上手](https://wit-motion.yuque.com/wumwnr/bf4d0f/buzter) |
| WIT 标准通信协议 | [WIT Standard Communication Protocol](https://wit-motion.gitbook.io/witmotion-sdk/wit-standard-protocol/wit-standard-communication-protocol) |
| WitMotion SDK 下载 | [WitMotion SDK Download Center](https://tag.wit-motion.cn/SDK.html) |

## 4. IMU-02：WitMotion WT901C-TTL

IMU-02 与 IMU-01 为相同型号，基本参数和官方资料相同。

| 项目 | 已确认信息 |
|---|---|
| 型号 | WT901C-TTL |
| 数量 | 1 |
| 官方产品资料 | [WitMotion 官方产品资料页](https://wit-motion.yuque.com/wumwnr/docs/dawcwq?singleDoc) |
| 官方通信协议 | [WIT Standard Communication Protocol](https://wit-motion.gitbook.io/witmotion-sdk/wit-standard-protocol/wit-standard-communication-protocol) |

## 5. ACT-01：SteadyWin GIM6010-8

### 5.1 订单配置

| 项目 | 已确认配置 | 确认依据 |
|---|---|---|
| 制造商 | 江西伺泰威自动化设备有限公司 / SteadyWin | 官方手册 |
| 型号 | GIM6010-8 | 订单图片、官方手册 |
| 数量 | 1 | 订单图片 |
| 绕组方式 | Y-48 行星绕组 | 订单配置图片 |
| 驱动器 | GDS68（12–48 V） | 订单配置图片中的选项名称 |
| 位置功能描述 | 断电记住单圈位置 | 订单配置图片中的选项名称 |

> 订单选项另明确标注【旧24v】。保留“Y-48”原文，不将其直接解释为额定48V；“断电记住单圈位置”不证明已安装输出轴第二编码器。见[订单配置摘录](all_hardware_relevant_documents/GIM6010-8/purchase_evidence/order_configuration.md)。

### 5.2 GIM6010-8 手册参考参数（不等同于所购旧款）

下表仅保留 rev2.2 的资料值，项目旧款参考见5.5；不得直接将额定/堵转电流或扭矩常数作为固件限值。

| 参数 | 官方手册 rev2.2 |
|---|---:|
| 额定转速 | 120 rpm ±10% |
| 最大转速 | 420 rpm ±10% |
| 额定扭矩 | 5 N·m |
| 堵转扭矩 | 11 N·m |
| 额定电流 | 10.5 A |
| 堵转电流 | 25 A |
| 空载电流 | 0.4 A |
| 转速常数 | 12.3 rpm/V |
| 扭矩常数 | 0.47 N·m/A |
| 相间电阻 | 0.48 Ω ±10% |
| 相间电感 | 368 μH ±10% |
| 重量 | 388 g ±3 g |
| 极对数 | 14 对 |
| 相数 | 3 相 |
| 驱动方式 | FOC |
| 减速比 | 8:1 |

### 5.3 GDS68 官方基本参数

| 参数 | 官方手册 rev2.2 |
|---|---:|
| 额定电压 | 15–48 V DC |
| 手册标注最小 / 最大电压 | 12 / 72 V DC |
| 额定电流 | 6 A |
| 最大线电流 | 30 A |
| 最大相电流 | 120 A |
| 待机功耗 | <10 mA |
| CAN 总线最大波特率 | 1 Mbit/s |
| Type-C 速率 | 10 Mbit/s |
| 主编码器 | 14-bit 单圈绝对值编码器 |
| 工作环境温度 | -20–70 °C |
| 电机 / 驱动板告警温度 | 默认 90 °C，可调 |
| 电源与通信端子 | XT30PB(2+2)-M；集成电源与 CAN |
| 调试接口 | USB Type-C |
| 第二编码器接口 | USART 或 I2C |

> 订单选项标注为“GDS68（12–48 V）”；官方 rev2.2 手册的驱动器规格表则标注额定电压为 15–48 V DC，最小/最大电压为 12/72 V DC。两者的含义不同，因此在本文中分开保留。

### 5.4 官方技术资料

| 资料 | 官方链接 |
|---|---|
| SteadyWin 下载中心 | [伺泰威官方下载页](http://steadywin.cn/h-col-124.html) |
| GIM6010-8 使用手册 | [SteadyWin GIM6010-8 使用手册 rev2.2（PDF）](http://14180476.s21i.faiusr.com/61/ABUIABA9GAAgzpi6wQYoh9D9CQ.pdf) |

手册第 1.1 节含电机尺寸图，第 2.1–2.4 节含 GDS68 尺寸、接口总览和引脚定义，可作为以后核对接线的官方原始资料。

### 5.5 旧款与不同来源的参数差异

2026-09-24 核对；各列独立，不拼接成一份实物规格。依据订单【旧24v】，暂以商品旧款表第一列作为性能参考，未经台架验证。

| 参数 | 旧款商品图：24V/GDS68 | 官网中文页24V图 | 官网英文页24V/GDS68 | 下载参数PDF/Excel的GDS68列 |
|---|---:|---:|---:|---:|
| 功率 W | 69.60 | 158.4 | 252 | 252 |
| 额定电流 A | 2.9 | 6.6 | 10.5 | 10.5 |
| 额定扭矩 N·m | 4.6 | 4.85 | 5 | 5 |
| 额定转速 rpm | 81.9 | 245 | 120 | 120 |
| 相间电阻 Ω | 1.45 | 0.44 | 0.44 | 0.44 |
| 扭矩常数 N·m/A | 1.2 | 0.61 | 0.47 | 0.47 |

旧款图另标：12–48V、堵转电流10.36A、堵转扭矩13N·m、相间电感0.82mH、减速比8:1。堵转参数不作为连续运行或安全限值。商品和选型资料写16bit，rev2.2驱动器表写14bit，实际编码器版本未确定。中文官网参数图的电感数值/单位也不与其他表直接合并。

来源：[旧款截图](all_hardware_relevant_documents/GIM6010-8/purchase_evidence/old_24V_product_parameters.png)、[中文产品页](https://steadywin.cn/pd.jsp?id=115)、[英文产品页](https://www.steadywin.cn/en/pd.jsp?fromColId=0&id=116)、[归档来源索引](all_hardware_relevant_documents/GIM6010-8/SOURCE.md)。官网下载表没有证明购买批次，硬件/固件版本可后续用上位机读取。

### 5.6 GDS68 接口及配置细节

| 项目 | 已有资料结论 | rev2.2页码 / 边界 |
|---|---|---|
| 电源/CAN | 板端XT30PB(2+2)-M；配套线XT30(2+2)-F；VBUS/GND/CANH/CANL | 10；按图观察方向核对，不能把板端正视图直接当线端背视图 |
| USB | Type-C用于调试；需要外部动力电源 | 15–16；不能靠电脑USB驱动电机 |
| 版本读取 | 电机精灵“电机参数”页显示硬件/固件版本 | 20；不以“旧24v”推断3.x版本 |
| 节点ID | `odrv0.axis0.config.can.node_id`；修改后保存配置 | 27–28；项目实际ID未选定 |
| 波特率/终端 | `odrv0.can.config.baud_rate`、`enable_r120`；内置120Ω可控制 | 34、38；示例`r120_gpio_num=5`须对应板版本，未执行 |
| USB与CAN | 硬件≤3.7有兼容切换限制 | 37；更高版本按手册说明 |
| 扩展口 | v3.9及以后不再提供20-pin扩展接口 | 10–11；未认定实物附加口存在 |
| 泄放 | EMG与GND间接刹车电阻；可设置`enable_brake_resistor`、`brake_resistance`、`max_regen_current`及电压泄放范围 | 13、38–39；阻值、功率、散热和实际接口未定，不能套用示例占位值 |

抱闸与泄放电阻用途不同，不假定同一接口可同时使用。驱动器无防反接；手册15–16页提示避免动力热插拔及用空开切换单根动力线。上述资料不构成完整急停/回生保护设计。用户已有匹配电源/CAN线束，不重复采购。上位机与Zadig仅[归档](all_hardware_relevant_documents/GDS68_tools/SOURCE.md)，未安装、未升级固件。

## 6. MOTOR-01：RoboMaster M3508 直流无刷电机

### 6.1 基本信息

| 项目 | 已确认信息 |
|---|---|
| 品牌 | DJI RoboMaster |
| 官方名称 | RoboMaster M3508 直流无刷电机 |
| 英文名称 | M3508 Brushless DC Motor |
| 型号 | M3508 |
| 数量 | 1 |
| 配套电调 | RoboMaster C620 无刷电机调速器 |
| 电调销售方式 | 需另外购买，不是电机包装内的套餐部件 |
| 手册标注包装内容 | M3508 电机 × 1；7-Pin 数据线 × 1 |

> 上述名称和包装信息来自用户提供的《M3508 直流无刷电机使用说明》v1.0（2025.10）。本文不再把该电机写成“M3508 P19 减速电机套装”。

### 6.2 电机官方参数

| 参数 | 官方手册 v1.0（2025.10） |
|---|---:|
| 额定电压 | 24 V |
| 转矩常数 | 0.02 N·m/A |
| 转速常数 | 390 rpm/V |
| 转速转矩梯度 | 260 rpm/(N·m) |
| 机械时间常数 | 49 ms |
| 相电阻 | 0.194 Ω |
| 相电感 | 0.097 mH |
| 使用环境温度 | 0–50 °C |
| 绕组最高允许温度 | 125 °C |
| 极对数 | 7 |
| 额定动载荷 | 276 N |
| 电机重量 | 120 g |
| 安装螺纹孔 | M3，深度 4 mm |

### 6.3 搭配 C620 在额定电压下的官方参数

| 参数 | 官方手册 v1.0（2025.10） |
|---|---:|
| 空载转速 | 9400 rpm |
| 空载电流 | 0.6 A |
| 额定转速 | 9085 rpm |
| 额定转矩（最大连续转矩） | 0.16 N·m |
| 额定电流 | 10 A |
| 最大效率 | 83% |
| 峰转矩 | 0.33 N·m |
| 峰转电流 | 2.5 A |

> 官方手册注明：上表数据为实验环境测得；电流数值均表示 C620 电调输入端电流，效率表示电机与电调组成的驱动系统整体效率。

### 6.4 输出齿官方参数

| 参数 | 官方手册 v1.0（2025.10） |
|---|---:|
| 齿数 | 17 |
| 模数 | 0.6 |
| 压力角 | 20° |
| 分度圆直径 | 10.2 mm |
| 齿顶圆直径 | 11.38 mm |
| 齿根圆直径 | 8.68 mm |
| 变位系数 | -0.01 |
| 跨齿数 | 2 |
| 公法线长度 | 2.79 mm，上偏差 -0.02 mm，下偏差 -0.04 mm |

### 6.5 本项目当前机械使用方式

| 项目 | 当前决定 |
|---|---|
| 所用产品层级 | 裸 M3508，不含 P19 减速器 |
| 几何传动比 | 1:1 |
| 附加减速机构 | 当前不设置 |
| 暂定输出轴 | M3508 的 Ø5 mm 电机轴直接作为输出轴 |
| 当前验证边界 | 先用于几何装配、无负载和低负载台架验证；轴端承载能力尚未验证 |

上述内容是本项目当前机械方案，不是 M3508 厂家对人体侧关节用途的承诺。1:1 直接输出不产生减速增矩，不得套用 P19 的减速比、Ø10 mm 输出轴尺寸或输出转矩。

### 6.6 官方技术资料

| 资料 | 官方链接 |
|---|---|
| M3508 官方使用说明 | [本地保存：M3508 Brushless DC Motor User Guide v1.0（2025.10）](all_hardware_relevant_documents/RoboMaster_M3508_Brushless_DC_Motor_User_Guide_v1.0_2025.10.pdf) |

## 7. ESC-01：RoboMaster C620 无刷电机调速器

### 7.1 基本信息

| 项目 | 已确认信息 |
|---|---|
| 品牌 | DJI RoboMaster |
| 官方名称 | RoboMaster C620 无刷电机调速器 |
| 英文名称 | C620 Brushless DC Motor Speed Controller |
| 型号 | C620 |
| 数量 | 1 |
| 与 M3508 的关系 | 单独配件；通过 7-Pin 数据线和三相动力线与 M3508 连接 |

### 7.2 官方基本参数

| 参数 | 官方信息 |
|---|---:|
| 额定电压 | 24 V |
| 重量 | 35 g |
| 外形尺寸（不含线） | 49.4 × 25.8 × 11.5 mm |
| 控制信号 | CAN 总线指令；50–500 Hz PWM |
| 最大持续电流 | 20 A |
| CAN 总线比特率 | 1 Mbit/s |
| 工作环境温度 | 0–50 °C |
| 电源连接 | XT30，24 V 电源 |
| CAN 终端电阻 | 板载 120 Ω，通过开关接入或断开 |
| CAN 反馈 | 电机温度、转子位置、转子转速等 |
| 其他官方说明 | 支持对 CAN 总线上的电调快速设置 ID |

### 7.3 CAN 协议基本信息

| 项目 | 官方手册信息 |
|---|---|
| 电调 ID 范围 | 1–8 |
| 控制报文标识符 | `0x200` 和 `0x1FF` |
| 反馈报文标识符 | `0x200 + 电调 ID` |
| 帧格式 | CAN 标准帧，8 字节 |
| 控制值范围 | -16384–0–16384，对应 -20–0–20 A 转矩电流 |
| 反馈内容 | 转子机械角度、转速、实际转矩电流、电机温度 |
| 默认反馈频率 | 1 kHz，手册说明可在 RoboMaster Assistant 中修改 |
| 转子机械角度值 | 0–8191，对应 0–360° |

### 7.4 官方技术资料

| 资料 | 官方链接 |
|---|---|
| C620 官方使用说明 | [在线：RoboMaster C620 Brushless DC Motor Speed Controller V1.01（PDF）](https://cdn-hz.robomaster.com/robomasters/public/document/RoboMaster%20C620%20Brushless%20DC%20Motor%20Speed%20Controller%20V1.01.pdf) |
| C620 官方使用说明（本地副本） | [本地保存：C620 User Guide V1.01](all_hardware_relevant_documents/RoboMaster_C620_Brushless_DC_Motor_Speed_Controller_V1.01.pdf) |

## 8. FORCE-01：5 kg 直梁式应变称重传感器

### 8.1 基本信息

| 项目 | 已确认信息 |
|---|---|
| 类型 | 直梁式 / 平行梁式应变称重传感器；四线全桥 |
| 额定载荷 | 5 kg |
| 灵敏度 | 1.0 ± 0.1 mV/V |
| 综合误差 | 0.05% F.S |
| 推荐激励电压 | 3–12 V |
| 输入电阻 | 1000 ± 50 Ω |
| 输出电阻 | 1000 ± 50 Ω |
| 绝缘电阻 | ≥2000 MΩ（100 VDC） |
| 极限过载 | 150% F.S |
| 工作温度范围 | -10 ～ +50 °C |

以上参数来自随购买套装提供的技术参数表；到货后仍需核对实物尺寸、线色、桥阻和零点。

### 8.2 外形与安装

随附尺寸图显示传感器主体尺寸为：

- 长度：80 mm
- 截面：12.7 mm × 12.7 mm（5 kg 规格属于 `W > 2 kg`）
- 安装孔：一端 `2-M5`，另一端 `2-M4`

安装采用悬臂方式：一端固定，另一端悬空并沿标示方向受力。不得直接压迫中部白色覆胶区域。具体孔径、螺纹深度和受力正方向在正式机械设计前应重新测量实物确认。

### 8.3 接线定义

| 线色 | 定义 |
|---|---|
| 红色 | 激励正 `E+` |
| 黑色 | 激励负 `E-` |
| 绿色 | 信号正 `A+` |
| 白色 | 信号负 `A-` |

> 线色定义来自本次套装资料。实际正负方向必须通过安装后的已知方向加载测试确认，不以交换线色代替方向标定。


## 9. ADC-01：HX711 称重传感器采集模块

### 9.1 基本信息

| 项目 | 已确认信息 |
|---|---|
| 芯片 / 模块 | HX711-BF / HX711AD 模块 |
| 类型 | 应变桥称重传感器 24 位 ADC / 信号放大模块 |
| 数量 | 1 |
| 传感器侧接口 | `E+`、`E-`、`A+`、`A-`；通道 B 不使用 |
| MCU 侧接口 | `VCC`、`GND`、`DT/DOUT`、`SCK/PD_SCK`；具体排针顺序到货后核对丝印 |
| 输出速率 | 固定 10 SPS：随附模块原理图中 HX711 `RATE`（15脚）直接接 `GND` |
| 当前用途 | 低速辅助力监测、显示、记录和标定观察 |
| 不承担的功能 | 不作为快速助力触发、关节主控制反馈、安全触发或高带宽力闭环输入 |
| 资料位置 | `all_hardware_relevant_documents/电子秤HX71..料包/` |

> 本项目接受该模块的 10 SPS 限制。软件只能按新数据到达速率更新力监测值，不得用控制循环频率伪装成更高的传感器采样率。到货实物若与资料原理图不同，应重新核对 `RATE` 接法。

### 9.2 模块供电与数字电平

配套ADC原理图把VSUP及DVDD连到VCC，8550与20k/8.2k网络的模拟稳压目标约4.3V；配套使用图指定+5V。原项目选择NUCLEO的5V供电。2026-10-16用户为简化接线改选**方案B：VCC接NUCLEO CN7-16 3.3V，SCK/DOUT与PC3/PC2直连**。HX711芯片DVDD 2.6–5.5V、VIH=0.7·DVDD，3.3V逻辑可直接驱动；但模拟稳压在3.3V下会饱和，E+预计约3.0–3.2V，信号相应变小。这块模块整板3.3V供电**尚未验证**，首次上电须实测E+−E−电压。

STM32端为3.3V逻辑，方案B无需电平转换。上电/复位时须保证SCK不误保持高电平，建议SCK对GND加10kΩ下拉。HX711芯片11脚是PD_SCK、12脚是DOUT，并非模块排针11/12。配套原理图数字接口附近存在“B+”标注歧义，应按实物DOUT/DT丝印和连通关系核对，不接桥路B+。

HX711手册第5页规定SCK高电平0.2–50µs，超过60µs进入掉电；MCU DOUT输入不启用内部上下拉。传感器按配套资料红E+、黑E−、绿（部分图示蓝）A+、白A−，具体以所购传感器核对；不随意延长原传感器线。无需单独购买HX711电源适配器。

## 10. CAN-01 / CAN-02：SN65HVD230 CAN 总线收发器模块

### 10.1 已确认信息

| 项目 | 已确认信息 | 确认依据 |
|---|---|---|
| 模块类型 | Waveshare SN65HVD230 CAN Board 样式的 3.3 V 高速 CAN 总线收发器模块 | 用户提供的商品截图及截图中的 Waveshare 资料链接 |
| 数量 | 2 块 | 用户确认 |
| 核心收发器 | SN65HVD230，芯片顶面通常标记为 `VP230` | 商品截图；Waveshare 原理图；TI 数据手册 |
| 标称最高通信速率 | 1 Mbps | 商品名称；Waveshare Wiki；TI 官方 SN65HVD230 数据手册 |
| 逻辑侧接口 | `3.3V`、`GND`、`RX`、`TX` | 商品截图 PCB 丝印；Waveshare 原理图 |
| 总线侧接口 | `CANH`、`CANL`，同时引出至 2 位接线端子和黄色 2 针排针 | 商品截图 PCB 丝印；Waveshare 原理图 |
| 终端电阻 | 板载 120 Ω 电阻，原理图显示其固定跨接在 CANH 与 CANL 之间 | 商品截图；Waveshare 原理图 |
| 斜率控制 | SN65HVD230 的 `Rs` 端通过 10 kΩ 电阻接地 | Waveshare 原理图 |
| 主要作用 | 在 STM32 CAN 控制器的逻辑信号与 CAN_H/CAN_L 差分总线之间完成物理层收发 | TI 官方数据手册 |
| 计划用途 | 分别用于 `SHOULDER_SIDE` 的 GDS68 与 `ELBOW` 的 C620 CAN 通信链路 | 当前系统连接方案；最终分配待接线验证 |

商品名称中的“工业级抗干扰”属于商家描述，当前没有独立测试结果，因此不作为本项目已经验证的性能结论。

### 10.2 SN65HVD230 芯片级官方参数

| 参数 | TI 官方资料 |
|---|---|
| 协议类型 | CAN 高速物理层收发器，兼容 ISO 11898-2 |
| 通道数量 | 1 路 |
| 供电范围 | 3.0–3.6 V |
| 最高信号速率 | 1 Mbps |
| 总线故障电压范围 | -4 至 16 V |
| 工作模式 | 正常模式、斜率控制模式和低电流待机模式 |
| 总线端静电保护 | 超过 ±16 kV HBM |
| 其他芯片级功能 | 热关断、开路失效保护以及上电/掉电毛刺抑制 |

以上参数属于 SN65HVD230 芯片级规格。Waveshare 原理图能够确认固定接入的 120 Ω 终端电阻和 10 kΩ 斜率控制电阻，但原理图未显示独立 TVS、隔离器或电源隔离电路。因此，本模块属于非隔离 CAN 收发器板，不能把“工业级抗干扰”理解为已经具备隔离型 CAN 模块的防护能力。照片中的黄色部件是 CANH/CANL 两针接口，不是终端电阻使能跳帽。

当前依据是用户提供的商品页截图，而不是两块到货模块的正反面实拍。第三方商品是否为 Waveshare 原装、PCB 是否改版以及所装芯片是否与页面一致，仍应在到货后核对；在此之前，文档只按“商品页展示版本”记录。

### 10.3 计划连接关系

每块模块的 `TX` 接 STM32F446RE 的 `CAN_TX`，`RX` 接 `CAN_RX`，并连接 `3.3V` 和共地 `GND`；差分总线侧的 `CANH`、`CANL` 分别连接对应驱动器的同名端子。两块模块的初步分配为：

- `CAN-01`：NUCLEO-F446RE 与 `SHOULDER_SIDE` 的 GDS68；
- `CAN-02`：NUCLEO-F446RE 与 `ELBOW` 的 C620。

这是当前建议的双 CAN 分路方案，不表示已经完成实际接线。120 Ω 终端电阻只应位于每条 CAN 总线的两个物理末端。本模块的 120 Ω 电阻按原理图为固定接入；若实际拓扑不需要该端终端电阻，只能在确认实物电路后拆除相应电阻，不能把黄色两针接口当作开关。正式通电前还必须核对到货模块丝印、供电极性、CANH/CANL，并测量总线断电后的等效电阻。

模块商品图排针从上至下为3.3V/GND/RX/TX，而参考原理图P1编号为1TX、2GND、3V3.3、4RX；必须按信号名称对应实物，不套用参考P1针序。TI芯片D=1、R=4、VCC=3、GND=2、CANH=7、CANL=6仅用于电路核查，不是模块排针编号。完整断电且每条总线只有两只120Ω时，H/L间约60Ω；这不等于测得电缆120Ω特性阻抗。

### 10.4 资料与下载位置

| 资料 | 位置 |
|---|---|
| 用户提供的芯路城 CAN 资料汇总页 | [芯路城：几款 CAN 模块资料汇总](https://www.xinlucity.com/?s=plugins/index/pluginsname/ask/pluginscontrol/index/pluginsaction/detail/id/93.html) |
| 商品截图所示 Waveshare Wiki | [Waveshare SN65HVD230 CAN Board](https://www.waveshare.com/wiki/SN65HVD230_CAN_Board) |
| Waveshare 原理图本地副本 | `all_hardware_relevant_documents/SN65HVD230_CAN_module/Waveshare_SN65HVD230_CAN_Board_Schematic.pdf` |
| Waveshare 配套数据手册本地副本 | `all_hardware_relevant_documents/SN65HVD230_CAN_module/Waveshare_SN65HVD230_CAN_Board_Datasheets.pdf` |
| Waveshare 示例代码本地副本 | `all_hardware_relevant_documents/SN65HVD230_CAN_module/Waveshare_SN65HVD230_CAN_Board_Code.7z` |
| 用户提供的商品截图本地副本 | `all_hardware_relevant_documents/SN65HVD230_CAN_module/user_provided_product_screenshot.png` |
| TI SN65HVD230 产品页 | [Texas Instruments SN65HVD230](https://www.ti.com/product/SN65HVD230) |
| TI SN65HVD23x 官方数据手册 | [在线 PDF](https://www.ti.com/lit/ds/symlink/sn65hvd230.pdf) |
| TI 官方数据手册本地副本 | `all_hardware_relevant_documents/SN65HVD230_CAN_module/TI_SN65HVD23x_3.3V_CAN_Transceivers_RevO.pdf` |

最初提供的芯路城页面是多种 CAN 分析仪资料汇总，没有列出该小模块的专用附件。本次商品截图给出了 Waveshare 对应页面，因此已补充下载原理图、配套数据手册和示例代码。示例代码仅作为接口参考，后续编程仍应先判断与 STM32F446RE 工程的适配范围，不得未经核对直接并入主程序。

## 11. PWR-01：S-250-24 开关电源

### 11.1 规格来源与差异

| 项目 | 既有商品截图 | 卖家提供规格书001（2024-09-20） |
|---|---|---|
| 输出 | 24V/10A，标250W | 24V/10.4A，250W（第2页） |
| 调压 | 不作为定值 | 出厂23.7–24.3V；可调22.7–25.2V（第2页） |
| 输入 | 95–132 / 185–264Vac | 100–240Vac、额定115/230Vac，同时写180Vac启动；第2页说通过开关选择，档位待核对 |
| 尺寸 | 215×115×50mm | 215×113×50mm（第3页） |
| 保护 | 商品宣称过载/过压/短路 | 过流12.5–14.5A恒功率，过功率300–350W，短路解除自动恢复（第3页） |

规格书第2页另列过温/过压保护，但不据此编造阈值。供应商栏空白，尚未核对到货批次。**原PDF临时路径在归档时已失效，当前仅保存此前的读取记录及第7页派生图；原PDF待补，不宣称已保存原件。**

### 11.2 电源端子与项目用途

按规格书第7页风扇面朝上、端子朝向观察者，从左至右1–9编号，调压器不计入端子：

| 端子 | 定义 | 连接职责 |
|---|---|---|
| 1/2/3 | +V | 同一路+24V，接动力正极分配 |
| 4/5/6 | −V | 同一路直流0V，接动力负极分配 |
| 7 | GND（保护接地） | 接PE，不当作逻辑GND/直流负极 |
| 8 | AC/N | 交流零线 |
| 9 | AC/L | 交流火线 |

不得采用卖家“零火线无需区分”的说法；L/N按标识接。三个+V/−V端子不是三路独立额定输出，电流不能乘三。实物丝印与输入选择开关仍待核对；S-350-24商品接线图只说明名称，不替代本型号编号。

项目采用USB给NUCLEO和低压模块供电，24V动力域向GDS68与C620独立分支，M3508只接C620输出。逻辑电源不得接24V。电机工作点、电源温升及回生尚未验证，不能由250W标签推断双关节同时输出能力。保护地、绝缘罩、应力释放、保险、急停和回生处理须在通电验证前落实；当前尚未完成保护器件选型。

### 11.3 本地资料

- [原商品截图及来源](all_hardware_relevant_documents/S-250-24_power_supply/SOURCE.md)
- [规格书读取记录（原PDF待补）](all_hardware_relevant_documents/S-250-24_power_supply/specification_reading_record.md)
- [规格书第7页派生图](all_hardware_relevant_documents/S-250-24_power_supply/specification_page7_render.png)

## 12. 资料使用说明

- 开发板接线应以背面贴纸对应的 C03 或 C04 官方原理图及 UM1724 为准。
- MCU 外设、电气极限和复用功能应以 DS10693 与 RM0390 为准。
- WT901C-TTL 的默认波特率、默认输出频率和当前输出内容可能被修改，接线和读取前应通过官方工具或寄存器读取确认。
- GIM6010-8 的电源、端子、接口定义和通信协议应以 rev2.2 或厂家后续发布的更新手册为准。
- M3508 应以《M3508 直流无刷电机使用说明》v1.0（2025.10）为准，不得使用 M3508 P19 减速电机套装的参数代替。
- M3508 电机和 C620 电调是两个独立配件；C620 需另外购买。
- C620 电源、CAN 帧、PWM、终端电阻和线束定义应以 C620 V1.01 官方使用说明为准。
- SN65HVD230 的芯片级电气参数以 TI 官方数据手册为准；当前板级记录以 Waveshare 原理图和用户提供的商品截图为准，到货后仍须用实物 PCB 丝印和测量结果复核。
- S-250-24 已读取卖家规格书并保留第7页派生图；原PDF待补。型号级端子已登记，实物一致性、输入档位及系统供电能力仍待核对。
- 本文不记录无法由照片或官方资料确认的购买批次、传感器内部芯片型号和模块默认配置。

## 13. 已有线束与连接边界

用户已拥有NUCLEO Mini-B数据线、GDS68 XT30(2+2)-F配套线、C620电源线及M3508原厂连接线。C620的CAN线是否已有尚未明确。C620手册24–25页：24V动力输入；CAN固定1Mbps；A黑=CANL、B红=CANH（不是电源黑/红）；CAN运行不同时接PWM。120Ω终端开关断电设置。M3508手册中文第6页（PDF第8页）：用原厂7-pin及同色三相接头连接C620，不拆未公开针序，不直接接24V。具体项目接线见[接口登记表](hardware_interface_registry.md)。
