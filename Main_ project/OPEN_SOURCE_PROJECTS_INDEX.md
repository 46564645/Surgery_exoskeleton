# 上肢外骨骼开源项目本地索引

更新日期：2026-09-22

本目录绝对路径：`/Users/mzc/Desktop/Surgery_exo skeleton/Open-source projects/`

## 本目录的定位

这里保存的只是目前已经收集到的**部分参考项目**，不是完整的开源方案库。本地文件夹中没有某个模块时，不能直接得出“没有现成项目、需要自己从零写”的结论，必须继续进行联网检索。

本项目属于学生个人原型和学习娱乐用途，现阶段许可证不作为是否下载、研究和原型复用的筛选条件。表格仍保留来源和许可证记录，主要用于以后追溯项目出处。

## 使用规则

每个软件或硬件模块开始设计、写代码之前，必须按以下顺序检查现有成果：

1. 先检查 ExoKit 主参考项目中是否已有可直接借鉴的结构、接口或逻辑。
2. 再检查本地这个文件夹中的参考项目，但必须记住它们只是部分参考。
3. 本地找不到或现有项目不匹配时，必须继续到 GitHub、Gitee、GitCode、AtomGit、GitLink 等开源网站检索，不允许直接从零编写。
4. 搜索到候选项目后，按“可直接使用 > 部分可以使用 > 仅借鉴思路”的顺序评估；预计修改量小于 40%时优先直接适配。
5. 如果只能复用部分代码，应明确保留哪些部分、修改哪些部分，并记录项目来源。详细规则见技术文档中的 AI 编写原则和开源项目原则。
6. 如果网上仍然找不到合适项目，必须先用最直白的话告诉用户“这一部分没有找到可直接使用的开源实现”，讨论确认后才能自行设计或由 AI 从零编写。
7. 本项目为学生个人原型和学习娱乐用途，许可证不作为现阶段的筛选条件。

“修改量小于 40%”只是项目内部判断是否值得直接适配的工程规则，不替代实物核对和台架验证。

## 网上开源项目检索入口

| 平台 | 链接 | 主要用途 |
|---|---|---|
| GitHub | https://github.com/search | 第一检索入口，优先查英文型号、芯片名、控制器名和完整项目 |
| Gitee（码云） | https://gitee.com/ | 中文代码托管平台，适合检索 STM32、机器人、电机驱动和国内硬件示例 |
| GitCode | https://gitcode.com/ | 中文开源项目与代码托管平台，可补充检索国内项目和镜像 |
| AtomGit | https://atomgit.com/ | 国内开源代码托管与协作平台，可检索嵌入式和机器人项目 |
| GitLink（确实开源） | https://www.gitlink.org.cn/ | 中文开源社区和项目托管平台 |
| OSCHINA 开源项目 | https://www.oschina.net/project | 中文开源项目分类与发现入口 |
| GitLab Explore | https://gitlab.com/explore | GitHub 以外的国际开源仓库补充入口 |

实际检索不能只搜索一次。至少要组合中英文关键词，例如 `GIM6010 STM32 CAN`、`M3508 C620 STM32 HAL`、`WT901C TTL STM32`、`HX711 STM32 non-blocking`、`upper limb exoskeleton open source` 和“上肢外骨骼 STM32”。

## 项目清单与位置

| 项目 | 本地位置 | 原始来源 | 来源/许可证记录 | 对本项目的用途 | 当前复用判断 |
|---|---|---|---|---|---|
| ExoKit | `./ExoKit-main/` | https://github.com/HCI-Lab-Saarland/ExoKit | 仓库未发现明确开源许可证 | 本项目整体底版；参考背架、绑带、导轨、上臂/前臂固定、交互抽象、状态与命令组织 | **主参考/部分复用**。优先依据它修改尺寸和电机接口；Arduino、Dynamixel 底层代码不能直接用于 STM32F446RE |
| LEAP Upper Limb | `./LEAP-upper-limb-main/` | https://github.com/ziteh/LEAP-upper-limb | MPL-2.0；仓库已归档并停止更新 | 参考 STM32F446RE 主控、STM32Cube/HAL 工程组织、上肢多关节系统和硬件分层 | **部分复用**。主控型号接近，但电机、驱动器和传感器不同 |
| Penn Elbow Exoskeleton | `./elbow_exoskeleton-main/` | https://github.com/penn-figueroa-lab/elbow_exoskeleton | 仓库未发现明确开源许可证；当前仅记录来源 | 参考单自由度肘部结构、左右臂 CAD、可调绑带、肘轴偏移补偿和装配方法 | **思路/结构参考**。电机和整体机械方案不同 |
| Auxivo EduExo Maker | `./Auxivo_EduExo_Maker/` | https://www.auxivo.com/eduexo-maker | 官方页面说明为个人使用许可，符合当前学生个人原型用途 | 参考教学型肘部外骨骼、力传感器、位置控制、导纳控制示例和装配说明 | **部分复用/学习参考**。重点查看力传感和导纳控制示例 |
| OpenExo | `./OpenExo/` | https://github.com/naubiomech/OpenExo | 硬件 CERN-OHL-P-2.0；软件 LGPL-3.0 | 参考设备/关节/控制器分层、校准、状态管理、错误管理和数据记录 | **部分复用/架构参考**。主要面向另一类外骨骼硬件，不能整套直接移植。下载版本：`523852a` |
| UICRM-Embedded | `./UICRM-Embedded/` | https://github.com/UIC-RoboMaster/UICRM-Embedded | GPL-3.0；当前仅记录来源 | 参考 STM32 上的 M3508+C620 CAN 报文、反馈解析、转矩电流换算、角度累计和电机控制结构 | **部分复用**。硬件匹配度高，但代码与其 BSP/线程/控制框架耦合，需要拆出适合本项目的驱动部分。下载版本：`225427b` |
| WitMotion JY901 标准协议 | `./WitStandardProtocol_JY901/` | https://github.com/WITMOTION/WitStandardProtocol_JY901 | 仓库未发现明确开源许可证；当前仅记录来源 | 参考 WT901C-TTL/JY901 串口协议、寄存器定义、数据帧解析和 STM32 示例 | **协议核对/部分适配**。优先核对 `STM32F1/` 与通用 `wit_c_sdk.c/.h`，再适配 STM32F446RE HAL。下载版本：`e8f4e3a` |
| STM32 Libraries - HX711 | `./STM32-Libraries-HX711/` | https://github.com/DanielMartensson/STM32-Libraries | MIT | 直接候选模块位于 `HX711 Load Cell Amplifier/HX711.c` 和 `HX711.h`，包含 GPIO 读取、增益、去皮、平均和标定接口 | **优先直接适配候选**。预计可在小于 40% 修改量内接入 STM32F446RE，但使用前必须检查阻塞等待、超时、采样率和控制循环影响。下载版本：`5a8a5d6` |

