# 现有硬件基本资料

> 大部分使用文当可以在同文件夹查看，查看技术细节以技术文档为准（文件夹名：official_documents 于该文件同属一个文件夹）

## 1. 配件清单

| 编号           | 配件 | 数量 | 已确认型号 / 标识 |
|--------------|---|---:|---|
| MCU-01       | STM32 Nucleo-64 开发板 | 1 | STMicroelectronics NUCLEO-F446RE；PCB：MB1136 rev C；板载 MCU：STM32F446RET6 |
| IMU-01       | 九轴姿态传感器 | 1 | WitMotion WT901C-TTL |
| IMU-02       | 九轴姿态传感器 | 1 | WitMotion WT901C-TTL |
| ACT-01       | 集成减速电机 | 1 | SteadyWin GIM6010-8；Y-48 行星绕组；GDS68（12–48 V）；断电记住单圈位置 |
| MOTOR-01     | 直流无刷电机 | 1 | RoboMaster M3508 直流无刷电机 |
| ESC-01       | 无刷电机调速器 | 1 | RoboMaster C620 无刷电机调速器；与电机分开记录 |
| FORCE-01     | 微小方 S 型拉压力传感器 | 1 |
| ADC-01：HX711 | 称重传感器 ADC / 放大模块| 1 |

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
| 第二编码 | 断电记住单圈位置 | 订单配置图片中的选项名称 |

> 上表按订单页面原文记录，不对“第二编码”的具体器件型号或断电保持范围作额外推断。

### 5.2 GIM6010-8 官方基本参数

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

### 6.5 官方技术资料

| 资料 | 官方链接 |
|---|---|
| M3508 官方使用说明 | [本地保存：M3508 Brushless DC Motor User Guide v1.0（2025.10）](official_documents/RoboMaster_M3508_Brushless_DC_Motor_User_Guide_v1.0_2025.10.pdf) |
| M3508/C620 官方混控教程 | [本地保存：M3508 直流无刷减速电机混控教程 V1.0（2017.10）](official_documents/RoboMaster_M3508_C620_Mixing_Tutorial_V1.0_2017.10.pdf) |

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
| C620 官方使用说明（本地副本） | [本地保存：C620 User Guide V1.01](official_documents/RoboMaster_C620_Brushless_DC_Motor_Speed_Controller_V1.01.pdf) |
| M3508/C620 官方混控教程 | [本地保存：M3508/C620 Mixing Tutorial V1.0](official_documents/RoboMaster_M3508_C620_Mixing_Tutorial_V1.0_2017.10.pdf) |

## 8. FORCE-01：微小方 S 型拉压力传感器

### 8.1 基本信息

| 项目 | 已确认信息 |
|---|---|
| 类型 | 微小方 S 型拉压力传感器 / 应变式称重传感器 |
| 测量形式 | 拉力、压力 |
| 灵敏度 | 2.0 mV/V |
| 综合误差 | 0.05% F.S |
| 蠕变（30 min） | ≤ ±0.016% F.S / 30 min |
| 推荐激励电压 | 5–12 V DC |
| 零点平衡 | ±1.0% F.S |
| 绝缘阻抗 | ≥5000 MΩ（50 VDC） |
| 安全过载 | 120% F.S |
| 极限过载 | 150% F.S |
| 工作温度范围 | -20 ～ +60 °C |
| 温度补偿范围 | -20 ～ +60 °C |
| 零点温度影响 | ≤ ±0.015% F.S / 10 °C |
| 输出温度影响 | ≤ ±0.011% F.S / 10 °C |
| 防护等级 | IP67 |
| 额定载荷 | 产品系列提供 1、2、3、5、10、20、30、50、100 kg 等规格；当前实物具体量程待确认 |

> 商品资料中不同版本的输入/输出阻抗存在差异。部分资料标注输入阻抗 `380±20 Ω`、输出阻抗 `350±2 Ω`；另一版本标注输入阻抗 `750±20 Ω`、输出阻抗 `700±2 Ω`。实际参数应以当前实物型号、铭牌或对应厂家资料为准。

### 8.2 外形与安装

商品尺寸图显示传感器主体尺寸约为：

- 长度：25 mm
- 高度：30 mm
- 厚度：10 mm
- 安装螺纹 / 安装孔标注：M6

具体尺寸和螺纹深度在正式机械设计前应重新使用卡尺测量实物确认。

### 8.3 接线定义

| 线色 | 定义 |
|---|---|
| 红色 | 激励正 `E+` |
| 黑色 | 激励负 `E-` |
| 绿色 | 信号正 `S+` |
| 白色 | 信号负 `S-` |
| 屏蔽线 | Shield / 屏蔽 |

> 当前商品图片所示接线极性为压力测量方向。若用于拉力测量，输出信号正负方向可能需要交换绿色与白色信号线，具体以实际测试和厂家说明为准。


## 9. ADC-01：HX711 称重传感器采集模块

### 9.1 基本信息

| 项目      | 已确认信息                                                          |
|---------|----------------------------------------------------------------|
| 芯片 / 模块 | HX711                                                          |
| 类型      | 应变桥称重传感器 ADC / 信号放大模块                                          |
| 数量      | 1                                                              |
| 具体内容    | 详细请见本文件同文件夹的official_documents文件夹  电子秤HX71..料包 文件夹和HX711AD..资料 |

> 当前仅确认已有 HX711 模块。由于尚未提供 HX711 模块实物照片、PCB 版本或购买资料，因此本文件暂不记录其具体采样率、供电范围、增益、电气接口及引脚定义。后续以实际模块和对应技术资料为准。
## 8. 资料使用说明

- 开发板接线应以背面贴纸对应的 C03 或 C04 官方原理图及 UM1724 为准。
- MCU 外设、电气极限和复用功能应以 DS10693 与 RM0390 为准。
- WT901C-TTL 的默认波特率、默认输出频率和当前输出内容可能被修改，接线和读取前应通过官方工具或寄存器读取确认。
- GIM6010-8 的电源、端子、接口定义和通信协议应以 rev2.2 或厂家后续发布的更新手册为准。
- M3508 应以《M3508 直流无刷电机使用说明》v1.0（2025.10）为准，不得使用 M3508 P19 减速电机套装的参数代替。
- M3508 电机和 C620 电调是两个独立配件；C620 需另外购买。
- C620 电源、CAN 帧、PWM、终端电阻和线束定义应以 C620 V1.01 官方使用说明为准。
- 本文不记录无法由照片或官方资料确认的购买批次、传感器内部芯片型号和模块默认配置。
