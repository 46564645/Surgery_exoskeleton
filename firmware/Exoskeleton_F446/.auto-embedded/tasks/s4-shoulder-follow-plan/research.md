# S4 肩侧 FOLLOW：RESEARCH 证据

日期：2026-10-07（Asia/Shanghai）；trace_id：`T-s4-shoulder-follow-plan`。本文件记录计划依据、R0 执行审计和 R3 参考源码复核；这些记录不代表台架验收。当前分支 `main`；开始时工作树仅有用户的未跟踪 `.vscode/`，本任务不触碰它。

## 已确认的任务边界

| 事实 | 证据 | 对 S4 的含义 |
|---|---|---|
| S4 只做右肩侧 FOLLOW；肘部固定在已知且承托的姿态，比较同姿态无补偿/有补偿的保持努力，并手推到另一姿态后恢复支撑 | `Main_ project/docs/development_schedule.md` 阶段表 S4 行；`Main_ project/docs/Drafts/project_requirements_blueprint.md` §3、§6 | 首版是一个关节的姿态相关重力基线，不含肘部控制、Lock、Amplify、IMU、HX711。 |
| 肩侧 S3 仅按用户 2026-10-06 缩减范围收尾；板上仍为输出禁用的 HSE 只读黑匣子 | schedule 开头、§1.1 与 2026-10-06 23:54–23:55 日志 | 可以做 S4 方案及离线代码；S3 范围豁免不能当作 S4 持续出力的实测证据。 |
| 当前接线/电机 USB 条件下，主动 CAN 只读 30 秒、1200 次请求、600 对 MCU 反馈、300 个主机新鲜样本，错误/丢帧/溢出 0 | schedule 2026-10-06 23:54–23:55 日志和其 `active-read-...` 原始证据 | 证明该窗口可读连续转子位置/速度，不证明同时持续输出、长期可靠性或 CAN 旧错误根因。 |
| 10 月 4 日仅有一次肩侧 20 mNm/100 ms 微动及停止观察；速度符号曾矛盾 | schedule S2/S3 日志及 2026-10-06 汇总 | 不把一次短脉冲改成长 TTL 当作 FOLLOW，也不把原数值当持续运行限值。 |
| 本机 GDS68 为 HW3.11.1/FW0.6.5、node 1、CAN 500000、SIMPLE；客户端减速比读值 8，未独立验证输出到关节的整体比例 | `Main_ project/Hardware_information_list/hardware_interface_registry.md` §10；schedule 2026-10-06 实读记录 | 实读配置与实际关节几何/正方向分层记录；不套新手册的运行上限。 |
| 2.8982579708099365 转子 rev 仅与用户目视静止姿态配对，未定数值 0°、重力零位、正方向、外部传动和活动区间 | schedule §1.1 与 2026-10-06 目视记录 | 不能写入 S4 零位、模型、工作区间或启用许可。 |

## 当前源码入口及缺口

| 模块 | 已核对的接口/行为 | S4 缺口 |
|---|---|---|
| `App/control/joint_state.h/.c` | `JointState_FromRotor()` 把连续转子 rad/rad/s 转为关节 rad/rad/s，要求已确认 `JointMapping` | 参数尚无实物映射；S4 不改其单位合同。 |
| `App/application/s3_feedback.c`、`shoulder_property_feedback.c`、`can_monitor.c` | 七属性周期快照有请求/回复时间戳、完整序列和失效锁存；`S3Feedback_Read()` 可在映射已确认时填出 `JointState`，但 `enabled_axes=0` 时整体返回 false | 当前生产 `s3_site.c` 全部许可/限值为 0；S4 只读观察需独立读就绪门和不授予输出的数据接口，再验证连续读取与输出共存时的时序。 |
| `App/application/encoder_stream.c` | 已有 30 秒只读位置/速度会话；同一启动中读所有权互斥 | 只读诊断不验证输出准备/驱动模式，不能直接担任 S4 控制采样器。 |
| `App/application/s3_shoulder_service.c`、`shoulder_read_diagnostic.c` | S3 七属性读会话有固定窗口、一次所有权；`Stop()` 会失效化当前属性并退役 reader；读请求走 `BoardCanTx_SubmitShoulderRead()` | S4 要有独立、有限总时长的连续读会话，不能与 S3/诊断/encoder stream 并发。停后只可在既有有限读尾窗取得 IDLE/新鲜姿态，不可同一冷启动重开旧 SDO 事务。 |
| `App/control/safety_supervisor.h/.c` | `SafetyConfig` 检查启用轴、已确认映射/范围/速度/协议、反馈时间戳、CAN 和停止条件 | 现有肩侧受监督策略专为单次 S3；S4 需明确的新策略和会话上限，不能伪造未安装的停止 GPIO。 |
| `App/application/output_gateway.h/.c` | 唯一出力入口：Prepare→Arm→`Replace`→`Step`，先撤权、清旧邮箱、发停止；当前肩侧受监督策略冷启动一次请求；现有 `Step` 每个服务 tick 都会重发旧目标 | `Replace` 是单轴、带 TTL 的 S3 请求；S4 要只凭新样本刷新目标，并为仍新鲜样本的斜率步进限频，有总会话期限，不可让输出持续发送饿死七属性读取。 |
| `App/devices/motor_command.h/.c` | 肩侧现有 `MotorCommand_GdsV22Torque()` 是 SIMPLE 转子侧 Nm、命令 `0x00E`；`Idle()` 和扭矩模式准备已有编码器 | FW0.6.5 的该命令行为仍按实物核验；没有 MIT 发送器。 |
| `App/application/app_main.c`、`console_commands.c`、`CMakeLists.txt` | 1 ms 静态调度、S3 命令和显式源文件列表 | 无 FOLLOW 状态、命令、遥测和 S4 模块集成。`main.c` 应只保留初始化编排。 |
| `App/application/s3_site.c` | 默认全零禁用，只有 node/肘 ID | S4 实物参数必须集中管理，默认禁用，不复制 S3 单次会话为可动作配置。 |

