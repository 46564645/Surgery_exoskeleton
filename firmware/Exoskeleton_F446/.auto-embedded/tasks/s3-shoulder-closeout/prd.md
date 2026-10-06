# S2/S3肩侧快速收尾并进入S4的计划

- 创建日期：2026-10-05（Asia/Shanghai）
- 当前阶段：RESEARCH；trace_id：T-s3-shoulder-closeout
- 用户授权范围：2026-10-05用户确认按原计划开始，已执行P1第一姿态只读；随后要求电动前屈约+10°后停住测量。展示新增E1–E5清单后，用户选择“暂时保留现程序”，不实施该定位/保持扩展。后续另行授权单次运动后撤力、报告两侧余量；+20mNm/100ms入口因CAN累计错误在准备/使能前中止，没有发运动命令，继续点见末节。实际方向/比例未落实时不填写可动作假值。
- 唯一阶段/进度来源：[schedule第1.1节](../../../../../Main_%20project/docs/development_schedule.md)。本文件仅保存技术变更范围和审查项；现场成果只写schedule，不建立第二套阶段表。

## 需求 / 验收标准

先完成S3肩侧进入S4所必需的坐标、活动区间、有限运动/停止及异常停止依据。沿用已通过的S2批准范围和2026-10-04单次微动/停止；不等待肘部S3，不恢复已取消的S2掉线/恢复，不新增全覆盖矩阵。S4研究可以先行，带动力S4以schedule第1.1节列出的条件为准。

## 已选路线与轮次

1. 只读定位三个肩姿态：2026-10-05用户报告自然垂臂0位且不能后伸，当前取得该报告0位读数；后两点只在前屈方向已能轻松到达的小区间选取，不尝试−20°。实际测角与原生读数成对，记录方法/精度、安装关系及相对重力的定义；不推到机械止挡。一次会话同时核实际watchdog配置/厂家适用依据，原20项工具未含两字段，不能写成已读。
2. 测量后只填现有集中site，并冻结有限动作计划。候选沿用已验证HSE配置；生产默认禁用与原始验收档案不混称。仅做相关构建和关键换算/门控检查。
3. 一次受监督有限肩请求核对映射、动作和停止；真实异常停机证据只补所采用方式缺少的一项。每次启动一次请求和读取时限照常执行，必要的断电/刷写/新启动放在同一有人值守会话中安排。
4. 满足schedule条件即转S4的同小区间、固定肘姿态重力基线。S3肘部保留到S5前完成；持续输出会话/模型/负载是S4开发项，不能把单次pulse镜像直接延长。

目标为一个集中工作单元；这不是时限承诺。现场测量后若需要候选构建/刷写，存在多个供电段；遇真实错误立即停止该轮并调查根因。

## PLAN技术清单

