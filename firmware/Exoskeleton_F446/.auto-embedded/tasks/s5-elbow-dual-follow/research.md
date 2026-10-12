# S5 肘部与双关节 FOLLOW：研究发现

日期：2026-10-11；当前任务阶段 `EXECUTE`。以下开源参考仍用于 R5 双轴协调计划；早期 R1 软件调查已被随后 R2/R4 台架记录更新：肘已上板、观察到短时动作和出力，最近 R4 `4 A` 窗口故障后未取得正常停止或测力值。R5 仍只离线构建、未上板。开源索引对应第 9、11、12、17、18、21、22、25、28 行；BetaRobot、GALAXEA、SymPyBotics、UICRM 是仓库固定 Gitlink，链接指向其原仓库固定提交。

## 本机事实

- `Main_ project/docs/development_schedule.md` 2026-10-11 最新记录：S4 仅按用户选定的低姿态范围通过，有界 FOLLOW、非零目标读回、主动停止、软件撤权与主观支撑感；助力段反馈角相对基线最多约 2.51°。定量减负、手推重定位、角度偏移来源和速度门物理安全性未验证。用户要求先做 S5，具体力值随后一起测；试后报告 24V 关闭、机构承托静止。生产 `s4_site.c` 仍 `enabled_axes=0/output_enabled=false`。
- 阶段表 S5 行及最新开发日志是当前进度依据。R2 后续短脉冲用户观察到两次轻微屈肘；断电手动改变肘角时电机输出端与前臂同步、未见打滑。R4 `2 A` 有一次正常停止，最近 `4 A` 启动时用户却感到伸直方向一下，随后故障；方向和故障唯一根因仍未知。肩 GDS68/CAN1，肘**裸 M3508+C620/CAN2**；没有 SHOULDER_BACK、P19 减速器或实时 IMU。用户口述肘部同轴直连，按 1:1 作为小角域软件候选。C620 协议满量程 ±20 A 只是封包范围，不是运行限值。
- `App/devices/c620_feedback.c::C620_Receive` 解析原生 0–8191 单圈/RPM/电流；`App/devices/motor_command.c::MotorCommand_C620` 发送电流 A 组帧。`App/control/joint_state.h::JointState_FromRotor` 要求可信连续角；单圈原值不能冒充装配肘关节角。`App/application/elbow_session_angle.c::ElbowSessionAngle_Observe/Read` 在新鲜、静止反馈上建立 RAM 会话零点，只在受限小区间以最短差解跨零，跳变/过速/过期使零点失效。R5 的 `S5Follow_Start/Poll` 与 `OutputGateway_RefreshAxesFollow` 已接入两轴共同许可、限幅与撤权的软件链；两路 CAN 的实际发送并非同一物理时刻。R5 示例 ELF 和主机测试不构成双轴上板证据，生产 S5 profile 仍禁用输出。
- R1 当时的执行者报告：H 盘临时目录下 WindowsDebug configure/build 退出 0、0 warnings，23 个相关 S3 主机用例 PASS，`git diff --check` 通过。这些结果只支持当时的软件集成；后续 R2/R4 实物观察另以上方当前状态及唯一开发日志为准。

## 一手参考：原文短摘／函数、借鉴、适配与下一步