## 各模块开始编码时先看哪里

### 整体结构与交互逻辑

1. `./ExoKit-main/`：第一优先级和整体底版。
2. `./OpenExo/`：用于补充系统分层、校准、错误状态和日志组织。
3. `./LEAP-upper-limb-main/`：用于补充 STM32F446RE 上肢外骨骼工程组织。

### J2 肩部关节：GIM6010-8

当前本地参考库中还没有与 STM32F446RE 足够接近、可直接采用的 GIM6010-8 驱动。开始这个模块前，必须先使用上面的开源平台重新联网检索，不能只依据当前本地目录判断，也不能直接从零编写。

先以本地官方资料核对协议：

`/Users/mzc/Desktop/Surgery_exo skeleton/Main_ project/hardware_list/official_documents/ABUIABA9GAAgzpi6wQYoh9D9CQ.pdf`

如果网上项目和官方示例仍不能覆盖所需接口，应先和用户讨论确认，再决定是否自行实现 `Gim6010Actuator` 适配层。

### J3 肘部关节：裸 M3508 + C620

1. 代码参考：`./UICRM-Embedded/boards/drivers/src/Motor/DjiMotorBase.cpp`
2. 接口与参数参考：`./UICRM-Embedded/boards/drivers/include/Motor/DjiMotorBase.h`
3. 官方 C620 资料：`/Users/mzc/Desktop/Surgery_exo skeleton/Main_ project/hardware_list/official_documents/RoboMaster_C620_Brushless_DC_Motor_Speed_Controller_V1.01.pdf`
4. 官方 M3508 资料：`/Users/mzc/Desktop/Surgery_exo skeleton/Main_ project/hardware_list/official_documents/RoboMaster_M3508_Brushless_DC_Motor_User_Guide_v1.0_2025.10.pdf`
5. 官方搭配教程：`/Users/mzc/Desktop/Surgery_exo skeleton/Main_ project/hardware_list/official_documents/RoboMaster_M3508_C620_Mixing_Tutorial_V1.0_2017.10.pdf`

UICRM 中的 `Motor3508` 注释同时涉及 M3508/P19。当前项目使用的是用户已确认的裸 M3508，不得把 P19 减速器的尺寸、减速比或输出转矩直接套用到本项目。

### WT901C-TTL 姿态传感器

1. 官方协议仓库：`./WitStandardProtocol_JY901/`
2. STM32 示例：`./WitStandardProtocol_JY901/STM32F1/`
3. 通用协议核心：在仓库内检索 `wit_c_sdk.c`、`wit_c_sdk.h` 和 `REG.h`

示例目标为 STM32F1，不等于可直接编译到 STM32F446RE；需要替换串口、DMA/中断、时钟和平台回调部分。

### S 型拉压力传感器 + HX711

1. 第一候选：`./STM32-Libraries-HX711/HX711 Load Cell Amplifier/`
2. ExoKit 原有 Arduino 依赖说明：`./ExoKit-main/Arduino API/README.txt`
3. 本地随硬件资料：`/Users/mzc/Desktop/Surgery_exo skeleton/Main_ project/hardware_list/official_documents/HX711AD..资料/`

优先验证当前已经下载的 STM32 C 实现；如果不满足非阻塞采样或控制周期要求，再到上述平台继续搜索其他实现。即使代码可直接编译，也必须通过零点、方向、量程、漂移、噪声、采样率和超时测试后才能进入控制逻辑。

## 当前尚未下载的候选项目

- GIM6010-8 已发现若干第三方仓库线索，但尚未逐个核对 MCU、CAN 模式、反馈格式和代码完整度；编写 J2 驱动前必须重新检索并比较后再决定下载对象。
- `nimaltd/HX711` 是另一个 STM32 HAL 候选。当前先验证已下载的 STM32 HX711 库；第一候选测试不合格时，应继续下载并比较，而不是立即自行编写。
- M3508/C620 目前先用 UICRM-Embedded 作为主代码参考，但正式编写 J3 驱动前仍要再次联网搜索更新、更独立或更接近 STM32F446RE 的实现。

## 当前结论

当前本地库只是部分参考。每个模块都应按“ExoKit 底版 -> 本地参考库 -> 网上开源平台检索 -> 官方资料核对 -> 直接或部分适配”的顺序推进。只有完成联网检索后仍找不到合适实现，才能向用户报告并讨论是否由 AI 从零编写。
