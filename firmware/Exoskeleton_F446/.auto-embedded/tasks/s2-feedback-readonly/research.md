# 研究发现

| 关键词 | 来源 | 摘要 | 可信度 | 状态 |
|---|---|---|---|---|
| 当前阶段 | `Main_ project/docs/development_schedule.md:13-15,264-269` | S1 基本完成；S2 仅有双 CAN 原始接收。2026-10-01 GDS 上电后 CAN1 持续收到 `0x029`、DLC 8、全零帧，CAN2 无帧，`output_allowed=0`。这不是有效位置/速度反馈。 | 项目实测日志 | 已核对 |
| 肩侧 CAN 错误 | 同上；`Drivers/STM32F4xx_HAL_Driver/Inc/stm32f4xx_hal_can.h:297`；`Core/Src/board_can.c:41-54` | `err` 从 1 到 11 仅表示 10 次错误回调增量，不能断定全部为同一种错误；最近 HAL 错误 `0x10=HAL_CAN_ERROR_FOR`。当前固件不保留错误时刻或电气波形。形式错误的物理根因仍未知。 | 代码与日志 | 阻塞依赖根因的固件修改 |
| 原始帧边界 | `App/application/can_monitor.c:5-33`、`Core/Inc/board_can.h:7-13` | `seen` 表示启动以来收到过帧，不表示此刻在线；需要用 `rx` 增量和帧年龄区分断电后旧帧。当前主循环尚无设备协议解析。 | 当前源码 | 已核对 |
| GDS68 协议候选 | 匹配手册 Word `ABUIABA9GAAgzpi6wQYoh9D9CQ.docx`、原 PDF rev2.2 第 41–48 页；接口登记第 10.2 节 | 若实物采用 CAN Simple，`0x029` 可拆为 node 1 / command `0x009`，载荷为两个小端 float32 编码器估计值。实物版本、模式、节点与全零含义均未独立读取。 | 厂家资料；实物待证 | 待查实 |
| GDS68 USB 供电 | 同版手册原 PDF 第 16、19、37 页 | Type-C 数据线不能给驱动器供电；手册的配置读取以外部加电并连接 USB 为前提。早期硬件的 USB/CAN 模式切换可能修改配置，必须先确认版本和 IDLE，不在当前未知状态下操作。原计划“断开 24 V 时经 USB 读取”不可保证执行；手册本身也不能证明断电时 Windows 一定不会枚举 USB。 | 厂家手册 | 已纠正执行顺序 |
| GDS68 USB 工具 | `all_hardware_relevant_documents/GDS68_tools/SOURCE.md`、随包《必读》、Windows 被动 PnP | 厂家包含 MotorWizard 2.1.1；Git 中 ZIP 的 SHA-256 `59227465a0125214e0d4ff4b7e0ea74b1a0195b7adc19624675d892cb8cffd61` 与 SOURCE 记录一致，包内为单个 EXE。本机未发现已安装程序或 odrivetool，当前只见 ST-LINK COM10。随包说明要求 libusb 和固件 0.5.13 以上，手册的 odrivetool 路线要求 WinUSB；不能盲换驱动或升级设备。 | 工具来源、哈希与本机枚举 | 等待实物版本/接口核对 |
| 参考源码 | 开源索引第 5、8–11 行；ExoKit `ExoskeletonJointActiveHandle.cpp`；BetaRobot `bsp_bxcan.c`、`drv_djimotor_broadcast.c`、`drv_daemon.c`；Rhino `gim6010_drv.c`；UICRM `DjiMotorBase.cpp` | BetaRobot 的双 CAN 分区、DJI 大端字段和新鲜度组织可参考；ExoKit 仅提供关节零位/符号语义。Rhino 的 `GIM6010_fbdata` 没有实现当前 `0x009` 解码，须按 GDS 手册字段适配，不照搬第三方参数或 RTOS。 | 已读具体源码 | 用于后续 PLAN，不代表实物协议已核实 |
| C620 帧定义 | C620 V1.01 原 PDF 第 24、29、33 页 | 标准帧 DLC 8；反馈 ID=`0x200+实际电调 ID`，字节 0–1 为大端角度刻度 0–8191、2–3 为大端 RPM，4–5 为电流、6 为温度、7 为空。1 Mbps 和默认反馈周期为手册资料，当前实物 ID、终端、周期未证实。 | 厂家手册 | 待实物核对 |
| 现场电源与接线 | 用户 2026-10-01 本轮答复；接口登记第 10.2 节 | 用户确认 24 V 已关闭，随后澄清 STM32 经 CAN-01 到 GIM 的 CAN 连线仍接着；此前将“GIM 没有接线”扩大为 CAN 已断是误解。GIM Type-C 与动力线是否物理插着仍不由当前 USB 枚举判定。 | 用户即时答复及现有接线登记 | 待全部断电阻值核对 |
| 24 V 关闭时的串口基线 | 2026-10-01 11:44:19 与 11:44:23 +08:00，Windows 原生 SerialPort，ST-LINK VCP COM10 115200 8N1；只发 `status`、`canstatus`、`shoulder`、`elbow` | 两轮均为 `stage=S2_CAN_RX output_allowed=0`、CAN1/2 `started=1`、500000/1000000 bps；肩/肘 `rx=0 err=0 seen=0 bus_off=0`，四秒内未增加。`uptime_ms` 从 3637933 到 3642015，说明这两轮间 MCU 未复位；与先前肩侧数万帧的累计值不属于同一未复位计数窗口，不能用来推断此前 `0x10` 的原因。 | 实时只读查询 | 仅断电基线，不是 GIM 反馈验收 |
| 工具与记录 | Windows 被动 PnP 枚举；`build/s2-validation/usb-soak.json`；GDS 工具 ZIP/SOURCE | 系统仅见 ST-LINK VCP COM10 等既有设备，未见 GDS USB；24 V 已关闭、GIM Type-C 连接状态未独立确认，因此枚举结果不能判定线材或设备。既有 9/25 USB 测试发生在无驱动器场景，没有 10/1 错误逐时记录。归档 MotorWizard ZIP 哈希与 SOURCE 一致；包内 EXE 版本资源为 2.1.1.241017 / SteadyWin，无嵌入式 Authenticode 签名，尚未运行或安装。 | 用户澄清、本机枚举、归档检查 | 待正确供电条件下核对 GIM USB |

