# 研究发现

| 关键词 | 来源 | 摘要 | 可信度 | 状态 |
|---|---|---|---|---|
| 当前状态 | schedule阶段表与2026-10-04日志；`tests/s3_capture/evidence-relative-20261004T013713353068Z/acceptance.json` | S2当前批准肩反馈范围通过；S3一次prepare/enable/pulse/stop通过，S3全部false；掉线恢复取消 | 本项目原始记录及明确验收范围 | 已核查，不扩展结论 |
| 只读坐标缺口 | schedule 2026-10-02/03机械观察；`App/control/joint_state.c`、`App/application/s3_site.h` | 手动无碰撞是定性证据；实装pose/方向/比例及有限数值区间仍缺。换算使用连续转子、dir、zero、总比例 | 项目记录+实际源码 | 现场待测 |
| 单次镜像 | `docs/bench/relative-hse-20261004/overlay/App/application/s3_site.c`；`output_gateway.c`、`s3_shoulder_service.c` | 原生相对原点、一次请求、2500ms读会话；不是装配坐标/持续控制。主仓HSI禁用配置与已刷HSE区分 | 归档源码+实际实现 | 已核查 |
| 参考对应 | Git HEAD索引第8/10/11/25/28/29行；蓝图5.12的实际文件导航 | 本轮只读坐标、活动区间和停止路径，不编写新控制器；源码读取见下 | 项目索引及原始源码 | 已核查 |
| 厂家零点 | 仓库GIM6010-8 rev2.2 Word正文“编码器零点/用户零点”段；接口登记 | 用户零点需要相对编码器定义；机械限位未测不能由零读数或默认ExoKit范围推定 | 厂家文本，实机情况另证 | 已核查正文；本轮未重新检查接头图示 |
| watchdog | 已归档HW3.11.1/FW0.6.5 endpoint0 schema | ep153 watchdog_timeout(float)、ep154 enable_watchdog(bool)、ep500 watchdog_feed(function)存在；原20项USB采集未读前两项，不知道现值/实际断线作用 | 实机schema支持接口，但无本轮设备读回 | 待只读核查，不写参 |
| 既有PC入口 | `D:/codex_tasks/2026-10-01/task/s3-shoulder-supervised/usb/read_s3_properties.py`；`tests/s3_capture/capture.py` | NativeUSB有固定白名单20项且首末IDLE；UART单次trial已有配置/哈希/时序核对。不是任意读写或持续控制工具 | 实际代码 | 已核查，无运行 |

## 实际读取的reference

索引通过`git show HEAD:Open-source projects/开源项目借鉴索引表.md`读取；该文件是稀疏跳过，不宣称不存在。本地子模块源码未展开，按索引继续联网读取其已登记commit，未复制/修改参考工程。