本机工程识别：STM32F446RET6/NUCLEO-F446RE、CubeF4 V1.28.3、HAL/CMSIS、裸机、经典双 CAN、CMake/ARM GCC。`python .auto-embedded/tools/shared/project_detect.py .` 识别 Windows/CMake；它把现存实验 ELF 列为首个 artifact，不能由此推断板上镜像。`.auto-embedded/spec/hardware/hw-lock.yaml` 沿用已选针位，不新增引脚、DMA、中断或定时器；该旧锁中的 `configured_no_bus_test` 注释滞后，实际阶段和现场证据以 schedule 为准。

本机当前 Python 执行 `import sympy` 返回 `ModuleNotFoundError`。S4 首版一自由度模型采用可手核的静态公式和主机数值检查，不把安装 SymPy 作为固件开发前提；SymPy/SymPyBotics 是已读的离线推导参考。

### 固定肘姿态的一自由度推导草案

只在肩轴绕水平轴、固定负载的质心随肩角在竖直平面内转动这一几何条件成立时，记各负载的质量 `m_i`（kg）、到肩轴的有效力臂 `l_i`（m），`q−q0` 为相对自然垂臂的关节角（rad），则势能可写成 `U(q)=−g Σ(m_i l_i) cos(q−q0)`（J）。静态保持所需的关节侧力矩为 `dU/dq=g Σ(m_i l_i) sin(q−q0)`（Nm）。实际关节正向不同可由经现场确认的 `sign_g∈{−1,+1}` 表示；`amplitude_nm=g Σ(m_i l_i)` 必须由本机固定肘/负载估计和小范围响应复核。转子侧理想换算为 `assist_gain × tau_joint / rotor_per_joint`，最终仍受独立转子侧限幅；这个式子不假设已知传动效率或实际扭矩标定。几何条件不符则撤回这个候选模型，不靠调大增益补救。

## 功能对应表和实际参考源码

开源借鉴索引当前被 Git 稀疏检出跳过，已用 `git show HEAD:'Open-source projects/开源项目借鉴索引表.md'` 读取，不能称其在工作树中展开。S4 对应第 11、17、18、21、25、28、29、30 行：关节坐标、离线重力推导、连续补偿、同单位合成、限制、安全/超时和命令。第 17/18/21 行标为“自己编写（开源辅助）”，不是现成可复制控制器。