## 线上原始资料与推论边界

- [ST HAL CAN 源码](https://github.com/STMicroelectronics/stm32f4xx-hal-driver/blob/master/Src/stm32f4xx_hal_can.c)和[STM32F446 RM0390](https://www.st.com/resource/en/reference_manual/dm00135183.pdf)将硬件 `CAN_ESR.LEC=010` 对应到 `HAL_CAN_ERROR_FOR=0x10`，含义是 CAN 帧固定格式位异常；这不是 GDS68 的错误码，也不能由 `0x029` ID 或全零数据直接推出。现有 `last_err` 只保留最近回调位图，无错误时刻、CAN_ESR 接收/发送错误计数或波形。
- 本工程 `main.c` 用 HSI→PLL→APB1 42 MHz，CAN1 配置分频 6、`1+11+2` 个时间量子：名义 500 kbps、采样点 `12/14≈85.7%`。串口的 500000 是寄存器分频计算值，不是实测总线位宽。[ST bxCAN 位时序说明](https://community.st.com/stm32-mcus-60/can-bxcan-bit-time-configuration-on-stm32-mcus-135466)明确不推荐正常模式用 HSI，[ST 接收问题排查](https://community.st.com/stm32-mcus-60/can-reception-issues-reasons-and-general-troubleshooting-135452)同时列出配置、供电、终端、共地和干扰等方向。HSI 时钟偏差是优先核验的候选，尚非本次错误的已证根因，不因此直接改 PLL。
- 官网归档一致的[GDS68 rev2.2 手册](https://14180476.s21i.faiusr.com/61/ABUIABA9GAAgzpi6wQYoh9D9CQ.pdf)第 4.1.1、4.1.2、4.1.5 节：CAN Simple 的 `0x029` 可拆为 node 1 / `0x009`，该命令的电机反馈为两个小端 float32（转子位置 rev、速度 rev/s）；手册给出编码器估计周期上报的出厂描述值 10 ms，实际设置仍待实物读取。官网手册引用的[厂家 C SDK](https://gitee.com/cyberbeast/mwmotorsdk/blob/master/MWSDK/MWMotor.c)中 `MWGetEncoderEstimates` 也构造同 ID、DLC 8、全零载荷作为查询请求，接收函数仅按两个 float 解码。因此 ID/DLC/全零本身不足以证明观察到的是有效反馈；当前 STM32 应用没有发送该请求，若现场确为 STM32 与 GDS68 两节点，持续 `0x029` 更符合驱动器自主周期帧，但仍须核对实物配置和反馈真实性。未找到厂家把全零载荷定义为某个故障状态的依据。
- [TI SN65HVD230 数据手册](https://www.ti.com/lit/ds/symlink/sn65hvd230.pdf)说明总线通常两端各接 120 Ω；两只并联的理想断电读数约 60 Ω。接口登记此前约 120 Ω 只说明需要核对完整线束和远端终端。[GDS68 手册](https://14180476.s21i.faiusr.com/61/ABUIABA9GAAgzpi6wQYoh9D9CQ.pdf)未说明软件控制的终端在断电时是否保持接入，所以断电阻值也不能单独判断上电后的终端配置。[STM32F446 ES0298 勘误](https://www.st.com/resource/en/errata_sheet/es0298-stm32f446xcxe-device-errata-stmicroelectronics.pdf)的 bxCAN 条目仅涉时间触发通信模式，本工程未启用。
- 可证伪顺序仍是读取实物硬件/固件版本、协议、节点和波特率，保留只读静态上电前后 `rx/err` 差值；如具备独立分析仪或示波器，断电接入后以只监听方式观察位宽、错误帧和载荷。现有 STM32 正常模式即使没有应用控制帧也会由硬件参与 ACK；不能为“静默”直接改双节点 MCU 为 silent 模式，避免改变待测网络行为。尚无证据可将帧格式错误、全零载荷或单个终端读数归为同一个根因。

## 知乎与开源案例复核（2026-10-01）

- [北汇信息在知乎发布的受控实验](https://zhuanlan.zhihu.com/p/2058847595645162808)用专用设备分别干扰 EOF、CRC 界定符和 ACK 界定符，均可观察到格式错误；只能借鉴“错误码无法定位具体受扰位，需抓错误帧/波形”的判断方法，不能推定本项目是哪一个原因。
- [GIM6010-8 开源使用示例](https://github.com/nguyen-v/gim6010-8_ros2_docker#starting-can-bus)说明 500 kbps 是默认值，主机速率须与电机**当前设置**相同；它不包含本项目旧 24 V 驱动板的配置，也没有解释 `0x10`。[Zephyr 的 bxCAN 驱动](https://github.com/zephyrproject-rtos/zephyr/blob/main/drivers/can/can_stm32_bxcan.c)分别统计格式错误并读取接收/发送错误计数，可作为后续诊断信息设计参考，不是当前根因证据。
- 未找到可核实的 GDS68 `0x029` 全零帧同时伴随 STM32 `HAL_CAN_ERROR_FOR=0x10` 的同症修复案例。手册默认节点 0 与现有帧按 CAN Simple 推出的节点 1 不一致，不能以默认值覆盖现场观察。手册允许在外部供电时经 USB 只读查看版本和部分通信参数；早期硬件的 USB/CAN 模式互斥与工具固件/驱动要求须先核对，不能为读取而切模式、保存设置、校准或升级。本次只查资料，不恢复台架操作。

## 下一项可证伪检查

下一步保持现有肩侧 CAN 连线不动，全部供电断开后测 CAN-01 H/L 和 GIM 动力 0 V 到 CAN-01 GND，记录完整总线阻值及公共参考地。之后在确认台架固定、可立即断电且轴保持 IDLE 时，按手册要求受控静态供电并只读获取实际配置，再用现有 `status`、`canstatus`、`shoulder` 比较静态上电前后及关闭 24 V 后的 `rx/err/age/bus_off` 增量。若错误仅随 GDS 上电出现，也只能把范围缩到该支路，不能直接归因于终端、线束或位时序；针对性实测后才决定修正。

## 2026-10-01 肩侧离线收敛：下一轮最小区分证据

本节更新上文“下一项可证伪检查”的优先顺序，不重复已经报告的肩侧120Ω、肘侧完整60Ω或接线自查。仅重读既有schedule第264–296、320–327行、当前board_can/main/console源码和已缓存GDS68 rev2.2第41、48、54页；本轮未产生新实物证据，根因仍未知。

**已证实及不能推出的结论：**肩侧曾收帧计数10549→10605、29505→29672，最近快照0x029/DLC8/全零，年龄2–9ms；err=1→11只计错误回调，最近0x10为HAL Form error，不代表十次均为该错误或同一原因。主机500kbps为42MHz/(6×14)的名义值，采样点85.7%、HSI→PLL，未测实际位宽。肩侧历史约120Ω不确定软件终端上电状态。rev2.2默认CAN Simple/节点0/500kbps、编码器10ms及心跳100ms不能替代实物配置；若实际CAN Simple，0x029才对应节点1/0x009、两项小端float32转子rev与rev/s。仅最新帧查询不能证明没有其他ID/心跳，也不能证明所有收到的帧均全零。

**分开处理两个问题：**0x10由CAN控制器检查固定格式位产生，应用层ID/载荷映射和接收滤波不修复该错误；全零载荷本身是合法数据，也可解为两个0.0，并非CAN格式故障证据。厂家SDK还以同ID/DLC8/全零发送查询，但本应用没有发送该请求；这排除了本应用生成查询的解释，不能据此断言现场不存在其他发送节点。大量已接收帧使“始终完全断路/始终完全无法匹配速率”的解释不足以覆盖现象，仍不能排除间歇供电、反射或时钟偏差。

| 待排除假设 | 最小区分证据与现成只读方法 | 决策边界 |
|---|---|---|
| 协议/节点/反馈设置与手册默认不同 | 先识别实物硬件/固件版本；版本适配的厂商工具仅查看当前接口、协议、节点、baud、周期和终端配置 | 默认值不写回。硬件≤3.7存在USB/CAN互斥；若读取须切模式、保存、换驱动或升级，停止并报告，不把读取当无副作用 |
| 供电切换瞬间错误或稳定带电持续错误 | 现场精确记录开/关24V时刻；同一MCU运行窗口用现有status/canstatus/shoulder保存uptime、rx/err差值、last_err、age、bus_off，覆盖稳定带电段 | 只区分发生条件；计数在断电后不再增加不等于故障修复，串口采样时刻不是错误精确时刻 |
| 终端/参考地/线束信号质量 | 先读取实际终端配置并利用既有120Ω记录；若仍指向该分支，再针对未知远端终端状态或参考地做一次有目标的断电检查；有仪器时看CAN差分/共模、振铃及错误帧 | 不要求整套接线复查或重复肘侧测量；带电不测欧姆，120Ω不能直接判定缺一个终端 |
| 两端实际速率或HSI时钟偏差 | 已有CAN1名义配置与驱动器实际baud对照；独立分析仪只监听抓位宽/错误位置，必要时示波器测MCU时钟与总线时序 | 500kbps位时间名义2µs；无实测不能归因HSI，更不能先改PLL/位时序试错；正常模式MCU硬件仍会ACK，不为静默改变待测网络 |
| 全零是合法零点/反馈未建立或非预期消息 | 协议确证后，厂商只读显示编码器状态与同一时刻独立转子读数；如已有独立监听抓全量ID及载荷，区分请求/周期反馈 | 静止零值不等于有效性已验收；本轮不校准、不查询使能、不要求移动轴；缺独立证据保留未知 |

**明天现场最少动作：**(1) 保持当前断电、GDS动力隔离，先识别标签/版本和可用工具/仪器；不重新接线或重测已知阻值。(2) 主对话先说明并确认下一次受控静态供电边界及未核实的启动配置；版本兼容且能只读时才获取实际配置，不点击启动/校准/模式切换。Type-C不能单独给该驱动器供电，不能承诺24V断开时能读配置；output_allowed=0也不证明驱动器自身IDLE。(3) 条件确认后，只在一个记录开关时刻且MCU计数连续的短观察窗口采现有三条只读查询；若稳定带电仍增错，再按对应假设补一项区分证据。没有仪器/兼容只读入口就记录该阻碍，不以重复上电或盲改固件替代。当前不打开串口、不烧录、不上电、不操作电机；肩侧固件修改门槛和S2双路验收均未满足。

## 2026-10-02 本地详细资料复核：版本读取路径与CAN1位时序

先读已提取的对应Word正文，再回查原PDF页图/正文；重读hardware_components_reference.md第5.6节、GDS68_tools的SOURCE/必读及ZIP目录。ZIP只有MotorWizard2.1.1.241017.exe（10385059字节），无额外配置说明或已读实机参数；未执行EXE、安装驱动或访问硬件。订单“旧24v/GDS68”不确定硬件/固件版本。

- **资料已经给出读取方法，无需先把拍标签作为强制步骤：**rev2.2第20页/3.1.3明确连接成功后“电机参数”页顶部显示硬件、固件版本；第34页/3.1.8列出hw_version_major/minor/variant、can.config.baud_rate、enable_r120，第27页给出axis0.config.can.node_id。文档GUI示例3.8.3/0.5.13、命令表“目前3.8”、同页ODrive3.6/0.5.6终端截图都是示例，均不是本项目已读实物版本。不能将手册修订rev2.2、工具版本2.1.1或旧24V订单转换为驱动板版号。
- **两条USB路线区分：**MotorWizard随包必读要求libusb及“0.5.13以上”固件；手册第18页的Windows odrivetool路线要求CyberBeast Motor Driver Device Interface2使用WinUSB。不同客户端的驱动要求不能混用；本地资料仅提供候选读取路线，不证明当前固件与新版客户端兼容，不为读取先升级设备或无差别换驱动。第27页说明修改ID后点击“同步”会存入驱动器，故后续查看参数不点同步/保存。
- **USB/CAN限制及启动边界：**第37页≤3.7互斥条件明确，但实物是否满足条件仍未知；CAN→USB须先IDLE再Disable_Can，且第52页明确Disable_Can(0x01E)会禁用CAN并重启、Save_Configuration(0x01F)会存储并重启，均非只读。第25页“上电校准”需点击启动/确认，并不等于已知该实物上电自动校准；资料未证明实物保存的启动配置或IDLE状态。Type-C仍不能单独供电。不能为版本读取运行校准/控制示例、切模式、清错误或复位。
- **两个0x10严格分开：**board_can.c的last_error来自HAL_CAN_GetError；本项目HAL头第297行0x10=Form error。厂家PDF第96页的电机0x10=CONTROL_DEADLINE_MISSED属于另一个错误域；未收到/读出该电机错误，不能据HAL的0x10修FOC或校准电机。
- **CAN1纯源码核对（不是新增实测）：**main.c第152–174行与.ioc一致：HSI名义16MHz→PLLM16/N336/P4→SYSCLK84MHz→APB1除2=42MHz；CAN1 Prescaler6、BS1=11、BS2=2、SJW=1，正常模式。总TQ=1+11+2=14，TQ=142.857ns，bit=2µs即500kbps，采样点(1+11)/14=85.714%，SJW=142.857ns。board_can取HAL_RCC_GetPCLK1Freq，不误用84MHz的APB定时器倍频。与GDS手册第41页默认500kbps数值相同，但驱动器实际baud、采样点和晶振偏差未知；不套用别的社区设备位时序或直接改PLL。本次同源CAN2的C620零错静态记录排除不了CAN1/GDS两端互操作问题，也不能单独认定HSI根因。

**最小下一步：**先利用已归档资料选候选只读客户端；待主对话明确一次肩侧受控静态供电边界后，只做一次现有Type-C连接识别及版本/通信参数查看。可连接则在同次查看中合并版本、协议/节点/baud/终端与状态记录；不能连接则保留枚举/错误证据并停止，不重复上电、不发送Disable_Can或切换驱动/固件。实机配置仍是证据缺口，照片只在读通道确实不可用且能提供版本线索时再考虑，不在文件核查前要求用户补资料。不访问串口/硬件、不改肩侧固件、不增加肘侧测试。