- ExoKit本地Git对象：`Arduino API/lib/exokit_api/src/exo/ExoskeletonJointActiveHandle.cpp`的`setGoalPosition()`、`getPresentPosition()`；`ExoskeletonHandle.cpp`的`checkJoint()`、`shutdownJoint()`。借鉴零偏/符号与越界关闭语义；不继承Dynamixel参数、TaskManager、exit(1)或全部关节。
- BetaRobot固定commit c83dd84：[`drv/drv_daemon/drv_daemon.c`](https://raw.githubusercontent.com/itgz123/BetaRobot/c83dd84d6c7717105c948d8d12f2fbb5f4d6b48c/drv/drv_daemon/drv_daemon.c)的`DaemonReload()`、`DaemonIsOnline()`、`DaemonTask()`。只借鉴反馈更新/在线监测与回调职责，不移植FreeRTOS/PWM，也不当作硬件自动停机。
- Rhino固定commit a4b24b8：[`gim6010_drv.c`](https://raw.githubusercontent.com/Coderyne/Rhino-Pi-X1-ROS2HomeRobots/a4b24b80c00aa1cbf58d2a8a20167d8710d97bec/Code/STM32/ros2-wheel-foot-car/User/Devices/SW_Motor/gim6010_drv.c)的`SWSetAxisState()`、`SWEstop()`。实际仍通过CAN发送；只参考协议/停止入口组织，不使用其FDCAN、模式默认和ffTorque乘8。
- GALAXEA固定commit 9006107：[`a1z/robots/arm_robot.py`](https://raw.githubusercontent.com/userguide-galaxea/GALAXEA-A1Z/9006107aa8d4334c762b8bf8e8fe40b3f9477717/a1z/robots/arm_robot.py)的`_read_state()`、`_check_runtime_safety()`、`_check_runtime_joint_limits()`。先查反馈健康再查运动限制的顺序可参考；它的角度越界段只告警，不能继承为本项目保护保证。未以这次局部阅读声称S4模型已经研究完成。

## 本轮操作边界

本轮只有文件/Git对象和公开reference只读核查、计划文档与任务元数据整理。未运行NativeUSB/VCP或读取硬件，未改固件/采集代码、未构建/烧录/复位/运动。当前24V/MCU读会话状态未由旧记录推断。旧HSI CAN异常和UART缺字不标为已解决；沿用HSE及严格解析/首错停止，真实重现再针对性查因。通用`control-safety-limits.md`仅作限位坐标/余量的思路参考，不把其经验阈值、回零或看门狗建议变成额外项目门槛。

## 2026-10-05：用户改为电动前屈约10°后测角

- 用户不能按原方式手动摆位，明确要求电动前屈约+10°后停住测量，并报告前方可运动、无碰撞。这授权了目标动作；未完成的机械方向/总传动关系和控制能力不能由授权补成事实。用户报告0位后伸不可用，旧两侧均有余量的条件不直接沿用。
- 当前归档台架site：原生相对转子坐标，位置包络±0.01rev、最大请求100ms、转矩上限0.020Nm、每启动一次。`OutputGateway_Replace/Step`传递的是出力，不是角目标；撤权发送IDLE，不主动保持姿态。主仓也没有位置编码器或受限定位动作。这是无法直接执行10°定位的已查明功能缺口，没有发送新设备命令。
- 实际厂家Word rev2.2第3.1.7/4.1.2/4.1.3.3节：滤波位置模式为control_mode=3/input_mode=3；Set_Input_Pos命令0x00C，Input_Pos为float32 rev，两个前馈为int16；USB输入位置按转子侧。不能把10°当作10rev。实际schema有input_pos ep270/float/rw，但字段存在不证明新CAN位置动作已经实测。
- 第8/11行reference：本轮再次读取Rhino固定a4b24b8的`SWSetControllMode/SWSetAxisState/SWMitControl`；它没有适用的Simple位置函数，MIT的乘8及FDCAN不移植。第7/11/25行ExoKit实际读取本地已保存`ExoskeletonJointActiveHandle.cpp`的`setGoalPosition/getPresentPosition/isGoalPositionReached/tare`，可借鉴目标/零偏/符号/到位语义，Dynamixel发送需替换。官方产品入口：https://steadywin.cn/h-pd-115.html；协议细节以仓库原手册为据，不使用第三方论坛代码补成当前设备事实。
- 第一组真实快照为用户报告0位2.816345453262329rev、目标转矩0、首末IDLE、模式3/3；驱动限值45A/30rev/s是读回的当前配置，不可当作本次定位限制。固定20项未读位置目标/位置增益/滤波带宽/循环位置/watchdog；schema端点已离线核对，实际值未知。
- 若电机输出与肩轴直连，总比例8:1且前屈符号s已证实，10°仅对应s*8*10/360=s*0.222222rev增量。这是条件换算，不是已批准可发送目标；历史单点不能确认方向和连接比例，开启前需新鲜基准。
- 原手动三点路线暂不执行。新的有限定位/保持链必须先出PLAN清单；默认禁用、唯一输出入口和当前已归档证据保留，不旁路USB直写，也不放宽旧100ms限制来伪装成角度控制。

## 2026-10-05 13:46 UTC：单次撤力请求的前置核查中止

- 用户改为一次运动后撤力、不保持，并报告起点已抬离后侧挡点、两侧有约2°估计余量和承托。沿用旧20mNm/100ms请求及原软件包络，不修改代码、增大参数或假称前屈符号已确认。
- 现有capture.py哈希ccc5369a...未变；新冻结计划精确复用三组限制/时序/停止字段，旧七属性资格只作兼容与6ms周期依据。离线计划校验通过，现场尚未完成三组运行配置核对即在shoulder查询中止。
- 原始5条命令/回复均完整，err80/last_err0x10、rx235496、bus_off/drop/invalid/fov0；UART/调度计数0。prepare/arm/pulse均0，仅一次cancel确认输出0，串口关闭。0x10由本机HAL头文件297行定义为Form Error；board_can.c的record_error累加错误回调且保留最近码，不能把80回调全算作同种错误或绑定最近0x029全零帧。
- 历史证据：10月1日同类0x10增量没有查明物理原因；10月3日HSE30秒静止无新增错误仅支持时钟方向，不是根因修复；10月4日关24V保留USB、一次MCU复位恢复SAFE_OFF/IDLE/UNSET后，重新供电完成旧一次微动。当前保留现程序，优先复用该单次恢复步骤，不盲改时钟/接线或放宽err检查。
- 现有Cancel即使没有prepare，也调用S3ShoulderService_Stop，使IDLE读会话进入STOPPED；Start只接受IDLE。该结论来自实际源码，本次没有额外读取终态reader。恢复需要新的启动，不能直接重发prepare。
- workflow机械检查实际退出1：既有main调用数量、build第三方/编译器文件及同优先级IRQ规则报告，spec退出0；未消警或声称门禁通过。这些静态报告不定位此次物理CAN错误。当前没有固件改动，不重复构建/主机功能测试。

## 2026-10-05：联网核对reference的标定与控制方法

用户要求查项目reference及GitHub的方法。按索引第8、11、15、18、23、25行核查；ExoKit读取主仓HEAD中的快照并对照GitHub源码，其余参考按主仓gitlink固定版本在线读取（当前子模块未展开，不称本地缺少资料）。以下是源码调查，不是本机功能或硬件验收。

| 来源与实际入口 | 查到的方法 | 本项目适配边界 |
|---|---|---|
| [ExoKit / ExoskeletonCalibrator.cpp](https://github.com/HCI-Lab-Saarland/ExoKit/blob/main/Arduino%20API/lib/exokit_api/src/exo/builder/ExoskeletonCalibrator.cpp)：构造函数、calibrate；[ExoskeletonJointActiveHandle.cpp](https://github.com/HCI-Lab-Saarland/ExoKit/blob/main/Arduino%20API/lib/exokit_api/src/exo/ExoskeletonJointActiveHandle.cpp)：tare、getPresentPosition、setGoalPosition | 先关闭主动关节转矩，提示人工摆到指定姿态并确认；保存编码器零偏，按符号换算关节角，发送目标前截到配置范围。肩侧原提示为最大后伸；基类tare默认currentAngle=0，不是本项目自然垂臂零位定义。范围由配置给定，相关函数未自动寻找两端最大角。 | 借鉴人工基准/零偏/方向/目标限幅逻辑；采用用户的自然垂臂0°，不照搬最大后伸基准、方向规则、范围或Dynamixel协议。JointLockReaction::startup将当前位置设为目标以通电保持，不能描述为动作后撤力。 |
| [Rhino / gim6010_drv.c](https://github.com/Coderyne/Rhino-Pi-X1-ROS2HomeRobots/blob/a4b24b80c00aa1cbf58d2a8a20167d8710d97bec/Code/STM32/ros2-wheel-foot-car/User/Devices/SW_Motor/gim6010_drv.c)：SWSetControllMode、SWSetAxisState、SWMitControl、GIM6010_fbdata | MCU经CAN设置模式/轴状态，发送MIT的目标位置、速度、kp、kd、前馈力矩；接收MIT/编码器反馈。属于通过增益设定位置响应和阻尼的控制方式，曲线是反馈观察入口。该文件没有自动寻找肩关节机械范围的实现。 | 参考报文适配职责；不直接采用FDCAN、轮足机构方向、ffTorque乘8及范围。存在MIT封装不等于本机已经验证该模式，也不要求本轮由现有转矩链改为MIT。 |
| [OpenExo / Joint.cpp](https://github.com/naubiomech/OpenExo/blob/523852aaa184e9b3cf6b67ffcb4316a07c09ba0a/ExoCode/src/Joint.cpp)：read_data、check_calibration；[Motor.cpp](https://github.com/naubiomech/OpenExo/blob/523852aaa184e9b3cf6b67ffcb4316a07c09ba0a/ExoCode/src/Motor.cpp)：_CANMotor::read_data、zero | do_zero触发驱动专用CAN置零；反馈按flip_direction调整符号，再在关节层除以gearing换算。zero使用其AK电机专用格式（末字节0xFE），相关函数没有识别解剖学零位或最大活动范围的逻辑。 | 参考零位/符号/传动比三个概念分开；不向GDS68发送AK置零报文，不把电机电角度校准、设备零位和肩轴自然零位混为一项。 |
| [ODrive / endstops.rst](https://github.com/odriverobotics/ODrive/blob/master/docs/endstops.rst)、[control-modes.rst](https://github.com/odriverobotics/ODrive/blob/master/docs/control-modes.rst)（GitHub外部补查） | homing自动回零依赖已可闭环控制的轴和可读取的min_endstop，缓慢接近开关后用受限轨迹回到home；位置运动可用滤波或梯形轨迹限制速度/加减速。回零并不自动测得另一端最大角。 | 本机没有该回零开关的接入证据，不采用撞机械挡点代替开关，不将ODrive接口或默认参数当GDS68事实。 |
| [GALAXEA / arm_robot.py](https://github.com/userguide-galaxea/GALAXEA-A1Z/blob/9006107aa8d4334c762b8bf8e8fe40b3f9477717/a1z/robots/arm_robot.py)：_update、_check_runtime_safety；[gravity_model.py](https://github.com/userguide-galaxea/GALAXEA-A1Z/blob/9006107aa8d4334c762b8bf8e8fe40b3f9477717/a1z/dynamics/gravity_model.py)：compute_gravity_torque、compute_inverse_dynamics | 循环读取反馈并先检查健康，再计算动力学力矩、叠加额外前馈、符号转换/限幅，经CAN发送位置/速度/增益/力矩字段；速度与加速度为零时退化为静态重力补偿。模型提供关节上下界，实测越界检查在该版本仅告警。 | S4可借鉴反馈→健康→重力项→合成限幅→发送顺序；重建本项目模型与负载，不能照搬六轴参数、Python循环或仅告警的范围策略。 |

本轮决策依据：目前适合采用ExoKit式人工摆位与软件记录零偏，由MotorWizard取得同一会话的自然垂臂、手动前屈端点实际POS。两点可提供原始反馈边界和前屈时编码器增减方向；未确认单位/总传动关系及实际肩角时，不能据此宣称135°、+115°或可精确驱动+10°，也不能替代出力极性的有限动作确认。无需先驱动到机械最大点来取得这个端点；带动力工作边界另留停止余量。厂家Word rev2.2第3.1.7节确认MotorWizard表盘单击/右键精确输入/拖动为控制入口；曲线的实际POS与目标POSref须区分。

本轮只有读取和研究记录；没有连接串口/NativeUSB、使能、运动、复位、参数写入、固件修改、编译或烧录。既有CAN错误仍未定位；GUI采集路线及保留现程序的用户决定继续适用。

## 2026-10-05：CAN错误专项调查（RESEARCH，尚未定位物理根因）

用户暂缓MIT讨论，要求结合历史日志、当前实现、项目reference、其他GitHub项目及知乎找最可能原因。本轮只研究与记录；以下排序是排查优先级，不是已经证明的故障概率。更换Simple/MIT不会直接消除CAN物理层/位时序故障。

### 已核实的时间线与结论边界

时间统一为UTC；原events也包含北京时间偏移。当前固件来源以HSE归档镜像和10月5日寄存器实读为准，不能用主仓main.c的默认HSI配置代替板上状态。

| 证据 | 实际结果 | 能说明什么 |
|---|---|---|
| 10月1日肩侧记录；接口登记第10节 | 曾报告H/L约120Ω；参考地曾不通，补专用共地线后用户报告导通 | 物理链路有历史疑点；不能声称目前仍未共地。肘侧60Ω记录不能代替肩侧 |
| 10月3日01:12，USB终端属性原始capture及接口登记第4.2.3节 | enable_r120=true、r120_gpio_num=5；HW3.11.1/FW0.6.5 | 已读到软件开关开启，但未证明实际终端支路导通。软件控制终端的断电表现尚未证实，不能仅凭断电120Ω就补第三只电阻 |
| can_clock_comparison.md中的早期HSI窗口 | 4952ms发生BR后中止 | 是旧窗口的隐性位错误记录；不把BR、STF、FOR混为同一码 |
| 10月3日HSE静止窗口summary.json | preflight uptime1273056ms；肩rx56401→60916、err0→0，30秒读会话成功 | HSE配置确实有成功窗口；不证明根因修复或长期稳定 |
| 10月4日01:37单次pulse归档 | 一次有限出力与停止通过；reader健康检查期间未失败 | 保留适用的既有证据；该工具未作结束时原始CAN计数查询，不补称所有时刻计数为零 |
| 10月5日13:46前置核查events | 肩rx235496、err80、last_err0x10；prepare/arm/pulse均0 | 发动作前已有错误；计数从本次启动累计，不能推断80次全是Form或刚刚发生 |
| 10月5日14:00复位后、24V关闭的缓存快照 | 肩rx0、err0，output0 | 只证明该时刻启动计数清零 |
| 10月5日14:04重新上电后events及寄存器 | rx19033、err29、last_err0x10；drop/invalid/fov/bus_off0；HSE bypass/PLL实际选中，BTR001A0005 | 本次启动确实重新发生错误；排除“只是上次启动的旧计数”。没有错误逐条时间，仍不能区分上电瞬间集中发生与稳定运行持续发生 |

上述10月5日原始证据均在tests/s3_capture/evidence-direction-once-20261005T134609551403Z、evidence-recovery-20261005T135953303329Z、evidence-direction-after-recovery-20261005T1404074080622Z。HSE成功证据在tests/encoder_capture/evidence-static-hse-20261003T0153385075330Z。

### 当前实现：确认的问题与暂不支持的解释

- Core/Src/board_can.c的record_error保存累计errors和最近HAL错误码；初始化/接收失败也可走此入口，不应把errors严格等同于坏报文数。未保存每类错误次数、首次/最近错误时间及错误瞬间TEC/REC。HAL_CAN_ResetError只清软件ErrorCode，不清项目累计计数，也不是物理总线恢复。
- 本地HAL_CAN_IRQHandler先取ESR、根据LEC映射HAL错误，再清LEC并调用错误回调。因此事后ESR的LEC=0不否定历史0x10；以后若增加诊断，分类以回调取得的HAL码为据，不能直接照抄回调中再读LEC的方法。TEC/REC是控制器收发错误计数，不等于项目累计errors，也可能随后下降。
- 0x10是HAL_CAN_ERROR_FOR，即CAN固定格式位被读成不应有的电平；不是GDS68业务错误码，更不是“收到的float字段格式错”。最近成功接收的0x029全零帧与最近错误没有关联编号；HSE成功窗口也持续收到该全零帧，不能将二者直接绑定。
- App/application/s3_shoulder_service.c的healthy要求can.errors==0；历史任一错误会阻止后续会话。Poll仅在ACTIVE/OBSERVING发送属性读请求，IDLE不启动连续SDO读取。这解释为何清计数后可能暂时能开始，但不是此次Form错误根因，也不构成放宽保护的理由。
- 核查board_can.c、board_can_tx.c、stm32f4xx_hal_msp.c、stm32f4xx_it.c及HSE overlay/main.c：CAN1/2时钟依赖、过滤器分区0/14、GPIO AF9、RX/SCE IRQ与HAL接入存在；FIFO/软件队列损失在失败快照中均0。现有证据不支持优先归因于漏开时钟、过滤ID错误、应用解析字节序或队列处理不及。
- BTR对应分频6、BS1=11、BS2=2、SJW=1，名义500kbps、14TQ、采样点85.714%；输入8MHz和APB1 42MHz是名义计算，未实测频率/抖动。SJW=1是合法值；不能凭它小就直接改大，更不能断言两节点TQ数量必须相等。

### 最可能的三类原因

| 顺序 | 假设及通俗解释 | 项目依据与反证/未知 | 最小区分办法（仅计划） |
|---|---|---|---|
| 1 | 终端支路、连接或线束导致信号反射/边沿失真：信号经过接头后出现多余跳变，接收端偶尔读错 | 肩侧历史120Ω、终端只有软件使能证据；实际模块与参考图并不完全相同。大量好帧和偶发Form符合这种可能性，但不是证明；无肩侧波形，不能点名某个接头或判模块损坏 | 先依据实际GDS终端开关电路核实有效终端，避免用断电读数直接加电阻；在不改变拓扑的情况下捕获出错附近H/L差分及MCU RXD波形，查反射/毛刺 |
| 2 | 两端实际位时序余量不足：名义速率相同，但采样与ACK应答的时刻过于接近边界 | HSI旧窗口失败、HSE有成功窗口，时钟方向仍有意义；10月5日已是HSE却仍报错，故“仍用HSI”已被否定。驱动实际采样点、时钟偏差、模块传播延迟未测 | 波形测位宽与CRC/ACK界定符附近边沿，核实两端实际时序；有证据后才提出单变量BTR对比，当前不抄论坛参数 |
| 3 | 模块供电或地电位瞬态：静态导通正常，但上电或USB/动力回路变化时参考电平短暂移动 | 参考地曾修正；3.3V收发器与24V驱动处于分离供电链，错误在重新上电后的窗口出现。没有时间关联或电压波形，不能认定只在上电发生，也不能认定当前共地缺失 | 先比较不复位条件下稳定供电期间的错误增量；若仅与电源/USB事件相关，再一次同步观察收发器3.3V及参考地差，避免反复盲开关电 |

三者可能同时影响同一条链路，现有证据不足以精确排列1和2的概率。收发器Rs脚/斜率设置、实际模块供电/焊接质量归在1和3内；没有依据直接指称假芯片或更换芯片。GDS68某版本固件缺陷目前缺少可复现或厂家确认的证据，暂不排在前三，也不刷固件尝试。

### Reference与外部资料：实际读了什么

1. 项目索引双CAN平台/设备适配行：沿用已核查ExoKit的Dynamixel接口边界；它没有适用的STM32 bxCAN错误处理。重新读取[BetaRobot固定c83dd84的bsp_bxcan.c](https://raw.githubusercontent.com/itgz123/BetaRobot/c83dd84d6c7717105c948d8d12f2fbb5f4d6b48c/bsp/bsp_can/bsp_bxcan.c)的CANConfig、CAN_ReceiveHandler、HAL_CAN_ErrorCallback：分开统计协议/溢出/总线状态有参考价值；回调读取LEC受HAL先清标志影响，ISR打印与动态注册不照搬。
2. [Rhino固定a4b24b8的gim6010_drv.c](https://raw.githubusercontent.com/Coderyne/Rhino-Pi-X1-ROS2HomeRobots/a4b24b80c00aa1cbf58d2a8a20167d8710d97bec/Code/STM32/ros2-wheel-foot-car/User/Devices/SW_Motor/gim6010_drv.c)：SWGetEncoderCount和GIM6010_fbdata按节点/命令拆ID、分派反馈；不能解决下层Form错误，不移植FDCAN/RTOS或MIT倍率。GALAXEA前轮已读的控制合成逻辑同样不能作为物理CAN故障修复依据。
3. 额外GitHub：[Zephyr can_stm32_bxcan.c](https://github.com/zephyrproject-rtos/zephyr/blob/main/drivers/can/can_stm32_bxcan.c)的can_stm32_bus_state_change_isr、can_stm32_get_state，实际把STF/FOR/ACK/Bit/CRC分别计数后清LEC；[stm32-rs/bxcan](https://github.com/stm32-rs/bxcan/blob/master/src/lib.rs)的Error/ErrorStatus区分收发计数、最近码和bus-off状态。按2026-10-05在线源码读取，仅作诊断结构参考，未移植或将浮动分支当本项目已验证版本。
4. [ST官方bxCAN正常模式指导](https://community.st.com/stm32-mcus-60/using-can-bxcan-in-normal-mode-with-stm32-microcontrollers-part-1-151183?fid=60&tid=151183)：核查共地、两末端终端、外部时钟及ACK机制。两节点中接收方仍须硬件ACK；保持应用不发送运动命令不等于改为silent模式。
5. [ST社区STM32F405偶发Form案例](https://community.st.com/stm32-mcus-products-25/stm32f405-bxcan-form-errors-7705)：作者报告大量正常帧中偶发错误，示波器观察ACK过早涉及CRC界定符；ST工程师讨论时钟漂移与重同步组合。它支持调查时序余量，不证明本机相同原因，不推出不同TQ数必然不兼容。
6. [TI SN65HVD230原厂数据手册](https://www.ti.com/lit/ds/symlink/sn65hvd230.pdf)：3.3V收发器、Rs控制边沿/待机、接收共模范围等用于判断实际模块；型号本身不构成3.3V节点不能与5V CAN收发器通信的证据。
7. [ST F446勘误ES0298 Rev8第2.15.1节](https://www.st.com.cn/resource/en/errata_sheet/es0298-stm32f446xcxe-device-errata-stmicroelectronics.pdf)：bxCAN不支持时间触发模式，要求TTCM清零；本项目TimeTriggeredMode=DISABLE，不支持把该已知限制当作本次根因。此结论不等于证明芯片绝无其他问题。
8. 知乎公开搜索：[Polelink上海北汇信息《基于Vector工具进行CAN协议错误帧的分析实践》](https://zhuanlan.zhihu.com/p/2058847595645162808)，认证机构作者的VN1630/VH6501干扰实验，展示界定符异常及错误标志重叠。一个节点报告Form可能是另一节点先发现错误后的次生表现；应关联整段波形，而不能只凭最后错误码定位首发节点。专用知乎skill未配置密钥，本次使用公开网页，无账号或密钥变更。

### 最小下一步与停止条件

1. 经用户指出重复排查后修正：不把再做30秒静态观察列为默认下一步。先复用10月3日4952ms主动读失败、随后约5分11秒被动收帧无新增错误，以及HSE30秒成功的既有记录；历史已存在运行中出错，不能用“所有错误都仅发生于启动”解释。10月5日HSE的29次Form相关记录仍缺具体发生时刻，但不能因此忽略既有实验、从基础验收重来。若将来确需新窗口，必须先说明相对这些记录要补哪个区分量，优先错误同步波形/分类时间而非又一段无区分力的累计计数。
2. 若稳定阶段仍增长，进入上表1/2的波形定位；若与供电事件相关，优先3。一次窗口尽量取得相关电压/波形；普通日志只能缩小范围，不能测出反射或采样边界。
3. 若现有缓存粒度无法保留首发错误，才考虑仅增加有界错误事件记录（时间/HAL分类/TEC/REC/收发状态），借鉴BetaRobot/Zephyr思想并另列小步实现计划。当前不写诊断代码，不改保护条件；任何涉及中断取证的实现先说明如何避免HAL已清LEC及ISR打印影响。

本轮没有硬件连接、抓总线、配置修改、构建、烧录、复位或电机动作。只有资料核查和文档补充，S2既有证据与S3/S4状态保持原记录；当前通信根因未确诊。

### 同日补充：按已尝试方法排除，替代笼统重复检查

用户要求回查既有日志、排除已处理的问题。本次重新逐段核对development_schedule.md的9月30日共地/接线、10月2日配置/来源对照、10月3日失败/邮箱/终端/HSE窗口与10月5日复现；另读旧read_only_diagnosis.json及HSI失败summary原始字段。以下“排除”均限定到具体假设；已采取措施不等于整类物理问题均已消失。

| 具体假设/重复方法 | 之前已经做过什么 | 现在的处理 |
|---|---|---|
| 完全没有共地；再补一根共地线 | 9月30日已加动力0V→逻辑GND专线，用户复测接近表笔短接值；之后能大量收帧 | 按既有实物报告排除“尚未接公共参考地”，条件未变不重复补线/基础导通验收；瞬态地电位仍是另一项未知 |
| CANH/L接反、TX/RX交叉或线路始终断开；从头重接 | 9月30日照片误判已纠正；后续真实CAN属性请求/回复、HSE连续600对及微动/停止成功 | 排除会使通信始终不通的固定接线错误；不据此排除间歇接触或波形失真，不要求从头拆接 |
| GDS波特率、协议、ID没有对上；再查默认值 | 10月2日实机HW3.11.1/FW0.6.5、CAN500000/SIMPLE/ID1；实际ID024请求→025匹配应答成功 | 当前既定配置不匹配不再列首因；已读参数不用重复询问。实际位时序裕量与名义速率不同 |
| GDS终端软件开关没开；再读/开启终端 | 10月3日01:12固定实机身份下读到enable_r120=true | 排除当时“软件开关未开”，无配置变化证据则复用；不等于终端硬件导通已测，不再次读相同属性或盲加电阻 |
| 仍用HSI、HSE没有生效；重新切换时钟/刷入同版 | 10月3日HSE刷写与独立读回匹配，RCC确认旁路HSE/PLL；10月5日再读配置一致而错误复现 | 排除“当前仍用HSI/未切成功”；HSI不是当前错误的必要条件。保留实际时序裕量候选，不能把已做的HSE切换当新方案 |
| 只是旧计数/会话卡住；关电复位就算修好 | 10月4日复位恢复reader后曾成功；10月5日相同步骤err0后重新出现err29 | 排除“纯旧计数残留可解释此次问题”。复位能恢复启动状态，已证明不能保证通信恢复；不默认反复复位 |
| 编码器没准备好、STM32把非零数解析成零 | 10月2日USB ready=true/error0、具名位置非零；CAN SDO非零且与USB接近，而周期029原始字节仍零 | 排除这两种全零解释；周期029来源差异已绕开取得可信反馈，不重新校准或改浮点转换来修Form |
| CAN带宽满、请求重叠/邮箱堆积、接收处理不及 | 10月3日诊断核过唯一发送路径、单请求/空邮箱；估算占用4.15–5.05%，失败无drop/FIFO/调度超限；后续邮箱全空 | 已有证据不支持这些为该BR窗口首因，降级；不把估算当测量，也不宣称证明所有并发代码永无缺陷。无需先改RTOS、加延时或降读取频率 |
| CAN完全无法发送/永远得不到ACK；NART导致所有通信失效 | 10月2日两次SDO、10月3日HSE1200提交/600对有效反馈、七属性及10月4日动作链通过；NART配置未变 | 排除持续性发送不可用；单次丢仲裁与硬件BR分开。不开自动重发去掩盖偶发错误，不凭邮箱ALST断言全部根因 |
| 每次错误仅是上电瞬态，或只有主动SDO才会出错 | 10月3日先已有rx12201/err0，读到第99对、4952ms时err0→1/BR；停止后rx12963→47205约5分11秒错误不增。10月5日又在未开始S3主动读前报Form | 两种“唯一原因”均不足以解释全部历史。不同镜像/窗口的BR、STF、FOR不能强行合并为同一个已经证明的根因；10月5日HSE错误时点仍未知 |
| 重复肘侧电阻和收发/离线/恢复验收 | 10月1日肘侧完整60Ω；10月2日静态收帧、断电离线、新帧恢复无新增错误 | 沿用已通过证据，不重做；不能反过来证明肩侧独立收发器和线束正常 |

修正后的调查范围：只保留肩侧实际电气链路的间歇问题（终端硬件/接触/边沿）、两端实际位时序裕量、供电/参考地瞬态；现有证据不足以给三者确定概率排序。共地缺失、软件终端未开、仍用HSI、设备名义配置不匹配、纯旧计数、基础SDO解析不通从清单删除；带宽/调度原因降级。此前将“终端电阻/接线”笼统放第一，未充分体现软件终端及基础接线已有证据，本节予以收窄。

下一项有价值的新证据是故障时刻的波形或错误分类/时序，而非重复已做检查。现有日志能排除以上具体解释，但无法单独分辨剩下的反射、采样裕量和瞬态。是否需要补硬件取证视下一任务条件决定；本轮不发起测试，不修改固件或物理接线。

## 2026-10-05：开始调查电气间歇与实际位时序余量

用户明确调查这两项，并确认现有仪器只有万用表。本轮未连接硬件、未读写驱动或MCU；先核查资料与离线计算，再按可用仪器取得新证据。仍保留前节排除项，不重测已确认共地、切HSE或重读相同参数。

### 终端与收发器电气路径

- 重新提取仓库GIM手册rev2.2 Word的“CAN匹配电阻开关”正文：板载120Ω可控，示例确为r120_gpio_num=5、enable_r120=True，与已实读相同。因此“GPIO参数明显设错/未打开”没有支持证据。官方资料下载入口及本地接口图未取得HW3.11.1内部终端开关原理图；不能推断其采用MOS、继电器或模拟开关，也不能保证断电时保持导通。
- 实机endpoint0的can.config只有baud_rate、protocol、r120_gpio_num、enable_r120、break_timeout、auto_bus_off、auto_retransmission；没有公开BS1/BS2/SJW或采样点字段。schema定义不是这些参数现值；本轮没有读新值。不存在已确认可用的“读一下驱动采样点”接口。
- 从Git原始PDF渲染并目视核查[Waveshare参考原理图](https://files.waveshare.com/upload/c/c5/SN65HVD230-CAN-Board-Schematic.pdf)：R1=10kΩ从Rs接GND，R2=120Ω跨H/L，无终端跳帽；图内未画去耦。只能代表参考板，不能由此声称实物缺去耦或电阻错误。
- [TI RevO第8页§8.9](https://www.ti.com/lit/ds/symlink/sn65hvd230.pdf)列10kΩ斜率控制下回环延迟：隐性→显性典型105ns/最大175ns，显性→隐性典型155ns/最大185ns；100kΩ对应最大920/990ns。这是芯片规定条件下的D→总线→R回环，不能当成实际整条链路延迟，也不能把此值直接与SJW相减判错。实际R1若与参考不同，会明显改变传播延迟预算，但目前没有实物阻值证据。
- 本轮已请求完全断开24V、NUCLEO USB及GDS USB后，提供CAN-01元件面近照（电阻数字/芯片丝印）及H/L大致线长，保持原接线不先拆动。这补的是此前未确认的Rs/实际终端器件及长度；不是重复要求证明CANH/L极性或共地。尚未收到这组现场资料。

### 当前STM32位时序：离线复算

基于10月5日已保存的实际BTR=0x001A0005及名义APB1=42MHz，逐字段复算：

| 项目 | 结果 | 边界 |
|---|---|---|
| BRP / BS1 / BS2 / SJW | 6 / 11 / 2 / 1 | 寄存器字段+1后值 |
| 一个时间单位TQ | 142.857ns | 假定名义42MHz |
| 位宽 / 速率 | 14TQ=2µs / 500kbps | 不代表示波器测频 |
| 采样时刻 | 位开始后12TQ=1.714286µs，即85.714% | 配置采样位置，不是实测边沿 |
| 采样后BS2 / 单次最大重同步SJW | 285.714ns / 142.857ns | 合法配置，不能只凭SJW=1判错 |

[TI SPRAC35第2页](https://www.ti.com/lit/an/sprac35/sprac35.pdf)给经典CAN时钟容差两项约束。仅代入SJW项df≤SJW/(20×NBT)，当前得到每节点对称最坏偏差模型的上限0.357143%。它不是实测时钟误差，不是已证明两端允许相差该值；完整校核还需相位段/传播延迟约束及对端配置。另一项min(Phase1,Phase2)/[2×(13×NBT−Phase2)]在Phase1≥2TQ的条件下为0.555556%，否则更小。STM32 BS1合并了传播与相位段，不能把全部11TQ都当Phase1而忽略线缆/收发器。

当前没有证据证明500kbps、85.714%或SJW1不合适；也没有足够证据证明整个两端时序预算满足。万用表不能验证上述百纳秒级边沿与实际采样裕量。增加SJW/改变BS1、BS2属于后续有依据的候选实验，不是本轮已经提出可直接刷写的修复；不修改固件，也不假借ODrive公开配置推定厂家FW0.6.5的内部时序。

### 适配万用表的下一步

先依据新的CAN-01照片识别是否真是参考电路、有没有不同Rs阻值，再决定必要的断电测量。若需要查线束间歇，应在完全断电并隔离被测线后测每根线端到端电阻、轻微改变接头/线束姿态看读数是否跳变；不重复只测整个H/L的一个120Ω静态数。具体可接触测点须由实物确认后再指导，不让表笔盲探未知XT30触点。此方法能查断续接触，不能排除反射/窄毛刺或证明驱动终端上电实际接入。严禁在带电CAN上用电阻档验证终端。

现阶段两项仍在调查，尚未定位根因。现场近照/线长待补；若实物与参考相符且线束稳定，剩下的边沿和两端时序需要波形或厂家对应版本资料，不能通过继续重复万用表检查替代。

后续用户补充：CAN-01到GDS68线长约15cm，确认模块元件面与官方一致。作为现场报告同步接口登记10.2；不再要求重复照片作为接线说明前置。本轮按用户新请求查既有完整接线，给出STM32→CAN-01→GDS68及供电/共地逐线说明。未获得新阻值、波形或驱动内部时序，电气与时序根因仍未确诊。

### 后续现场确认与旧HSE窗口重新对齐

- 用户确认完整接线无问题，按提示检查未发现端子/插头明显松动，已同步接口登记；降低明显松脱的优先级，不再重复此项。
- 重新读取旧HSE原始events（本段使用北京时间）：10月3日23:54:12.109肩rx14773/err0/age8ms；10月4日00:07:12.229肩rx23710/err1/last0x08/age698928ms；00:21:31.350仍rx23710/err1/last0x08/age1558098ms。status的uptime持续增长，没有此间MCU复位证据。文件分别为s3-relative-trial-candidate/trial-20261003T155411933025Z/events.jsonl，以及s3-uart-reply-integrity下cached-probe-20261003T160712110326Z、postfix-cache-20261003T162131239030Z的events.jsonl，位于D:/codex_tasks/2026-10-01/task/。
- 由两次主机时间减age估算，最后成功接收约在10月3日23:55:33，两估值相差约49ms（UART/查询延迟，非精确线上时间）。这证明该启动先有正常接收，后来停止刷新且累计一次Stuff；后面约14分钟计数未再增长。错误本身只能定位在23:54:12至00:07:12之间，不能将最后收帧时刻当错误发生时刻，更不能当精确关电时刻。它不证明10月5日的Form同源，但进一步说明累计错误快照不能直接称持续带电故障。
- 当前有价值的厂家信息清单（仅草稿、未发送）：请针对GDS68 HW3.11.1/FW0.6.5、CAN Simple 500kbps，提供①CAN内核时钟来源/频率和实际BRP/BS1/BS2/SJW（或等价采样点）；②板载120Ω的开关电路/驱动条件，GPIO5为true时何时生效，断电是否仍导通；③CAN收发器型号及适用于该硬件版本的终端检测办法。现有STM32对照参数6/11/2/1及HSE已核实；不给厂家套用ODrive配置或推定已知固件Bug。没有授权对外联系，本轮未发消息。