| 来源与原文位置 | 借鉴 | 本项目不照搬／下一步 |
|---|---|---|
| [ARMin，Just 等，2020，原论文 Methods → Average，式(1)–(2)、Fig. 1](https://pmc.ncbi.nlm.nih.gov/articles/PMC7003349/)：短摘 “two independent, rigid segments”；同文 Full 与 Equilibrium 小节 | 人体上臂/前臂按两段刚体处理：质量、质心位置与当前姿态构成重力 wrench，再由各接触点 Jacobian 转成关节补偿力矩；Average 用人体参数表，Full 用静姿力传感数据估计三维质心，Equilibrium 用静姿力矩平衡。 | 这是**人体重量**补偿模型，不是本机机构两杆已测参数或可直接运行的固件公式。ARMin 的多自由度结构、参数和临床效果不是本机值；完整几何/定量测力留在 S5 低姿态功能跑通后。 |
| [GALAXEA `gravity_model.py::compute_gravity_torque` L38–53](https://github.com/userguide-galaxea/GALAXEA-A1Z/blob/9006107aa8d4334c762b8bf8e8fe40b3f9477717/a1z/dynamics/gravity_model.py#L38-L53)：`rnea(q, 0, 0)`；[`arm_robot.py::_update` L676–750](https://github.com/userguide-galaxea/GALAXEA-A1Z/blob/9006107aa8d4334c762b8bf8e8fe40b3f9477717/a1z/robots/arm_robot.py#L676-L750) | 静态重力 Nm；新鲜状态→安全检查→模型/附加项合成→逐轴裁剪→发送。 | `_update` 实际调用含 `cmd.vel/acc` 的完整逆动力学，仅静态输入才退化为重力；不复制六轴 URDF、Pinocchio/Python、参数或限值。肩 Nm、肘 A 必须各在设备边界转换，不能直接相加。 |
| [SymPyBotics `rne.py::gravityterm` L35–44](https://github.com/cdsousa/SymPyBotics/blob/5d4613abd9ca6563cad470d212a161f9cf98ad93/sympybotics/dynamics/rne.py#L35-L44)：`dq=0, ddq=0`；[`RobotDynCode.g_code` L65–66](https://github.com/cdsousa/SymPyBotics/blob/5d4613abd9ca6563cad470d212a161f9cf98ad93/sympybotics/robotmodel.py#L65-L66)；[`gen_c_func` L131–150](https://github.com/cdsousa/SymPyBotics/blob/5d4613abd9ca6563cad470d212a161f9cf98ad93/sympybotics/symcode/generation.py#L131-L150) | 离线分离静态重力项、生成 C 表达式并交叉核对。 | 不提供本机轴系/质量/质心/零位；数据齐备后再用，不在 STM32 控制循环跑符号库。 |
| [ExoKit `ArmComposedReactionGenerator.h::parallelize/init` L34–49、75–101](https://github.com/HCI-Lab-Saarland/ExoKit/blob/adcad9f8870bc1cef6bd720ac43b27cf97a526ad/Arduino%20API/lib/exokit_api/src/event/reaction/fabric/ArmComposedReactionGenerator.h#L34-L101)；[`ParallelReaction.cpp::shutdown/finishCondition` L23–37](https://github.com/HCI-Lab-Saarland/ExoKit/blob/adcad9f8870bc1cef6bd720ac43b27cf97a526ad/Arduino%20API/lib/exokit_api/src/event/reaction/ParallelReaction.cpp#L23-L37)，短摘 `c_currentReaction1->shutdown(isCancel)`、`c_currentReaction2->shutdown(isCancel)`；本仓库 HEAD `ExoskeletonArmHandle.cpp::getAngles` L78；[原手册](https://github.com/HCI-Lab-Saarland/ExoKit/blob/adcad9f8870bc1cef6bd720ac43b27cf97a526ad/ExoKit%20Manual%20V1.pdf) 印刷 p.6、19、22 | Arm 对有效 Joint 组合并行 Reaction；借鉴两轴统一开始/运行/取消/清理语义。 | 不复制 Arduino/Dynamixel、动态 `new/delete`、第三肩轴。手册图示两种动作却未给装配坐标变换，不能由它断定本机两轴平行/共面或套固定二维 `sin(q_s+q_e)`。 |
| [BetaRobot `DJIMotorBroadcast_GetData` L174–296](https://github.com/itgz123/BetaRobot/blob/c83dd84d6c7717105c948d8d12f2fbb5f4d6b48c/drv/drv_motor/drv_djimotor/drv_djimotor_broadcast.c#L174-L296)、[`SetRef/Send` L597–655](https://github.com/itgz123/BetaRobot/blob/c83dd84d6c7717105c948d8d12f2fbb5f4d6b48c/drv/drv_motor/drv_djimotor/drv_djimotor_broadcast.c#L597-L655) | 大端原始反馈、时间戳/跨零累计、偏置和方向，目标设置与发送分离；源码亦指出丢帧会使累计角不可信。 | 不复制型号参数、转矩常数、环路增益或 FreeRTOS 封装；其位数注释与 8192 刻度有出入，以本机手册/反馈为准。先核肘原始序列与小扇区。 |
| [UICRM `DjiMotorBase.cpp::AngleTracking` L144–174、`SetTorqueFeedforward` L259–272、`Motor3508::UpdateData` L493–514](https://github.com/UIC-RoboMaster/UICRM-Embedded/blob/225427b004568e75069f0ed7a92180a548082295/boards/drivers/src/Motor/DjiMotorBase.cpp#L144-L174) | 独立交叉核对跨零、Nm→A/raw 的 Kt 条件和 0–8191/RPM/电流解码。 | 不套 P19/库默认减速比；[示例 `m3508_torque_ff.cpp`](https://github.com/UIC-RoboMaster/UICRM-Embedded/blob/225427b004568e75069f0ed7a92180a548082295/examples/DJI_Board_TypeC_general/motor/m3508_torque_ff.cpp) 是按目标角误差方向给前馈，并非姿态重力补偿。未知本机 Kt 时不把 A 称为关节 Nm。 |

## 缺口与最近一步

开源索引第 22 行还列 StateSmith 作为双关节动作协调参考；本轮沿用已经实现的静态 S5 会话，不引入状态机生成器，也不声称复用了 StateSmith 的具体源码。ExoKit 的 `ArmComposedReactionGenerator::parallelize/init` 和 `ParallelReaction::shutdown` 只提供统一启动与分别清理两轴的生命周期思路；本机仍由 `S5Follow` 与唯一 `OutputGateway` 实现。

用户口述的同轴直连 1:1 与断电手动同步观察足以支撑低姿态小角域候选，但带电肘输出方向有矛盾观察，不能写成已定。肩/肘完整三维轴向与相对位姿、前臂/负载质量和质心、C620 电流到关节力矩的标定也未齐备，不能由此生成可信的全姿态双轴重力公式。完整三维装配坐标和质心不作为低姿态功能演示前置；用户已选择收束肘单轴，R5 下一步是在一个现场窗口观察两轴共同支撑、手动小幅变姿态后继续跟随及正常停止。现有肩模型与肘固定电流都是局部候选，联动不等于精确耦合补偿；异常结束窗口并保留记录，不靠提高限值继续试。定量测力和精确模型留在功能演示之后。

2026-10-11 补记：R4 `+1/4000 mA` 是用户提出实际扶持力翻倍目标后制备的**电流指令离线候选**，不是上述论文或开源项目给出的力值，也不是已测关节力矩或扶持力；没有 `F_on` 读数，不能推定扶持力翻倍。