- ExoKit `ExoskeletonJointActiveHandle.cpp` 的角度符号/零位与裁剪、`CmdReactionRunner.cpp` 和 `IReaction.cpp` 的启动/取消语义已核；其 API 中未找到连续重力补偿。仅借鉴关节和动作语义，不引入 Dynamixel、Arduino、`new/delete` 或动作直接出力。
- GALAXEA-A1Z 固定提交 `9006107` 的 `a1z/dynamics/gravity_model.py::compute_gravity_torque()` 使用关节角和零速度/加速度求静态重力 Nm；`a1z/robots/arm_robot.py::_update()` 展示反馈→计算→力矩合成→逐关节裁剪→发送。其实际动态路径也可调用含速度/加速度的 inverse dynamics，且部分角度越界只告警，均不能照搬本项目。
- SymPy 官方 C codegen、SymPyBotics 固定提交 `5d4613a` 的 `RobotDynCode.g_code` / `gravityterm()` 为离线模型推导工具；本机固定肘姿态、负载、零位和质心不来自参考机器人。
- 联网复核 [GALAXEA-A1Z 项目说明](https://github.com/userguide-galaxea/GALAXEA-A1Z) 与 [SymPy 官方代码生成文档](https://docs.sympy.org/latest/modules/codegen.html)：前者仍是六轴机器人/Pinocchio 路线，后者提供符号表达式到 C 的工具说明。联网资料只用于确认参考路线，不替代上述固定提交源码、本机实物参数或现有固件接口。
- BetaRobot 固定提交 `c83dd84` 的 `drv_daemon.c` 是在线倒计时/回调参考，`drv_djimotor_broadcast.c` 展示计算和发送职责分离；不继承其电机限值或软停效果。
- 厂家 rev2.2 与本机 FW0.6.5 的 MIT 对照已在 `docs/reference/gds68_mit_adaptation_notes.md` 归档：MIT 的 0x028、输出轴单位、量程端点 335–339 均不证明本机模式切换和异常停机。S4 不能因这份研究改用 MIT 或宣称 CAN Form 错修复。

## 带动力前仍缺的事实

1. 小工作区间中关节正方向、输出/关节比例、重力参考姿态与机械余量；用户先前跳过的是 S3 追加测角，不恢复为 S3 验收。S4 仅获取能使模型和限幅有意义的最少新证据。
2. 固定肘姿态和负载的重力参数来源；可以先离线推导/仿真，参数未冻结时只能输出候选值而不输出 CAN。
3. FW0.6.5 持续 SIMPLE 转矩、实际丢命令/丢 CAN 后停机以及可触及的 24V 切断方式。软件撤权或 CAN 停止帧不能单独证明断线后停机。
4. 七属性连续读取与转矩发送共用 CAN1 时的实际新鲜度、邮箱占用和错误增量。旧 30 秒纯只读结果只是起点。

在这些事实未成立前，可以完成模型、控制结构、主机测试和默认禁用构建；不能宣称带动力 FOLLOW 通过。S4 首版不引入新机械件或全故障矩阵。

## 2026-10-07 正常撤力与漂移验收补充核查

- 用户补充：正常取消/会话到期应有界斜坡撤力，故障可立即硬撤；到期前提示扶稳；S4 纯前馈允许缓慢漂移，不按 S6 Lock 原位保持。`App/application/output_gateway.c::withdraw()`、`OutputGateway_Cancel()` 和 `OutputGateway_Step()` 的到期分支当前都先撤权、清 `applied` 后提交 GDS IDLE，没有保留可降至零的 S4 撤力子状态；直接复用会硬切。`s3_bench.c` 的取消也会结束 reader，S4 斜坡期间必须继续独占读取，零 Nm/IDLE 后才进入既有有限观察尾窗。旧 S3 路径维持原样。
- `Core/Src/board_console.c::BoardConsole_Write()` 只把固定文本非阻塞写入有界 TX 队列，`BoardConsole_TxFree()` 可保留命令回复容量；入队不证明 PC 端实际显示。`App/application/app_main.c` 当前先 Poll console，再 Poll CAN/输出和命令，因此 S4 服务产生的提示最早在下一调度 tick 启动 DMA，不能贴着硬截止才发。
- 旧 2026-10-06 静止只读 30 s 窗口转子位置范围 0.000231 rev，但本机输出到关节比例尚未确认，不能换算为 S4 关节角噪声或漂移容差；旧 2026-10-04 微动时速度符号曾与位置趋势矛盾。S4 需先确认映射，再以完整新鲜位置序列及半秒窗口中位数判 5 s 漂移；速度只辅助查因，停止后的约 1 s 读尾窗不能替代 FOLLOW 出力期间的重定位观察。

## 2026-10-07 R0-1 执行审计

- 复核接口登记 §4.2.3：HW3.11.1/FW0.6.5、CAN 500000、ID 1、SIMPLE 是实物只读配置；客户端减速比 8.00 仍非独立确认的输出至关节总比例。2026-10-06 配对的 2.8982579708099365 rev 是转子原生读数，不是关节零位或重力参考姿态。
- 复核唯一开发日志 2026-10-06 23:54–23:55：主动 CAN 只读窗口已通过，但只证明当时可取得新鲜转子数据；旧 20 mNm/100 ms 单次微动与停止不能外推持续出力、撤力斜率或断线停机。S4 的方向、负载、角度映射、活动区间、限值和现场停止参数仍需后续只读/短窗证据。
- R0 不新增硬件事实，也不更改接口登记。进入 R1 只做纯算法、默认禁用配置及主机检查；缺失现场参数保持为零/未确认。修改前 `python tests/run_host_tests.py s3_mapping` 退出码 0。源码审计发现 `GravityCompensation_Compute` 无当前时间参数，故只检查有效标记/有限量；新 `S4Site_*Ready` 只有配置参数，故只核配置，实时样本新鲜度留 R2/R3 检查。

## 2026-10-07 R3 参考源码、采用范围与缺口

写 R3 前后复核索引第 24–30 行及本工程接口。稀疏检出的 ExoKit 源码通过 `git show HEAD:<路径>` 读取；其余链接指向所读的公开项目源码。以下的“借鉴”均为组织和行为语义，不是将参考项目的参数、线程或安全结论复制到本机。

| 索引行 | 实际源码/函数 | 借鉴与本工程适配 |
|---|---|---|
| 24 模式平滑切换、25 限制 | [GALAXEA `arm_robot.py`](https://github.com/userguide-galaxea/GALAXEA-A1Z/blob/main/a1z/robots/arm_robot.py) 的 `_update()`、`_check_runtime_safety()`；[CMSIS-DSP 源码](https://github.com/ARM-software/CMSIS-DSP) 作为数学库方向核对 | GALAXEA 展示反馈、计算、约束、发送次序；其机器人模型和数值不可移植。索引第 24 行“拿走使用部分”为空，所查源码也未给出适用于本机 SIMPLE Nm、CAN 邮箱确认和有界 IDLE 提交的完整撤力方案。因此用户批准的 S4 计划在现有 `OutputGateway` 内实现绝对值单调的正常撤力和独立限幅，故障仍硬撤；没有引入 DSP 滤波器。 |
| 26 状态机、27 动作管理 | 本地 ExoKit `Arduino API/lib/exokit_api/src/com/serialreactions/CmdReactionRunner.cpp::launchReaction/cancelReaction`、`src/event/Event.cpp::performRunning/cancel`；[StateSmith C 示例 `LightController.c`](https://github.com/StateSmith/StateSmith-examples/blob/main/c-include-sm-basic-1/LightController.c) 与 [`LightSm.inc`](https://github.com/StateSmith/StateSmith-examples/blob/main/c-include-sm-basic-1/LightSm.inc) | 借鉴“启动前取消旧动作”、运行/取消明确转换及 C 事件分派；S4 用已有静态 reader/网关状态和冷启动一次会话，不引入 ExoKit 的动态 `Event`、Arduino 调度或 StateSmith 生成器。故障不得由普通取消恢复许可。 |
| 28 安全、29 周期健康 | 本地 ExoKit `src/exo/ExoskeletonHandle.cpp::ExoskeletonRomKillSwitchExecutable::exec/checkJoint`；[BetaRobot `drv_daemon.c`](https://github.com/itgz123/BetaRobot/blob/master/drv/drv_daemon/drv_daemon.c) 的 `DaemonReload/DaemonTask`；GALAXEA 上述 `_check_runtime_safety()` | 借鉴越界/离线独立于动作的否决和按实际时距判超时；本工程保留裸机 `SafetySupervisor`、统一 `OutputGateway` 及故障锁存。没有增加或宣称硬件 IWDG；服务超时、反馈新鲜度、CAN/串口错误都由现有周期检查否决，软件停止帧不等于已实测物理停稳。 |
| 30 固定文本命令 | 本地 `third_party/clip/src/clip_cmd_parse.c`、`clip_cmd_call.c::clip_cmd_call_command_callback`，以及 ExoKit 上述命令/动作入口 | 直接复用已集成的 CLIP 静态命令注册和无参数解析；仅增加 S4 五条命令与状态文本。`cancel` 分流到当前 S4 会话，S4 独占时旧 S3 出力命令被拒绝。 |

R3 的 Nm 斜坡、零帧 TXOK 后提交 IDLE、读发共用 CAN 邮箱时的证明链为项目特定逻辑；参考项目没有可在本机不改协议/驱动情况下直接复用的实现。用户已明确批准本计划的离线实现。实际 S4 数值仍全为未确认/默认禁用，主机 mock 仅证明软件分支和顺序，不能证明实机持续 SIMPLE 转矩、CAN 异常停机或人的卸力感。