| ID | 文件/现有接口 | 计划内容 | 验证标准 | review |
|---|---|---|---|---|
| P0 | `Main_ project/docs/development_schedule.md`；本任务文档 | 保存收尾范围、复用证据和推进条件 | 保留历史；S2批准范围通过、S3部分/S4待开始；无固件改动 | false（本轮文档项） |
| P1 | 现有`D:/codex_tasks/2026-10-01/task/s3-shoulder-supervised/usb/read_s3_properties.py`：`read_values(channel, guard, index, report)`、`capture()` | 原固定20项工具在三个可重复姿态取得只读快照，配对外部姿态记录。已读规则要求powered/IDLE/MotorWizard关闭，按实际现场满足后运行；当前不运行 | 正确设备HW3.11.1/FW0.6.5/node1、首末IDLE、输出禁用；真实采集时刻/位置/姿态可对应；不以旧缓存填锚点 | false（不改脚本的只读定位） |
| P2 | 实机schema：`axis0.config.watchdog_timeout` ep153/float、`enable_watchdog` ep154/bool；厂家参数浏览只读入口 | 与P1同会话核查两项实际配置及厂家匹配固件的超时/喂狗规则。原20项工具不包含两字段，不能声称它已读取。优先厂家只读界面；若需要扩大脚本白名单，先补精确代码diff供审查 | 字段存在只说明schema支持；必须区分实际读值、启用状态及是否能在失去CAN时停机。不得调用ep500 watchdog_feed或改参数来伪造事实 | false（厂家界面只读）；脚本改动则true，须先展示具体补充清单 |
| P3 | 候选`App/application/s3_site.c`（L4），`const S3SiteProfile *S3Site_GetProfile(void)`、`bool S3Site_ReadSample(uint32_t now_ms, S3SiteSample *out)`；已验证HSE `main.c/.ioc`覆盖 | 以实测结果建立`S3_ASSEMBLED_JOINT`、肩侧mapping、工作区间/限速、有限输出及停止观察配置。复用已有API，不新增C函数或硬件资源；从唯一活动工程生成候选，原台架归档不改写 | `direction`与实物一致；`rotor_per_joint`包含电机减速及输出连接比例；`rotor_zero_rad`来自同期实物姿态。确认标记只由有效证据填真；SAFE_OFF启动、无自动prepare/arm、仅肩轴 | true（新坐标/现场配置的代码项） |
| P4 | `App/control/joint_state.c`：`bool JointState_FromRotor(const JointMapping *mapping, float rotor_rad, float rotor_rad_s, uint32_t received_ms, bool continuous, JointState *out)`；现有`SafetySupervisor_Check`、`OutputGateway_Replace/Step/Cancel` | 复用换算、限幅、过期撤权、清旧命令、故障锁存。三个实测点进入已有换算公式；不因制定计划改这些实现 | q=s*(2πr−zero)/R；由已测(qA,rA)取zero=2πrA−s*R*qA。数据重算与实物方向/比例相符；边界和陈旧反馈拒绝；正常停止不恢复旧请求 | false（复核/复用）；若失败先查根因再另列最小diff，改代码为true |
| P5 | `tests/s3_capture/capture.py`：`validate_plan(plan)`、`timing_budget(plan, cycle_ms)`、`frozen_plan(path, expected_sha)`、`trial(link, plan, summary, journal, clock=time.monotonic)` | 复用现有coords=2冻结配置/单次记录工具，不新增运动入口。新的配置JSON逐项匹配固件`s3limits/s3timing/s3stopcfg`和实测姿态资料；使用既有float32与hash核对 | 计划本身有效、时序预算满足，单次请求/到期撤权/IDLE/停止后新鲜配对均有原始记录；无自动升档、重试、ackfault；未实现的通信故障试验不能声称该工具已支持 | false（现有工具+新数据）；工具代码改动为true |
| P6 | `tests/run_host_tests.py`已有`s3_mapping`、`s3_interlocks`、`s3_chain_supervised_faults`；CMake WindowsDebug、`tests/check_generated.py` | 仅P3或经审查的代码实际改变后执行有关检查和一次目标构建；未改变的实现/失败场景复用已有证据 | 构建退出0、新增警告核查；实际site数据的换算/边界有检查记录。主机异常撤权结果不代替驱动实际停机；本轮只计划不运行 | false（验证执行） |
| P7 | `Main_ project/Hardware_information_list/hardware_interface_registry.md`及schedule唯一日志 | 取得实物映射/参数新事实后登记来源和适用状态；根据实际结果记录肩侧S3范围与S4继续点 | 没测到的不写通过；仅阶段相关事实入硬件表，临时电源状态不固化为规范 | false（证据与文档） |

P3的候选site若作为可复现覆盖档案新增保存，文件层级仍为L4。时钟只采用已验收档案的HSE bypass/PLL M8 N336 P4，不新增引脚/AF/DMA/IRQ，也不直接写新寄存器；NVIC/CAN/UART保持该档案配置。所有未知数值须由P1/P2真实结果填写，PLAN不提供默认假值或可动作占位配置。20mNm/100ms只属已实测起点，不能自动当作新工况授权；现场冻结方案需说明实际方向、有限区间、幅值/期限及停止条件。

## 停止与S4边界

- 已通过TTL撤权+IDLE+编码器停稳，能复用正常有界动作停止；主机也覆盖失效反馈及锁存。这些不证明完全CAN断开后驱动停止。
- 停机方式优先沿用本台架已有监督和备用断电，不增加未选GPIO、急停采购或通用知识库的整套保护。若决定使用驱动watchdog，先核实际配置与匹配固件规则，任何启用/写参均另列review:true且先确认；不套ODrive其他版本。
- 异常停止验证的具体执行法依所选真实机制确定后补入该轮清单，必须保留承托、低输出、独立切断手段和单次停止条件。只做一项影响S4许可的验证；不盲目带电拔CAN或用mock替代现场事实。
- 速度符号/精确轨迹没有验收，S4初版仅按姿态做重力基线，不用未确认的速度符号推断用户意图。若只读短程移动仍出现明显位置/方向矛盾，回RESEARCH查因；不能通过调增益掩盖。
- S4需要读对应表第17/18/21行的具体模型/合成源码，按本机姿态与负载适配；本轮不假称已实现gravity或允许连续输出。P3–P6的单次动作能力不自动构成持续FOLLOW实现。

## 审查与继续点

