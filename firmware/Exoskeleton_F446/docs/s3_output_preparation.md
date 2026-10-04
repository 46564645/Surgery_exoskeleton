# S3 输出软件接口与启用边界

这是已批准S2→S3方案的源码与启用边界说明，唯一进度日志在[开发排期](../../../Main_%20project/docs/development_schedule.md)。S2当前批准的可信连续肩反馈和一次真实变化已通过；2026-10-04一次受监督肩微动已实际prepare、闭环使能、arm/pulse并完成停止。不能继续以旧失败窗口的“从未使能”描述当前状态。

本次100ms/20mNm请求获接受，零目标/模式及新鲜闭环心跳符合门控；TTL撤权后驱动IDLE、250ms停稳观察通过，终态FAULT/EXPIRED且output/request/token0，原点UNSET→ACTIVE→RETIRED，锁存ACTIVE后至RETIRED身份不变。方向/零位、精确动态、肘部运动及其余S3现场项目未全部验收，因此S3仍为部分完成。实物掉线/恢复仅从本轮S2取消，未执行，不写通过。

当前已刷镜像为HSE相对台架ELF762c0902a4269f3056c32e75b2cb99039d9a45f22678aac41364c111500bcf54（BIN91148B）；2026-10-04只复位一次，未再次刷写。主仓默认site禁用/HSI仍保留，镜像差异见验收记录；以下较早上板、未动作描述保留历史时点。原始[本次记录](../tests/s3_capture/evidence-relative-20261004T013713353068Z/summary.json)与[S2变化判定](s2_encoder_stream.md#当前验收证据2026-10-04)记录了全部实际边界；目标转矩受理不证明实际达到20mNm。本轮归档并提交至原GitHub仓库，远端结果以对应Git提交记录为准。

## 实现与实际可达性

| 层/文件 | 当前行为 |
|---|---|
| L1 `board_can_tx.c/.h` | 按显式轴集合有界发送、旧邮箱abort及空闲确认；停用总线不校验帧、不执行HAL或TX。旧API以集合3兼容；无软件队列、不加TX中断，默认未绑定。 |
| L2 `motor_command.c/.h` | C620 分组发送编码，未选择槽清零；GDS rev2.2 SIMPLE 的 Set_Input_Torque/IDLE、力矩输入模式和闭环状态准备帧编码；纯函数不发送。 |
| L3 `joint_state.c/.h` | 连续转子量按已确认映射转换；拒绝未知配置、非有限值和冒充连续量的模角。C620裸轴的单圈扇区由L4独立处理，不称连续量或装配关节坐标。 |
| L3 `safety_supervisor.c/.h` | 只检查启用轴各自的反馈/协议、映射、限制及反馈新鲜度；停用轴的未知映射/状态不阻断。公共保护、停止、UART与服务时序仍参与门控；任一启用关键链异常撤销全部启用轴许可。 |
| L4 `output_gateway.c/.h` | 唯一提交点，按GDS/C620各自证据Prepare→READY→人工Arm→静态单轴请求；限幅、斜率、寿命、替换/取消、故障锁存及撤销旧待发帧；无动态对象。 |
| L4 `output_can_transport.c/.h` | 传递启用轴集合至L1；只触碰启用总线。生产仅在该集合配置与可信来源就绪时绑定，当前默认条件不满足。 |
| L4 `s3_bench.c/.h` | 从s3_site集中读取启用集合与确认项；接入启用CAN总线、公共UART/服务周期健康和人工命令。默认集合0、transport为空，SAFE_OFF无输出。 |
| L4 `s3_feedback.c/.h`、`s3_site.c/.h` | 从CanMonitor读取逐帧GDS档案观测和C620真实反馈，分别保留位置/心跳时间戳与driver[2]状态。WireValid不授予许可；裸C620反馈静止观察已提供，外部停止及GDS校准/零目标/模式来源仍未提供。 |

无参数只读命令 `outputstatus` 仍保留。以下默认禁用状态在2026-10-02 13:30 USB-only及13:46带电只读窗口均已观察；不代表后续停止策略已板测：

```text
OK stage=S2_CAN_RX s3_state=0 reason=1 configured=0 transport_bound=0 output_allowed=0 request_valid=0 stop_attempted=0 prep_state=0 arm_token=0
```

状态编号：0 SAFE_OFF、1 ARMED、2 ACTIVE、3 FAULT、4 PREPARING、5 READY；原因1表示配置未就绪。`prep_state`为0未准备、1等GDS零目标读回或C620零命令后的新反馈、2等GDS模式读回、3等GDS闭环读回、4准备就绪。当前生产 `enabled_axes=0`、确认项false/0、实际发送未绑定；默认prepare/arm在发送前被拒绝。

`enabled_axes`位0肩、位1肘：0禁用、1仅肩、2仅肘、3双轴，非法位拒绝。集合只在冷启动初始化时复制，CLI不能修改。每轴 `feedback_confirmed` / `protocol_confirmed` 取代整机S2完成布尔；整体验收仍如实记录，不靠将布尔置真满足门控。

人工命令为无参数`prepare`、`arm`、`cancel`、`ackfault`；限时请求为`shoulderpulse <arm_token> <signed rotor_mNm integer> <ttl_ms>`和`elbowpulse <arm_token> <signed current_mA integer> <ttl_ms>`。沿用有界整数解析，拒绝溢出、额外参数、NaN和过期/错误令牌；已识别pulse的非法参数撤销许可并锁存故障。签名输出单位延续下节的电机侧量/关节正方向，编码层再转换符号。未知命令仍无输出。没有校准、保存配置、在线修改确认项或Flash写入命令。

Prepare在具备真实确认项时请求驱动准备，Arm授权输出；它们不是只读命令。2026-10-02 16:09的HSI刷写与16:11默认拒绝是历史证据；2026-10-04已在独立HSE相对台架候选上完成一次pulse与停止验收，见开头当前事实。

## 单位与协议边界

- `S3_ASSEMBLED_JOINT`使用 `q = direction * (rotor_rad - rotor_zero_rad) / rotor_per_joint`，速度同样除以比值；`direction`只能为+1/-1。它需要可信连续转子参考与真实装配映射；C620此来源仍未绑定。未知裸M3508传动不得套P19；GDS界面8.00是配置读数，不等于已验证机构比。
- 裸C620可显式选择 `S3_BENCH_ROTOR`：使用当前真实8192刻度单圈编码器，映射必须1:1、`rotor_zero_rad=0`、方向已确认；需 `bench_sector_confirmed`、逐轴反馈/协议/限制确认及 `trusted_feedback_axes`，且现场配置的C620 ID必须与实际接收配置一致。按方向将上下限反算并排序后，必须严格满足 `0 < rotor_lower < rotor_upper < 2π`，避开编码器绕零；坐标是台架编码器扇区，不称连续转子量或肘关节角。它不依赖或设置 `rotor_reference_confirmed`，不补造圈数。超时/越界仍撤销并锁故障，新反馈不恢复旧许可。
- 请求按本次确认坐标的正方向表达**电机侧输出量**：肩为转子Nm，肘为发送电流A，按方向转换符号。没有关节Nm→电流转换，也不用未知力矩常数或效率拼扭矩模型。每次只选择一个启用轴，其他启用轴候选为零；停用轴不TX。换轴须先取消再重新人工Arm。
- C620厂家V1.01手册第31–33页 CAN通信协议：标准DATA、DLC8，ID1–4用0x200，ID5–8用0x1FF，大端有符号16位；±16384对应发送电流±20A。反馈0x200+ID只含角度、RPM、实际电流、温度和Null，没有目标电流echo、轴状态或校准状态。编码量程不是S3运行限值，也不证明反馈 `current_raw` 的A/计数倍率；禁止默认用20A运行。
- SteadyWin rev2.2 §3.1.5 ID0–63；§4.1.1 11位 `(node<<5)|cmd`、8字节、小端IEEE754；§4.1.2 Set_Input_Torque(0x00E)首4字节float32映射 `controller.input_torque`，剩余字节清零；Set_Axis_State(0x007)首uint32=1表示IDLE，剩余清零。按手册controller字段映射采用转子量；§3.1.7特指CAN MIT输出轴量，不能将MIT倍率套到SIMPLE。
- **GDS编码仅是rev2.2手册档案格式**。实物HW3.11.1/FW0.6.5兼容性、真实反馈对应与停止效果未确认，肩侧 `protocol_confirmed=false`。没有复制通用ODrive广播约定或Rhino的MIT倍率。driver[0]已有真实档案观测，但GDS校准/零目标/模式读回仍false；driver[1]只在真实C620反馈与显式现场operational确认满足时有效，不伪造轴状态或echo。原始收帧或HAL发送接受均不能单独置驱动就绪。

## 生命周期与停止

配置及启用集合只在冷启动/复位时复制，CLI不能修改；初始化始终SAFE_OFF，不恢复许可或旧请求。Prepare检查启用轴的可信坐标、逐轴反馈/协议与限制，及公共保护/停止/时序。

- 启用肩侧时，GDS需健康IDLE与校准证据，经唯一出口依次请求零目标、力矩模式、闭环；每步须比发送前更新的对应读回，闭环须健康状态8心跳。当前校准/零目标/模式实际提供者仍未完成。
- 启用肘侧时，C620需真实反馈、现场已确认 `operational_confirmed[1]`，Prepare和Arm前按显式停止策略检查：外部路径需要 `independently_stopped`；仅肘裸转子台架使用 `feedback_stationary`，两者不互相冒充。发送零电流后等待比提交时刻更新的有效C620反馈；仅肘时可进入READY，双轴时继续肩侧流程。没有C620目标echo要求，不填 `zero_verified` 或GDS轴状态。发送入队与新反馈只证明提交和反馈可观测，不证明零命令执行或电机已停。

准备/READY等待超时或健康损失撤销全部启用轴，READY不自动Arm；不声明完整产品的Flash故障持久化。

Arm只授权软件提交，要求READY、全部启用轴反馈及相关确认项满足，并先清除启用运输层旧帧。每次成功Arm发放非零单次递增令牌，取消/故障清除令牌；同一次启动内旧令牌不能复用，计数耗尽拒绝新Arm，不宣称跨重启持久令牌或安全认证。ARMED无请求也按确认的max_request_ms超时。Replace仅替换一个静态请求；非法数、超范围、非法寿命、未取消换关节、旧请求已到期或服务间隔异常均撤销许可，不能让旧动作继续。请求寿命使用uint32毫秒差，边界为`age >= ttl`失效；反馈为`age > timeout`失效；时长必须小于2^31。服务超限跳过旧动作，不补跑积压周期。正常候选按已确认的每秒斜率限制过渡；启用轴反馈越界/过速、断连、公共停止输入或驱动未就绪立即撤销全部启用轴。

撤销顺序是先清全部软件许可/请求，再purge启用轴旧正常帧，最后通过同一提交点对启用肩请求IDLE、对启用肘请求零电流；停用轴不TX。Stop提交只尝试一次并保留结果，不自动重试、重置或清故障。取消不会清掉FAULT；恢复须满足所选停止策略的证据、健康恢复及旧帧清除后显式ackfault，再重新Prepare/Arm。台架停止投递失败在本次启动内不可ackfault解除，详见下一节。

`stop_accepted`仅表示运输接口接受停止帧，**不证明电机停下**。L1发送适配器遵循下列契约；当前默认未绑定，实物行为未验证：

1. `purge=true`前确认启用轴旧正常bxCAN邮箱均已失效，不能把“已请求HAL abort”冒充完成；生产无软件待发队列。
2. 帧槽0肩CAN1、槽1肘CAN2；只同步复制/发送启用槽，停用总线无HAL/TX操作，不保存栈指针或补发积压正常帧。
3. 忙、部分提交、错误或清理失败必须返回false并留下证据；即使purge失败也尝试停止请求，FAULT不能隐藏仍待发旧帧的事实。
4. 公共串口/调度及启用CAN健康已接入，反馈与现场可信确认分别门控；实际断连/复位/末指令/停止行为仍需现场确认，并配合独立停止/机械支撑。软件撤销不是硬件急停。

实际L1仅对启用总线读取HAL LISTENING/bus_off/空闲邮箱；正常批次要求启用轴旧邮箱全空，每批最多两次AddTxMessage，部分成功返回false。purge每个启用总线最多一次AbortTxRequest并立即再查空闲数，不循环等待；ABRQ未实际完成则返回false并锁FAULT。停止时某启用帧非法或发送失败仍尝试其他启用轴，返回失败并保留证据；不自动重试，需要独立停止方式。真实abort可能跨服务周期，当前保守接口的实物适用性仍未验证。

模拟通过不证明硬件成立。`s3_site.c/.h`集中保存现场边界，当前只填已读节点ID，`enabled_axes=0`且确认/限制/时序默认false/0，生产transport仍为空。`S3Site_ReadSample`现调用真实 `s3_feedback` 从CanMonitor获取两轴观测；原始观测与 `wire_valid_axes` 不授予可信坐标或输出许可。外部停止、GDS校准/零目标/模式提供者仍未完成；受监督C620台架已接入真实接收静止观察，C620 operational和扇区确认尚未落实，装配关节连续参考仍缺失。不得用几个布尔代替真实证据。ARM编译包含真实s3_site/s3_feedback，不包含测试s3_site_mock。

主循环在处理命令前检查启用总线BoardCanStats（启动、错误/丢帧/溢出/非法帧/bus-off）、公共UART故障和服务间隔；pulse替换前再检查。故障确认需所选停止策略的证据与全部启用轴新鲜健康，启用肩还需IDLE；不自动恢复Prepare、Arm或请求。

## 受监督裸C620台架的停止策略（当前离线实现）

`SAFETY_STOP_EXTERNAL_INPUT=0`沿用真实外部停止输入与独立停机证据要求，当前生产没有该提供者；不把输入固定置真。显式的`SAFETY_STOP_SUPERVISED_C620_BENCH=1`只允许`enabled_axes=2`及`S3_BENCH_ROTOR`。它使用现有软件cancel、请求TTL、反馈/服务超时与输出边界；不假设已安装GPIO急停。生产启用集合仍为0，现场参数仍未填入。

人工拔24V是用户已确认随手可用的备用停机方式，MCU没有电压/独立急停状态来源，不声称能读取其状态。每次人工prepare表示开始当次有人值守的台架过程；不存储永久“人在场/已拔电”布尔。软件停止仅在MCU和通信仍运行时能够执行，零电流允许滑行。

`c620_standstill.c/.h`对每个成功解析的目标ID反馈积累`feedback_stationary`：接收时间连续250ms、各帧rpm为0、整段编码器跨度不超过2tick、相邻帧及最新帧年龄不超过50ms；同一毫秒帧仍检查运动但不增加时长。异常、倒序、失联、非零rpm或累计漂移清窗。2tick约0.001534rad/0.08789°，这是依据编码器分辨率选择的首次台架工程判据，不是厂家停机认证；不使用未确认倍率的`current_raw`判断停机或零力矩。

Prepare零提交后及撤权/停止后清除旧窗口，并以事件完成时间排除此前已接收但仍在队列中的帧。Arm/ackfault必须等待事件后的新窗口。Prepare在一次调用中失败并停止成功也会作废旧证据。反馈显示静止不证明动力已断开或无力矩；外部停止字段保持未提供。台架ACTIVE再次pulse会撤权并锁故障，不能刷新TTL无限续发。

`stopstatus`为无参数只读查询，分别报告`policy/source=c620_feedback/seen/fresh/stationary/dwell_ms/age_ms`与`stop_attempted/stop_accepted/purge_ok/stop_delivery_failed`。发送接口接受、反馈静止、人工断电分别记录，互不替代。若撤权时清旧帧或停止投递失败（或Arm清帧失败），`stop_delivery_failed`锁存，本次启动不能ackfault解除；先人工切断24V，再重启控制器，之后重新获得反馈并prepare/arm。软件不声称自动检测了断电。普通准备提交失败而随后停止投递成功，可在新的静止窗口及健康恢复后人工ackfault。

首次候选仅供下一硬件窗口确定：C620单轴`+50mA、100ms`、电流上限0.05A、斜率1A/s（约50ms爬升）、速度撤权阈值1rad/s。厂家C620 V1.01第31–32页给出±16384对应±20A；50mA向零编码为raw40，即48.828mA、满量程约0.244%，理想指令积分约0.00375A·s。该选择限制首试电流和时间，不保证克服齿槽或移动；禁止自动升档。电机Kt与C620命令电流口径未证实，不承诺力矩/位移。候选扇区为此前2.636146rad附近±0.25rad（约±14.3°），必须先确认实物允许、重新核对当前位置和方向；它是撤权边界，不能保证惯性滑行停在边界内。裸轴不套P19减速比。100ms同时限制现有ARMED等待，需本地成套脚本读取token后及时发一次pulse，不自动续期。

下一窗口只缺正常运行指示/目视停转、允许方向与窄扇区及当次值守；拔24V可达已有确认，不重复索取宽泛授权。S2可信连续肩反馈及一次小幅手动变化另行完成；本轮S2实物离线恢复已取消、未执行，不由本软件改动冒充通过，也不免除S3停止与断连处理。

本次对应开源索引27/28/29行，实际复核ExoKit `CmdReactionRunner::cancelReaction`及`ExoskeletonHandle`范围/停止处理、BetaRobot `DaemonReload/DaemonTask`。复用撤销和接收更新/超时的职责；新增本项目的窗口观测与台架策略，不复制裸GPIO、阻塞退出或永久许可。

刷写前最少软件验证：`s3_standstill`、`s3_bench_stop_policy`、`s3_bench_stop_cli`、既有`s3_cli`四组通过，包含接收时间边界、停止后旧证据作废、取消/TTL、停止投递失败锁存与外部路径保持；WindowsDebug编译通过。本轮不重跑未受影响全套测试和既存静态告警检查，当时未运行ASan/UBSan、未烧录或驱动电机；后续16:09已刷入并确认默认禁用，未验收实际停止。软件证据保存于`D:/codex_tasks/2026-10-01/task/s3-stop/`，`host-final.json`为最终主机结果。

## GDS档案观测与只读查询

L2 `gds_feedback.c/.h`仅在显式选择`GDS_PROFILE_V22_FW_GE_0513`时解释rev2.2档案：节点0–63、11位(node<<5)|cmd、DLC8；CMD009小端float32转子rev/rev/s换rad/rad/s并拒绝非有限数；CMD001按≥0.5.13布局读取axis_error、axis_state、fault低四位及life，处理life255→0、新鲜度和连续性。未选择profile拒绝解码。

已优先读取厂家Word正文/表格并回查原PDF41/44–45/48页。CanMonitor现在显式选择档案profile并逐帧解码，同时保留原始查询；实物0.6.5兼容性仍未确认，不宣布肩侧Form Error修复，不把原始0x298猜成节点1估计帧。档案wire_valid只代表该格式下的字段与新鲜度，不代表真实反馈对应、坐标参考、校准或驱动就绪。

新无参数只读命令 `shoulderfeedback` 输出 `profile=v22 verified=0`、`est/hb/wire_valid`、`pos_mrad/vel_mrad_s`、独立 `est_ms/hb_ms` 及 `state/error/fault/output_allowed`。每路保持各自接收时间戳；单独新心跳不能让旧位置样本变新。该命令已于13:30/13:46在7ca3f2ac HSI镜像查询；全零位置/速度仍不能证明非零量值、符号与物理运动对应。

## 实际参考映射

先查根AGENTS及[开源借鉴索引](../../../Open-source%20projects/开源项目借鉴索引表.md)，使用方式为逻辑/接口借鉴，未逐字复制源文件。参考目录未修改。

| 索引行 | 已读来源/函数 | 采用与舍弃 |
|---|---|---|
| 7/11 关节接口/坐标 | ExoKit `exo/ExoskeletonJointActiveHandle.cpp`：getPresentPosition/getVelocity/tare/setJointMode | 方向零位语义，显式添加传动比；舍弃Dynamixel/EEPROM倍率、tare后隐式写目标和模式切换后恢复torque。 |
| 25/28 范围/安全 | ExoKit `exo/ExoskeletonHandle.cpp`：RomKillSwitch.checkJoint/exec、EmergencyBtn.exec/shutdownJoint | 任一关键链异常关闭全部；仅右臂两个关节，不复制左右六轴遍历、exit(1)或裸GPIO。 |
| 26 启动 | ExoKit `exo/builder/ExoskeletonCalibrator.cpp`：构造函数/calibrate | 初始输出关闭；未移植阻塞询问、自动标定或EEPROM恢复许可。 |
| 27 请求 | ExoKit `com/serialreactions/CmdReactionRunner.cpp`：launchReaction/cancelReaction | 新请求撤销旧请求；静态单槽替代new/delete，并增加TTL、故障与清帧约束。 |
| 5/28 CAN/失联 | BetaRobot缓存 `beta_bxcan.c` CANTransmit、`beta_daemon.c` DaemonReload/DaemonTask | 显式发送失败、更新新鲜度/离线语义；未复制HAL等待循环或FreeRTOS任务。 |
| 9 C620 | BetaRobot缓存 `beta_dji.c` DJIMotorBroadcast_Send/电流参数；UICRM缓存 `uic_dji.cpp` DjiMotorBase::TransmitOutput；厂家C620手册 | 固定分组/大端槽/发送量程；其力矩模型、PID与反馈电流声明不直接继承。 |
| 8 GDS | Rhino缓存 `rhino_gim.c` SWSetAxisState/SWSetControllMode/SWMitControl/SWEstop；SteadyWin rev2.2手册 | 节点/命令组合，缓冲全清零；MIT整套编码与倍率不适用SIMPLE，改用厂家明确字段。 |

ExoKit是主仓库存档目录，快照tree为`04bff2f9710e8df74c7be29dc3d05d176f542a9e`；CmdReactionRunner blob `188bd3b5da461cda0e79d066515258eca9e08ecd`，JointActiveHandle `f2e40970d04817526c1011ff529f3cb1cc6796b0`，ExoskeletonHandle `6cac283661e7bab83465ba9c4ad802082e42ed15`，Calibrator `ce201e9b1369c85f7f6f1df88cd1552cb983b398`。其他参考子模块未展开；实际读的是原S2缓存中上述指定文件，并记录SHA256于本轮 `reference-files.json`。索引的仓库pin并不替代缓存文件字节核验：BetaRobot c83dd84、Rhino a4b24b8、UICRM225427b。

## 必要验证与剩余工作

以下前轮记录按各自时间保存；最新板上状态以本文开头的17:00连续观察镜像为准，最新停止代码验证见上节。

2026-10-02前轮：原40项基础加`s3_prepare`、`s3_gds`、`s3_cli`三组场景，累计43个不同场景通过。首次整体执行42项PASS而CLI失败；诊断确认测试夹具在App_Init前把HAL设成LISTENING，真实启动需READY。一次修改测试初始化顺序后先重跑失败CLI并PASS；随后于2026-10-02 06:44 UTC最终完整执行一次43/43 PASS，退出0、无warning，证据为 `D:/codex_tasks/2026-10-01/task/s3-next/final-full-host/run.json`。MSVC /W3 /WX；Windows ASan/UBSan未运行。

以下也是2026-10-02前轮镜像记录，不能当作本轮源码结果：活动工程及两份镜像生成配置检查通过；同源HSI/HSE WindowsDebug配置/编译0警告，FLASH59724B、RAM9264B；433个输入只IOC和main时钟段不同，未重跑CubeMX，复用字节相同的既有6.18.1生成一致性证据。机械检查ARCH81/HW1与上一轮相同，新增/移除0，SPEC通过；不宣称全绿。完整日志、初次失败和镜像在`D:/codex_tasks/2026-10-01/task/s3-next/`，新A/B manifest见`clock-ab/deliverables/manifest.json`。

2026-10-02前轮用户补充授权USB始终连接、24V未接通后，先核对固定ST-LINK/STM32F446身份、COM10链路及全Flash512KB备份，再烧录HSI ELF `ac5ea60571f18b754114a82ac303912bc5cc09babce3dd64593ba698d0ef6fc4`。CubeProgrammer返回Download verified successfully并复位；7条串口查询/默认拒绝检查通过，两路CAN启动成功、输出禁止，prepare/arm均ERR blocked。原Flash备份、下载原文及逐条响应在usb-only；24V断开依用户报告，本机未独立测量。未烧录HSE、未接通动力、未发送实物pulse/校准/模式命令。这些结果只证明当前配置下默认关闭，不是实际运动或停止通过。

2026-10-02本轮每轴/真实观测/C620合同：`host-04`一次执行受影响35项全部PASS（32项既有场景及新增 `s3_axes`、`s3_observations`、`s3_shoulder_console` 三组）；本次补入裸C620坐标ID匹配检查及反例后仍35/35 PASS、无编译警告，host-03及前版镜像证据保留，当前最终结果以host-04为准。11项未受影响UART场景复用前轮最终一次43/43整体验证的结果，不称本轮一次46项全绿。首次编译发现旧测试仍使用整机字段，定位后一次适配；随后按新合同完成验证，没有试改生产CAN接收链。Windows ASan/UBSan未运行。本轮最终同源HSI/HSE WindowsDebug编译/链接0警告，FLASH64488B、RAM9312B；复用缓存再配置各有1条 `CMAKE_TOOLCHAIN_FILE` 未使用提示。source-manifest.json登记434个项目输入；加上本机CMakeUserPresets.json，冻结比较共435项，A/B仅IOC/main时钟段不同，两份生成检查通过。静态ARCH81/HW1告警未变，新增/移除0，SPEC通过。本轮证据在 `D:/codex_tasks/2026-10-01/task/s3-axis/`；当前交付hash以本轮最终审计为准，旧镜像记录不代表当前源码。本轮没有USB访问或重新烧录，板上仍是上述ac5ea605镜像。

剩余工作按启用轴推进：肩侧须解决Form根因及真实反馈/必要读回，裸C620须确认方向、扇区限值与operational，按受监督台架策略落实当次有人值守和可用备用停机；新停止观察源码已离线实现，实物停止效果仍未验证；装配关节才要求连续参考和机构映射。现场取得必要证据后开展对应轴S3小输出、停止和超时观察，复用已过静态证据。肩侧S3完成可进入S4，S5再汇合两轴；当前不宣布S2/S3硬件完成。


后续有人现场的USB-only更新尝试（2026-10-02 10:55–10:58 UTC）已取得当前24V关闭确认，但固定ST-LINK的SWD芯片身份读取失败，Normal同速率诊断报DEV_USB_COMM_ERR，尚未备份或写入新版。三条只读串口查询确认应用运行、配置/绑定/输出均0；无prepare/arm、校准、切模式或运动指令。证据在s3-axis/usb-only-20261002/session-result.json。停止插头随手可拔已获用户口头确认，真实停止提供者和实际停止效果仍未验证；具体运动前仍须通知用户并明确轴/参数/时长/停止条件。


## 2026-10-03：肩属性反馈 provider 的离线接入

本轮只实现接收与状态适配，不具备自动运动条件。新增 `shoulder_property_feedback.c/.h`，复用 `gds_sdo_read` 的只读封装，对实机 HW3.11.1/FW0.6.5/node1 endpoint0 schema 中的位置372、速度378、motor.is_calibrated195、encoder.is_ready366、input_torque272、control_mode287、input_mode288进行类型检查。新增5个状态/配置属性尚无CAN实读；bool/uint8的高字节暂严格要求0，设备实际padding未验证。

- `Expect`只能由未来唯一发送方在读请求成功提交后调用；构造报文不是已提交。单个在途请求、固定字段顺序、请求/接收时间关联；7项完整发布一个快照。超时、错配、重复或时间回退使provider锁存失效，正常32位时基回绕可用。没有自动请求、重试、模式写入或输出许可。
- S3须显式选择 `S3_SHOULDER_ENCODER_PROPERTIES` 才能使用该快照；旧029仍保留为观察，不再作为S3坐标来源。换算仍经过已确认的方向、零点与传动比例。设备自报校准仅由195与366共同提供；272为零只证明目标读回，287/288均为1只证明模式。zero/mode证据使用请求时间，mode取固定顺序中更早的control_mode请求；写入前发出的请求不能因迟到回复变成写后证据。
- 生产 `s3_site.c`未改，来源、超时、现场确认和使能仍为0；主程序没有 `CanMonitor_ExpectShoulderProperty` 调用。当前 `operator_stop_clear`、`independently_stopped`保持false，IDLE、零速度、零目标或停止帧提交均不替代独立停止来源。
- 下一步**软件**缺口：建立SDO与输出gateway的唯一发送协调、诊断互斥及持续刷新；当前gateway每周期purge/submit可能打断独立SDO请求，不能把新provider直接并行启动。实际停止输入尚无选定物理来源，绑定不能由软件猜定。此前独立审查中的encoder_stream/output_gateway问题不在本轮修复。
- 仍需真实现场事实：肩侧映射（方向/零点/比例）、允许活动区间与固定/承托状态，以及选定的实际停机来源。校准/模式/零目标的读取与软件接入由程序完成，不要求用户反复手动转动测试。

本轮按开源索引7/8/10/11行读取ExoKit `ExoskeletonJointActiveHandle::getPresentPosition/getVelocity/setJointMode` 与Rhino `GIM6010_fbdata/SWSetControllMode/SWEstop`；复用状态/驱动分层，具体只读协议复用项目已验证SDO代码和实机schema，不复制Dynamixel模式缓存、旧扭矩恢复或Rhino硬件倍率。

验证：12个选定主机场景通过。审查发现时间回退反例后，先记录该回归失败，再修正并仅复验关联与S3消费两个受影响场景，均通过；MSVC /WX与最终WindowsDebug编译无警告（FLASH74492B、RAM9840B）。`check_generated.py`与`git diff --check`通过；Git另有既有行尾规范提示。证据在 `D:/codex_tasks/2026-10-01/task/s3-shoulder-provider/`。未刷写、未访问硬件、未验证真实运动/停止；S2变化项仍未验收，未提交或推送GitHub。

## 2026-10-03：S3肩持续属性读取、发送协调与停止输入软件闭合（离线）

本节接续上轮provider记录：统一发送协调、持续刷新与停止输入的软件路径已经实现，并通过下列主机和目标编译验证；生产现场配置仍保持默认禁用。这里的完成仅指离线软件链路，不代表真实肩运动、停止或S2变化验收通过。

- 显式 `prepare` 才取得本次启动唯一的肩读会话，和旧SDO诊断/encoder_stream互斥；上电不自动查询。先收齐7属性完整快照，再进入已有零目标→读回→模式→读回→闭环心跳→人工arm/pulse流程。会话按配置周期连续刷新，回复超时、会话到期、健康异常、停止或gateway FAULT关闭会话，不能自动恢复。
- `board_can_tx`分别跟踪肩只读请求与输出邮箱。正常输出purge保留当前只读邮箱；新读取等前序输出邮箱离开硬件后才提交，每周期先读后输出，以保留目标/模式证明的请求因果时间。停止先关闭读会话并取消所属邮箱，同时仍尝试输出停止；取消无法确认完成时锁故障。兼容HAL完成读取后复用邮箱的竞争，旧诊断全空/全清契约保留，无新增软件发送队列。
- 新 `board_stop_input` 真正调用HAL GPIO读取，仅接受显式单引脚、已启用时钟、已配置输入模式；不自行启时钟、初始化GPIO或修改上下拉。`s3_stop_input`分别提供操作者停止请求的反值和独立停机完成信号，每次现读，两路不得共用引脚。不存在缓存置真、B1默认映射，也不用IDLE、零目标或速度0替代独立停机完成。
- 初始快照等待在读提交前检查期限；每个心跳进入新服务时立即检查，坏心跳不能被同批次后续好心跳掩盖。取消、Arm清帧失败及异步Prepare拒绝都关闭读服务；读取消失败锁存且不会因后次查询恢复而消失。原 `output_gateway.c` 和 `encoder_stream.c` 的独立审查问题未混入本轮，文件哈希未变；本轮审查结论只覆盖上述新增集成。

| 必要验证 | 实际结果及范围 |
|---|---|
| 20个受影响主机场景 | 首轮20/20 PASS，MSVC /WX，无编译warning；覆盖停止GPIO、发送邮箱、完整肩软件链、故障、属性关联、旧SDO/stream与S3兼容。 |
| 末次修改后的复验 | 仅5个受影响场景 `s3_chain_mailboxes/run/faults`、`s3_cli`、`s3_bench_stop_cli`，02:43:25 UTC全部PASS。等待快照期间取消失败未锁FAULT的反例先实际失败，修正后通过，原失败日志保留。 |
| 完整软件链观测 | 模型中35次读取、23次输出、1次IDLE，模拟0.02Nm/20ms请求到期关闭且不重启；这些为主机模拟计数，不是实物CAN发送或电机动作证据。 |
| WindowsDebug | 02:43:21 UTC最终配置/编译退出0，无warning；FLASH78868B、RAM9872B。生成配置检查及 `git diff --check` 通过，Git有既有换行提示。 |
| 独立审查 | 发送协调/会话和真实停止输入分别审查，无本轮范围内剩余软件阻断；33个代码/测试文件与候选内容一致。 |

已复用开发日志2026-10-02 11:28“接线未改、电机已固定”及总电源插头可在不靠近轴/裸线处拔除的用户确认，无需重新确认这些相同事实。该确认没有给出当前肩支撑几何、允许运动区间或MCU可读停止来源。当前不可离线补造的配置是：操作者停止输入及独立停机完成输入的实际引脚、资源归属、电平/偏置/极性与物理含义；肩方向/零点/倍率/限位及当前承托是否足够也尚未确定。新5项属性的真实CAN读回/padding与运动时刷新负载未在设备上验证。两路引脚均NONE、确认false、使能轴/限制/时序保持0，所以当前固件仍拒绝实物prepare。

现场已有HSE静止30秒可信连续窗口通过，未完成一次真实变化；掉线恢复依用户要求取消，未执行。真实S3运动与停止未验收，不能称S2或S3总验收通过。当前板上HSE镜像、A/B和静止证据均保留；本轮构建仍来自仓库HSI源配置，未烧录。本轮没有设备访问、复位、使能/运动、校准、设备参数修改、commit或push，也未运行ASan/UBSan或扩展全套测试。

本轮先复核AGENTS、开源索引及ExoKit关节位置/速度/模式接口、Rhino反馈/模式/停止接口与现有HAL发送/读取实现，只借鉴职责和调用关系，不复制设备参数。可审阅的本轮增量、35文件清单、测试/编译、修复前反例与两份独立审查在 `D:/codex_tasks/2026-10-01/task/s3-shoulder-chain/`：`review.patch`、`changed-files.json`、`completion.json`、`host-result.json`、`host-final2-result.json`、`target-final2-result.json`、`static-checks.json`、`review.json`、`stop-review.json`。

## 2026-10-03：肩部受监督单次动作、停止后观察与只读USB准备（离线）

本节修正前轮外部GPIO方案的适用边界：两路GPIO是可选的外部停止实现，不是S3计划指定的采购或额外接线条件。新增 `SAFETY_STOP_SUPERVISED_SHOULDER_BENCH`，仅允许肩轴，适用于有支撑的现有台架；既可使用明确的转子坐标，也可使用已确认映射的装配关节坐标。它不读取或伪造 `operator_stop_clear/independently_stopped`，人工断电备用与软件停止观察分别记录。原外部GPIO与C620分支保留，生产site仍全零禁用。

### 软件行为与证据边界

1. 显式prepare取得本次启动唯一读会话，经7属性完整快照及已有零目标/模式/闭环准备后才允许arm。首次有效pulse被接受即锁存 `shoulder_attempted`；不以CAN实际送达作为消耗条件。ACTIVE不能通过第二条pulse续期，随后Prepare/Arm/ack均被拒绝；只有冷初始化清除本次启动锁。
2. 正常到期/cancel先撤销输出许可、清旧输出并提交IDLE。保留在途只读SDO及其唯一请求关联，让它完成；不以取消后重建provider的方式接收可能迟到的旧回复。新读取等待原输出邮箱离开硬件，再进入有界只读停止观察，不重启输出。
3. 停止事件的时间取停止提交返回后；观察必须有事件之后的新鲜IDLE心跳。位置和速度的请求、应答四个时间戳均须严格晚于事件、因果有效且新鲜；停止前请求即使晚到也不计。只有不同新配对的连续时间跨度可积累静止窗口，重复轮询不能延长证据。速度、累计位置跨度、配对间隔或健康异常使窗口失效。
4. 静止阈值、持续时间与观察期限集中在site，默认0不可用；没有把测试阈值当成实物值。观察成功关闭读取并记录历史 `stop_observed`，实时fresh会正常过期，历史结果不能恢复许可。观察超时、CAN/UART/接收异常或任何gateway TRANSPORT/SERVICE/HEALTH故障立即关闭读取并隔离；停止投递/清理失败保持锁存。后续停止指令成功不能掩盖首次发送/服务故障。
5. `stopstatus`在肩策略下报告 `source=shoulder_sdo`、实时seen/fresh/stationary/dwell/age和历史observed、reader、停止投递结果。反馈静止不等于断电、零力矩或独立急停完成；发送接受、驱动IDLE和人工拔AC也分别记录。

既有gateway两个入口期限已补齐：READY的Arm同样受原prepare截止约束；首次Replace同样受原ARMED等待截止约束。到达边界即拒绝并尝试停止，正常毫秒回绕可用。原READY准备期限语义不变；服务间隔逾期单独记SERVICE，避免误作正常TTL结束进入观察。此前独立encoder_stream问题未混入，文件保持不变。

### 用户事实与ExoKit默认范围

用户最新明确采用ExoKit默认机械限位、没有增加限位插件，有支撑并允许转动；复用此前电机已固定、接线未改、总电源插头可达的确认，不重复询问。此回复没有更新24V状态，不能记录为动力已关闭，也不授权本离线轮立即访问硬件。

实际只读核对Git归档 `Open-source projects/ExoKit-main/ExoKit Manual V1.pdf` 第7页（印刷第6页）§1.2图b：SHOULDER_SIDE名义后伸20°、前屈115°，跨度135°；图a的0–90°是另一自由度，不能混用。`Arduino API/lib/exokit_api/src/exo/builder/ArmBuilder.cpp:73`默认20/115，`ExoskeletonJointActiveHandle.cpp:113`执行−20至115°；`ExoskeletonCalibrator.cpp:139`却要求最大后伸后tare，零点口径不能直接套到本机。单零件3MF没有装配关系，不用模型名称伪造实装端点。

接口登记已实读减速比8.00，厂家资料也为8:1；属性372/378为原生转子rev/rev/s。若电机输出至关节1:1，名义135°对应3个转子圈，但历史约2.955rev不是限位锚点。当前仍缺一个已知机械姿态对应的编码器基准及实装符号/输出至关节连接比例，因而不能从总范围推出当前两侧余量。静态位置不能自动成为用户零位；无需先做完整解剖标定，也不补造 `mapping.confirmed`。

### 固定20项原生USB只读工具

工具位于 `D:/codex_tasks/2026-10-01/task/s3-shoulder-supervised/usb/read_s3_properties.py`，具体命令见同目录README。直接复用既有Fibre封包、ACK/空输入保护、已安装DLL及真实schema，不刷STM32/GDS、不重导schema、不改原工具。固定9项身份（首读板SN，HW3.11.1/FW0.6.5/node1/IDLE）＋7属性＋3限制＋末IDLE，共20个标量RPC上限；8秒读取总期限、每请求写读合计500ms、20秒进程期限，异常结束、单发无重试，不写属性、不调用函数、不复位或改变配置。默认执行仅离线自测。

7属性为 `motor.is_calibrated`195/bool、`encoder.is_ready`366/bool、`controller.input_torque`272/float、`controller.config.control_mode`287/uint8、`input_mode`288/uint8、`encoder.pos_estimate`372/float及`vel_estimate`378/float；限制为 `motor.config.torque_constant`247、`current_lim`249及`controller.config.vel_limit`301（均float）。保存实际值和原始报文，不把未校准、false或非零目标覆盖为通过。USB值验证不能替代新增属性的CAN填充字节和运行刷新验证。

### 本轮实际验证与下一步

必要检查累计覆盖21个不同host场景；分批执行，不能称一次21/21。原20项先通过，首次Replace期限夹具在age21前已调用Step20正常过期导致自身失败，仅修夹具后通过。两个期限在旧真实gateway上含回绕共4反例先失败；审查发现首次输出/服务故障仍保留观察的两个反例也先实际失败，再修正并仅复验受影响组。最后9项通过、MSVC /WX无warning。模拟肩链56次读取/23次输出/1次IDLE、停止事件29ms后形成测试配置下静止证据，且本次启动禁止重启；这些不是实物运动或停止数据。

固定USB名单的成功、错身份、中途超时及非空payload拒绝四个离线场景通过，USB独立审查通过；未运行capture。固件独立审查结论及最终编译以本轮review.json/hse-result.json为准。已用既有验证HSE的main/IOC时钟文件生成新默认禁用候选，stage到candidate仅这两处时钟差异；WindowsDebug、生成检查、BIN/HEX均通过，生产符号存在且无TestS3，FLASH82780B/RAM9960B，无编译warning。活动仓库时钟仍HSI；板上旧HSE、原A/B与静止30秒证据均保留，未刷入新候选。

下一可执行顺序：主线程明确通知并确认现场前置后，复用上述工具做一次只读参数窗口；建立现有机械姿态到原生编码器的基准/方向对应，依据实际值选择有限输出与停止观察阈值，再填集中site并构建动作候选。动力关闭状态经明确确认后才有刷写窗口，不能把当前默认禁用候选当可直接动作镜像。动作窗口只做一次有限请求，记录变化和停止，新错误立即结束、不自动升档/重试。S2实际变化、S3真实运动与停止仍未验收，掉线恢复依用户要求取消；本轮无设备访问/复位/烧录/驱动/校准/设备参数修改或GitHub提交推送，未扩展ASan/UBSan及全套静态扫描。

证据目录 `D:/codex_tasks/2026-10-01/task/s3-shoulder-supervised/`：`changed-files.json`、`review.patch`、`completion.json`、`host-02-rest.json`、`host-final3.json`、期限与观察故障修复前日志、`review.json`、`usb/review.json`、`usb/validation/`、`hse-result.json`及`deliverables/`。原生USB工具为该目录中的PC交付物，未隐式加入固件或改变启动行为。


## 2026-10-03：缓存遥测、七属性 CAN 资格检查与 PC 单次记录入口（离线）

本轮补齐现场执行前的软件入口，生产 `s3_site.c` 仍为全零禁用；没有刷写、串口/USB访问、复位或电机动作。当前板上仍是此前静止连续窗口使用的 HSE 镜像，不能将本轮候选构建当作板上版本。

与前节USB工具记录衔接：本轮之前、03:24:10 UTC已在另行确认的窗口完成固定20项NativeUSB只读，20/20成功、无重试/参数写入/CAN请求/复位；HW3.11.1/FW0.6.5/node1、首末IDLE、calibrated/ready为true、input_torque=0、模式3/3。位置2.869931936264038rev、速度−0.007629395462572575rev/s、Kt约0.097Nm/A、既有current_lim=45A/vel_limit=30rev/s。后两项是设备既有配置，不作首次动作限值。原始记录为 `D:/codex_tasks/2026-10-01/task/s3-shoulder-supervised/usb/captures/20261003T032410.058186Z/capture.json`；该窗口不代表现在的通电状态或运动许可。厂家rev2.2定义3/3为位置/位置滤波，既有prepare按零目标读回→1/1模式读回→闭环新心跳推进；本轮未实际执行。

- `s3feedback` 只返回最近一次完整七属性快照的位置/速度 float32 位模式、序号、各自请求/接收时刻、当前时刻、新鲜度及输出状态；`s3properties` 返回同一缓存中的零目标、校准、ready及模式证明。序号只在完整快照发布时递增，查询不请求 CAN、不更新时间戳。失效后保留历史数值并标记不新鲜，PC 不将其当作新测量。
- `s3reader` 返回读会话状态、剩余窗口、停止/观察完成时刻和带独立新鲜度的心跳；`s3limits/s3timing/s3stopcfg` 返回实际固件的集中配置，供冻结计划逐项核对。全部响应限定256字节。`s3help` 列出新命令。
- `qualify` 仅在 SAFE_OFF、配置/绑定/输出均关闭、健康且新鲜的 IDLE 心跳、CAN 无错误且发送空闲时可启动。它按固定顺序读取372/378/195/366/272/287/288七属性，每项只请求一次，单项250ms、总窗2000ms；首个异常停止、无重试、无模式/使能/力矩/IDLE发送。成功还要求第七回复之后的新健康 IDLE 心跳。一次启动独占读会话直到下次冷启动，与旧诊断、连续流和运动准备互斥。
- `qualstatus`、`qualfield 0..6` 和 `qualerror` 为缓存查询，保留原始 CAN 字节、解析值、时间及首错现场；`qualcancel` 只取消本检查所属的只读发送。`qualstatus.cancelled` 表示取消所属发送接口已确认（`cancel_accepted`），正常完成同样为1，不表示资格检查被用户取消；实际终态看 `state/reason`。
- [PC记录器](../tests/s3_capture/README.md) 复用已有 encoder_capture 的串口初始化、固定 ST-LINK 身份绑定与日志格式。默认及帮助为离线路径。资格检查与后续 trial 分开启动；trial 必须使用另一次冷启动、已确认并冻结的具体现场计划及真实资格记录，逐项匹配固件配置，然后依次执行 prepare→READY零目标/模式读回→arm token→单次请求→停止后新反馈记录。正常 TTL 到期保留现有 FAULT/EXPIRED 锁存，不自动 ackfault、重试、延长 TTL 或增加输出。

时序预算显式包含七读周期 C、刷新周期 P、调度余量和各阶段期限；只通过一轮七属性读取，不能证明运动负载下持续刷新合格。PC 对重复序号不重复计数，对新鲜数据和停止完成的历史证据分别记录。观测到数值变化不自动等于物理方向/幅值对应或 S2 总验收通过；USB读值也不能替代 CAN 实读/padding 验证。

参考沿开源索引8/27/28/30行，实际读取既有 `gds_sdo_read`、肩读所有权和发送接口、CLIP `clip.h`/现有命令回调、ExoKit `CmdReactionRunner::launchReaction/cancelReaction`，以及项目 `tests/encoder_capture/capture.py`。复用协议/生命周期和有界日志，按当前裸机、固定七属性与单次动作入口作适配，未引入新控制算法。

本轮软件证据统一保存于 `D:/codex_tasks/2026-10-01/task/s3-qualification-telemetry/`，最终测试、审查、候选哈希与应用文件列表见 `completion.json`。C侧12个必要场景全部通过（`host-02.json`），包括缓存查询不发CAN、七读首错停止/互斥以及真实C命令入口的模拟单次链；MSVC /WX无warning。PC离线合同测试及独立审查见该目录最终记录。默认禁用的HSE WindowsDebug候选FLASH89576B、RAM10560B；仅复用此前已验证的HSE时钟文件，活动工程HSI配置未改。未运行ASan/UBSan或扩展全套测试。

尚未验证：五个新增属性的真实 CAN 编码/padding、完整七读在持续负载下的时序、真实运动与停止；S2一次真实位置变化仍未验收，掉线恢复依用户要求取消。0.020转子Nm/100ms/0.20Nm/s仅属前轮讨论候选，未写入现场配置或作为已批准实物参数。后续仍需把实物姿态/限位与编码器零位、方向及实际传动关系对应，再冻结现场参数。接线未改、机构已固定及备用拔电可达的已有确认继续沿用；当前24V状态不由历史确认推断。未commit或push GitHub。


## 2026-10-03 06:10–06:16 UTC：资格候选已刷入，USB-only 缓存核查通过

本节更新本文件此前“候选未刷入”的历史状态：用户当前确认24V关闭后，已单次刷入默认禁用HSE资格候选 b37d0394e38de117b7cda060b099317ada97d0829176ee8271fcb25d58dc0359；官方verify/reset、89576B独立读回及HSE/PLL寄存器均通过。首轮Windows WMI端口枚举超时发生于打开串口前；后续获准诊断后，用活动设备映射与cfgmgr32健康/完整固定SN双次现读恢复，独占串口打开及四条缓存查询通过，输出/配置/绑定/请求/token均0，资格state/attempted/completed/claimed均0。当前资格尚未启动。

记录工具现用 `native_vcp_binding.py`，仅显式--probe时读取身份，仍有5秒subprocess限时，默认导入不枚举，不使用可被Python优化删除的assert。SDK接口依据为本机Windows10 SDK 10.0.26100.0的cfgmgr32.h/cfg.h；保留原固定ST-LINK身份合同与唯一端口要求。一个既有guards离线场景和独立审查通过；未重复固件/整套测试。本恢复阶段未刷写/复位或发CAN/运动，保持24V关闭。证据与最新板上记录在 `D:/codex_tasks/2026-10-01/task/s3-qualification-telemetry/`。两侧活动余量已有用户确认，不再索取照片，也未编造成精确机械映射。下一步仅为另行通知后的真实七属性只读资格窗口，S2真实变化与S3运动/停止未验收。


## 2026-10-03 15:24 UTC：真实七属性CAN资格单次通过

更新前节“资格未启动”：用户当前确认24V已开，固定身份/默认禁用/新鲜健康IDLE/未用资格及连续uptime前检通过后，唯一一次qualify成功。7/7、mask127、COMPLETE2/reason0；位置2.8727259635925293转子rev，速度0.0019073487492278218转子rev/s，calibrated/ready=true，input_torque=0转子Nm，模式3/3（位置/位置滤波）。原始ID025/DLC8及padding一致；没有参数、模式、使能或输出写入。

首请求至末回复6ms，全资格54ms，最后新健康IDLE晚于末回复47ms。每项请求/回复同1ms tick，不宣称物理零延迟；CAN/UART错误、丢包、溢出、发送超时/调度超限增量均0。仅22条UART命令中一次qualify，串口已关闭；本轮无复位、刷写、运动或测试重跑。独立只读原始证据审查通过。

当前boot所有权已消耗：claimed1/pending0/out0，cancelled1表示取消所属只读发送接口成功，实际终态为COMPLETE。不得在此boot prepare/trial；真实变化仍需单独启动和具体冻结现场参数，本窗口不授权自动重启或运动。一次资格不替代持续负载或S2真实变化验收；掉线恢复取消、GitHub未提交/推送。完整证据在D:/codex_tasks/2026-10-01/task/s3-qualification-telemetry/qualification-20261003T152430610504Z/，三处current-board.json已同步最新状态；此前历史记录保持原样。


### 2026-10-03：一次局部相对转子微动方案审查，尚未生成可动作镜像

用户已确认24V关闭、USB保留（Sentinel_d6ec3c7159f4819197f371ec510142ae），既有固定、承托、两侧余量、断电插头可达及动作授权继续沿用。离线源码审查确认现有固定mapping不能仅改常量取得当次相对原点；不得把此前资格位置写作当前零点。最小修改为显式相对原生编码器模式，在prepare之后的新鲜完整七属性、健康IDLE、校准/ready且目标0时只锁存一次原点，保持整次启动不变，并记录原点/序号/时刻。静态策略就绪与运行期原点有效分开，PC冻结计划同样需适配；无需取得绝对ExoKit零点或解剖方向。

可审阅方案为正原生转矩0.020Nm、TTL100ms、斜率0.20Nm/s；相对位置±0.01转子rev、速度0.1转子rev/s作为软件撤权阈值。支撑及定性余量不能被写成已确认数值物理限位或停止距离，具体软件包络尚待主线程落实批准，不假置limits_confirmed。service5ms下首包不超过0.001Nm，提交峰值不超过0.020Nm；1ms理想调度末包约0.0198Nm仅是示例，因为首次斜率积分可能包含pulse前时间。100ms到期先撤权，健康服务间隔不超过5ms时于age100..104ms提交IDLE；现停止只写requested_state=1，不宣称input_torque寄存器被清零。Kt约0.097对应峰值目标电流约0.206A，不是实测电流；既有45A/30rev/s不作试验限值。

候选时序为period20/reply20/feedback150/service5/prepare1500/maxrequest100/reader2500ms；停止观察span0.001rev、speed0.02rev/s、dwell250/fresh40/window1000ms。结合本次七读实测6ms，软件fresh预算31ms、reader预算1711ms、stop预算431ms；这些是预算，不代替持续负载或实物停止验证。完整review-only/disabled方案在D:/codex_tasks/2026-10-01/task/s3-relative-trial-candidate/plan-proposal.json。为避免无边界改动，本轮未改固件或生成可动作镜像、未重复测试、未访问设备、刷写或运动；下一步由主线程明确此具体软件包络后，才做最小相对模式实现、相关门控验证与构建。S2实际变化仍未验收，GitHub未提交/推送。


### 2026-10-03 UTC：当次相对肩微动软件及单次HSE候选完成，未刷写

- 用户Sentinel_cf5158fc3be481919ba4701d4a6b19e2已确认已告知的一次100ms/0.020转子Nm方案，并确认有支撑、当前位置两侧各约至少2°无碰撞空间；这是现场估计，不作测量或停止距离保证。24V最新确认关闭、USB保留。本轮仅离线实现、验证和构建，没有访问设备或动作。
- 新增显式S3_BENCH_RELATIVE_ROTOR及独立relative_native_envelope/software_envelope_approved。旧绝对坐标仍须原limits_confirmed/reference；新模式要求二者false，仅肩、原生方向+1/ratio1/静态zero0，不把数值约定当解剖映射。显式prepare成功启动读取后，首个完整且新鲜的七属性周期、健康IDLE、calibrated/ready及target0才能锁存原点；全部请求/回复须严格在本次prepare之后。位置减去冻结原点，速度不归零；取消、停止或故障退休且保留证据，本次启动不能重新归零或再试。
- 新被动s3anchor查询报告scope、phase、origin float bits、seq/q/r/begin/now及输出状态；不发CAN、不锁存、不刷新。PC在prepare前、READY/Arm前及停止后核对UNSET→ACTIVE→RETIRED和同一原点身份；历史原点与当前反馈新鲜度分开，不在Arm和单次pulse之间插入查询。默认禁用模板保留，单次试验计划另以用户具体授权及实际资格证据冻结。
- 必要验证：4个C场景全部PASS（固定原点、旧/同刻/陈旧/不健康快照拒绝、查询被动、配置边界），6个PC fake-serial场景PASS（相对成功/错误/重置/变更及既有trial/guards）；均无硬件。首次C夹具错误认为预先灌入SDO后仍可Prepare，实际旧所有权规则正确拒绝；只修夹具并冷初始化分场景，未放宽生产规则。首次失败的已解码工具输出另存并注明非原始字节，最终结果独立保存。未跑全套或重复资格检查。
- 隔离HSE候选WindowsDebug、生成一致性检查、BIN/HEX转换均退出0且无warning，FLASH91148B/RAM10600B，独立源码/配置/PC审查PASS。主仓site全零和HSI时钟保持原样，候选仅另外使用此前验证的HSE main/IOC及单次现场配置。候选已配置肩通道，但启动仍SAFE_OFF、anchor UNSET、reader IDLE，无自动prepare/arm/输出；固件配置不是当前板上状态。
- 实际候选：转矩上限0.020Nm/斜率0.20Nm/s/请求100ms，相对±0.01转子rev（8:1输出轴换算约±0.45°）及速度0.1转子rev/s撤权。period20/reply20/feedback150/service5/prepare1500/reader2500ms；停止span0.001rev、speed0.02rev/s、dwell250/fresh40/window1000ms。6ms实读周期对应31/1711/431ms预算，不是持续最坏耗时证明。首包最多0.001Nm，峰值最多0.020Nm；健康≤5ms服务下到期于age100..104ms提交IDLE。提交、驱动IDLE和机械停稳分别观察；不宣称零转矩寄存器、独立断电或硬停止距离。空间/速度换算不代替实物停止验证，不自动增大力度、延时或重试。
- 本轮证据目录D:/codex_tasks/2026-10-01/task/s3-relative-trial-candidate/：host-relative.json、w/pc-relative-result.json、review.json、hse-result.json、trial-plan.json及trial-plan-validation.json。ELF SHA256 762c0902a4269f3056c32e75b2cb99039d9a45f22678aac41364c111500bcf54；冻结plan SHA256 f39520e106855c7d1d871f1b5370f923456d688f04eda379f7ee6492721fa89d。修改文件和增量见changed-files.json/review.patch/completion.json。板上仍为前轮资格镜像，本次启动资格所有权已消耗；后续由主线程安排USB-only刷写及运动前通知，本轮没有刷写、复位或驱动。S2真实变化、S3实物停止尚未验收，掉线恢复取消，GitHub未提交/推送。


### 2026-10-03：相对候选已刷入，动作前UART缺字中止；离线根因未定

- 补记已执行现场窗口：15:50:36–39 UTC仅USB供电下刷入相对候选一次，官方verify/reset退出0；独立读回91148B SHA256 7f844000b67d9880106a54b2f3fcd3f4487e10666ad2756036fbbdfc0bff6740与候选一致。9项缓存检查通过，SAFE_OFF/output0、reader IDLE/requests0、anchor UNSET；未重复RCC测量。此前“候选未刷入”是历史事实，当前板上ELF为762c0902a4269f3056c32e75b2cb99039d9a45f22678aac41364c111500bcf54。
- 用户确认24V开启后的唯一trial于15:54:12 UTC在第6条shoulderfeedback前检中止：原始字节以4B（K）开头而非4F4B（OK），以完整CRLF结束。工具正确拒绝；只发送ping/status/canstatus/shoulder/outputstatus/shoulderfeedback及收尾cancel，未发送prepare、arm、pulse或使能。cancel实回OK cancelled output_allowed=0，串口关闭，无自动重试。已要求关闭24V，但尚未收到关闭确认，不沿用刷写时断电状态。
- 本轮离线核查：raw_hex在解析前已缺O；Python rstrip只处理行尾，实际pyserial read_until逐字节保留非空数据，无中途显式drain/purge或第二reader。固件固定OK前缀、同步队列复制和独立DMA缓冲未发现能证明丢首字节的路径；HAL/环形缓冲mock不能代替线路证据。query在发命令后重复设置timeout会调用Win32 SetCommState，可与回复抵达重叠，但没有证据证明它造成此次丢字。根因仍在尚未区分的MCU发送/串口链路/ST-LINK/Windows驱动范围，未据猜测改通信代码或放宽完整性检查。
- 两项必要离线验证PASS：原始7回复通过实际Link/session及pyserial read_until回放，仍拒绝第6条且prepare/arm/pulse均0、只cancel1次，raw日志完全一致；完整合成回复在整块、首O后混合分片及逐字节输入中均保留首字节。合成补O只用于测试，未修改原始现场记录或接受残缺报文；这些结果是软件路径排除证据，不是实机通信恢复或缺字根因已修复。
- 原始现场证据保留在D:/codex_tasks/2026-10-01/task/s3-relative-trial-candidate/usb-only-20261003T155032493272Z/与trial-20261003T155411933025Z/；本轮排查、源文件hash及最小离线回归见D:/codex_tasks/2026-10-01/task/s3-uart-reply-integrity/。四处current-board已同步失败及最新电源未知状态。本轮未接设备、复位、烧录或动作，未扩大编译/全套测试；S2真实变化及S3运动/停止仍未验收，掉线恢复取消，未commit/push。下一步只针对接收完整性补充必要的底层证据，不直接重跑动作或提高参数。


### 2026-10-03 16:07 UTC：一次只读UART底层诊断，缺字未复现、根因未解决

父线程授权的一次有界只读窗口已执行，固定ping/status/canstatus/shoulder/outputstatus/shoulderfeedback/status共7条、串口只开1次并关闭；无CAN请求、cancel、prepare、arm、pulse、复位或烧录。7条回复完整，890次ReadFile完成字节与原始日志逐字节一致，890次ClearCommError flags均0，84次异步pending均正常完成。每条命令写前、写后各有一次相同350ms/DCB串口重配置；只有open发生PurgeComm，未发现跨命令清缓冲或第二reader。缺O没有复现，不能标记已修复或恢复运动；追踪增加了Python计时开销。源码与Win32单次记录仍不足以区分偶发的MCU/ST-LINK/USB/Windows接收损失。

原始全程trace_complete=false如实保留：唯一缺失记录是全部查询完成后的close/call2709，SetCommTimeouts收到结构体实例而非指针，辅助记录器类型处理错误，native调用及返回不受影响。已在D工作区对这一明确原因先做失败反例，再最小修复并离线通过；没有为记录器修复重跑现场。该修复不是UART缺字修复，生产代码/固件未变。

当前缓存输出仍禁止；肩缓存age_ms=698928、CAN err=1/last_err=00000008、wire_valid=0、error=00000040/fault=9，属于读到的陈旧缓存，不判为本窗口新增错误，也不据此推断24V开关。串口完整不表示肩就绪。S2变化及S3运动停止仍未验收，GitHub未提交/推送。用户直接缩减指令为“减少不必要的步骤”，没有停止指令。

证据位于D:/codex_tasks/2026-10-01/task/s3-uart-reply-integrity/：cached-probe-20261003T160712110326Z、trace-analysis.json、close-structure-before.json和close-structure-after.json。下一层最小诊断已准备但未执行：使用本机已有UCX的Rundown+HeadersBusTrace，仅记录USB传输头/长度/时序，不记录USB数据包；配合同样7条缓存读及修正记录器，15秒进程上限，finally停止ETW，无驱动安装或复位。计划与可执行入口见next-diagnostic-plan.json及next-usb-headers/run_header_probe.py；默认离线检查通过。现有一次现场授权已用，新的USB事件范围交父线程决定；不自动重试动作。


### 2026-10-03 16:17 UTC：USB采集范围不符未执行，两项确定的PC通信合同已离线修正

父线程要求仅指定ST-LINK设备、仅传输头/时序、总15秒；准备的logman UCX0x8040实际按provider采集所有设备的传输头，只有事后SN过滤，不满足采集隔离。另有child15秒加start/stop各5秒，不能宣称整个ETW窗口15秒。该方案未执行，live入口现已在任何ETW/串口操作之前明确拒绝，默认方案也标记blocked；只验证了这个立即拒绝路径。没有新增设备探测、串口打开、复位、烧录或动作；不扩大到其他设备采集，不安装驱动或修改系统持久设置。

既有缺O原始证据及16:07完整7查询不能确定原始丢字原因，本轮不再增加硬件诊断。依据实际pyserial源码/已观测的每查询重复相同重配置，以及独立离线反例，最小修正tests/s3_capture/capture.py两处：只有timeout/write_timeout值改变才调用setter；仍保持原写前/写后remaining重算和min(.35,remaining)，未前移读取预算、扩大deadline或重试。原始回复先记录，原parse抛CaptureError后将usable置false，阻止后续普通查询；原单次有界cancel/qualcancel收尾例外保持。没有补O、接受残缺报文、自动重连或修改固件。

新增test_link_integrity.py的三项fake-serial离线回归：相同超时不重配置；临近deadline仍按写前/写后剩余时间裁剪且过期不发送；真实缺O字串原样记录、拒绝后禁止普通查询并只允许一次cancel。修复前前述两项反例实际失败、期限项通过；修复后三项均PASS，原失败和最终结果分别保留。独立补丁审查通过，无放宽解析/帧长/时限。README补充运行入口和事实边界。仅修改PC代码、测试及文档，未重跑固件构建或无关整套测试。

当前capture.py SHA256=ccc5369a4593ef35fa2bdaec10ef2af2f51fbfdf5b8328f86e67287ac7de8bce；改动及证据在D:/codex_tasks/2026-10-01/task/s3-uart-reply-integrity/link-integrity.patch、link-integrity-before.log、link-integrity-after.json。它们证明两项PC合同问题已修正，不证明15:54原始缺O原因或实机通信已修复；本轮没有修正后的实机验证。16:07肩陈旧/错误缓存及禁止输出事实仍保留，不能作为电机就绪。S2真实变化/S3实物运动停止仍未验收，掉线恢复取消，GitHub未提交/推送。后续不反复探测至成功；若继续真实验收，需先重新界定可用的本设备通信证据及当前反馈健康状态。