原清单进入EXECUTE/P1后，2026-10-05用户确认供电、窗口关闭、报告0位和承托，三条MCU缓存确认输出关闭与新鲜IDLE心跳，固定20项NativeUSB真实读取成功，报告0位对应2.816345453262329rev；证据见tests/s3_capture/evidence-pose-map-20261005T131610516871Z。用户随后无法手动摆位，继续点已改为下节有限电动定位PLAN；不再等待原手动第二点，也不将未确认的135°范围或单点作为已完成映射。P2工具扩展或新增方案若超出已展示清单，仍先补精确审查项；项目全阶段完成判断只在schedule维护。

## 新增PLAN：有限电动摆位后测角（代码尚未实现）

用户改为“前屈约+10°后停住，我来测量”，明确前方无碰撞、可运动。新的角目标/保持能力超出P1只读和P3填现有配置范围；依用户指定workflow，展示清单后确认再进入本代码轮次。此前已批准目标动作不等于已经执行，也不需重新询问是否想要10°。

| ID | 现有文件/接口与拟补接口 | 内容及验证标准 | review |
|---|---|---|---|
| E1 | L2 `App/devices/motor_command.c/.h`；拟补`bool MotorCommand_GdsV22Position(uint8_t node_id, float rotor_position_rev, BoardCanFrame *out)`、`bool MotorCommand_GdsV22PositionMode(uint8_t node_id, BoardCanFrame *out)`、`bool MotorCommand_GdsV22Limits(uint8_t node_id, float rotor_speed_rev_s, float current_a, BoardCanFrame *out)` | 按手册编码0x00C位置、0x00B模式3/3及0x00F限速/限流；little-endian、DLC8、位置前馈先0；拒绝非有限值/无效节点，不在L2发送。仅编码检查不宣称FW0.6.5位置动作通过。 | true |
| E2 | L3/L4 `safety_supervisor.h/.c`、`output_gateway.h/.c`及实际准备实现；拟补`bool OutputGateway_RequestShoulderPosition(OutputGateway *gateway, float rotor_target_rev, uint32_t move_timeout_ms, uint32_t hold_timeout_ms, uint32_t now_ms)` | 类型化区分位置与出力，不改变旧Replace的单位；仍只有gateway提交准备/运动/停止。IDLE下先锁定当次新鲜位置作为目标，限制写入与模式逐项读回后才闭环；固定的小步/限速位置目标、有限到位及保持期限；取消、过期、陈旧反馈/通信错误撤权、IDLE、不重放、不自动换符号/升档。保持需出力，不把IDLE当保持。 | true |
| E3 | L2/L4 `gds_sdo_read.c/.h`、`shoulder_property_feedback.c/.h`、`s3_shoulder_service.c`、`s3_feedback.c/.h`（按实际依赖最小修改） | 为明确选择的位置profile增加目标/限制/模式读回，旧7属性转矩profile保留原合同；同一SDO owner、有界字段和连续新鲜观测。schema已核input_pos270、模式287/288、current_lim249、vel_limit301；禁止仅依据发送接受判就绪。 | true |
| E4 | L4 `s3_bench.c/.h`及实际CLI分发表；候选`s3_site.c` | 增加显式有界位置请求/状态入口，生产默认禁用；沿用HSE实测时钟，不改针脚/AF/IRQ/DMA。实际方向/总比例及前向小工作区间落实后才启候选；从当次新鲜基准转换，不把当前45A/30rev/s照搬为试验限制，不把历史r0写死。先用必要的极小有界动作观察前屈极性，再考虑10°目标；不声称极性已确认。具体动作幅值/期限与姿态保持时间待真实条件落实后冻结。 | true |
| E5 | 现有host验证和`tests/s3_capture/capture.py` | 保留旧转矩测试，只补位置单位/无跳变准备/类型隔离/期限及故障撤权等新增风险；实际新增状态与有限位置采集方案一致。一次相关构建及配置检查；PC只发有限动作并记录，不承担实时环，不把mock到位写成实机到10°。 | true（代码）；构建/核查false |

无新增板资源或寄存器配置；main.c只沿用编排。新代码文件若确需新增，先补L2/L3/L4标记和实际CMake集成，不另建活动固件副本。当前没有足以写成已验证的肩轴符号/总比例/活动范围，相关确认保持未成立，方案不生成可动作假值。若适用reference或具体驱动接口存在缺口，按AGENTS2.1继续核查，不从零编造。

当前决定：2026-10-05用户选择“暂时保留现程序”。E1–E5保留为未执行候选，不开始代码、构建或刷写，不以原100ms转矩请求冒充10°定位/保持。报告0位的第一点及原始证据保留；三点映射和10°动作均未完成。后续是否重启该扩展以用户新要求为准，当前没有再次运行设备查询、改变驱动参数或尝试动作。

## 后续授权与实际中止：单次撤力微动

用户随后明确只运动一次、结束后撤力、不保持位置；方向未知，并最终报告已抬离后侧挡点、两侧各约2°有余量且有承托。这是用户对余量的估计，不是测量限位。已通知沿用现程序+20mNm/100ms一次请求，不保证10°、不升档、不自动重试；E1–E5仍不实施。

冻结计划SHA256为59356cf277ec78b78fe39794ed471be35ef6f34343ff97ab04894d4876d0aaef。13:46:29 UTC实际入口在第4条缓存查询发现肩err=80/last_err=00000010，进入prepare前中止；仅ping/status/canstatus/shoulder/cancel共5条完整回复，cancel确认输出0，串口关闭。没有prepare、arm或pulse，未改变程序/驱动参数，未复位或刷写。证据见tests/s3_capture/evidence-direction-once-20261005T134609551403Z。

继续点回RESEARCH：用户要求查历史处理，已核对10月3日HSE无新增错误窗口及10月4日USB-only单次复位恢复。当前先等待24V关闭、USB保留的明确报告，再沿用一次MCU复位恢复启动/读会话；之后重新上电必须核新鲜无错反馈，不能把累计清零当成CAN根因修复。不自动再次运行动作入口。

### 2026-10-05：一次断电恢复完成

用户对关闭24V、保留USB请求回复“好了”；13:59:53 UTC复用历史CubeProgrammer 2.23.0 HOTPLUG/4MHz/固定SN，仅一次-rst成功，无刷写。14:00:51 UTC固定COM10只读status/outputstatus/s3anchor/shoulder四条：uptime58423ms符合当次复位，输出/request/prep/token0、reader IDLE、anchor UNSET、肩rx/err0，串口关闭；无CAN属性请求或运动。证据见tests/s3_capture/evidence-recovery-20261005T135953303329Z。

当前阶段EXECUTE，只恢复既有启动/读会话，未实施E1–E5。已请求重新打开24V并确认；收到后现有单次记录入口先核新鲜无错IDLE与全部配置，满足时执行此前授权且尚未发送的+20mNm/100ms一次微动，不重复未完成的准备/请求、不升档，不以断电计数0宣称CAN根因修复。

### 重新上电后的复现与当前操作路线

用户确认24V已开且准备观察后，14:04:07 UTC在新的有人窗口进入旧单次记录入口；第4条shoulder查询再次发现累计err29/last_err0x10，prepare/arm/pulse仍均0，仅收尾cancel确认输出0、串口关闭。一次断电复位后错误由0再次增长，不能把本次视为仅旧计数；没有第三次复位或自动动作重试。

随后HOTPLUG只读RCC及CAN1 ESR/BTR，无写入、复位或刷写。RCC 03077B83/22415408/0000100A与历史HSE一致；CAN1 BTR001A0005、分频6/BS1=11/BS2=2/SJW1，名义500kbps。确认本启动实际选HSE与已验收位时序，未测时钟波形，ESR当次0不抹去累计错误。证据见tests/s3_capture/evidence-direction-after-recovery-20261005T1404074080622Z。

用户随后打开/启用MotorWizard，并明确最大前屈位置是手动抬到，不是软件驱动定位。用户选择当前改用MotorWizard操作；继续点转为其截图/导出反馈与真实姿态对应，停止STM32动作尝试，也不在GUI占用期间另开NativeUSB采集。当前软件目标正负号、实测肩角与截图是否对应该最大姿态均未建立。MotorWizard可用于本轮手动方向/角度观察；后续S4仍要求STM32/CAN控制和当前CAN异常的健康依据，不能以GUI能动替代该验收。E1–E5继续不实施，程序不变。


### 2026-10-06用户最新范围覆盖

用户明确忽略控制参数落实、再次有限动作确认、异常停止方式落实三项，且此前要求跳过进一步测角。本轮不再以原P1三点精确测角、P3–P6对应的新增映射/运动/异常停止验收作为收尾门槛。保留既有证据和未验证状态，不虚报跳过项通过；现有固件许可/限幅/故障门控保持。本轮剩余为当前接线下主动CAN位置/速度反馈复测及归档，完成后按用户调整范围记录肩侧S3收尾。唯一进展以schedule最新日志为准；S4实际输出方案另行确定，不自动使能。

2026-10-06最终结论：用户调整范围的最后一项主动CAN复测通过，肩侧S3按该范围收尾。唯一阶段表及原始证据见schedule 23:54–23:55条目；原计划未执行项保留为用户跳过，不写成实测通过。
