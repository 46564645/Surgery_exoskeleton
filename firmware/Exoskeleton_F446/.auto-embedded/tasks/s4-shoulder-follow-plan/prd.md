# S4 肩侧 FOLLOW 开发计划

## 2026-10-10｜S4 收尾计划（当前执行顺序）

**当前事实与目标。**低姿态命令、七属性反馈和正常停止链已有有界台架证据；增益 `0.18` 的隔离镜像在约 23°连续约 3 秒读回 `14.37–14.39 mNm` 的 GDS68 `INPUT_TORQUE` **目标**，操作者仍未感到明显减负。生产 `s4_site.c` 继续禁用输出，板上隔离 BIN 的 SHA256 为 `ca77d711b525df9e042696d83ca811e32623dd507e3f0f5ed4676f8be6846b0b`，最近一次现场回复是 24V 已关闭、机构承托且静止。S4 当前收尾先回答“带电、准备前的阻力从何时出现”“既有命令是否转化为可辨的净托力”；小范围 FOLLOW 手推重定位与支撑恢复另列后续，当前测力轮次不执行。不重复已经通过的零位、读发、正常停止整套验收；细节和原始数据见 [R4-3 现场记录](../../../docs/bench/s4-r4-3-20261009/README.md)与唯一开发日志。

**第四轮现场增补。**用户跳过断电对照，并事后纠正带电 IDLE 前抬没有做好；本轮没有有效阻力对照。其后一次 `followprepare` 已取得 RAM 零点，但在约 20° 下界准备约 79 ms 后按 `SAFETY_RANGE` 故障撤权，最后有效反馈为 `19.99464°`，未发 `followstart`，无本轮 READY／FOLLOW 测力或重定位数据。用户确认 24V 已关闭、机构承托且无明显异常。保全的同镜像故障 RAM 和证据解读见 [R4-3 现场记录](../../../docs/bench/s4-r4-3-20261009/README.md)。当前保持断电，不因该极小软件越界放宽 20°–30° 安全角域或提高输出；若另约窗口，应先取得有效的带电 IDLE 观察，再连续越过下界并在约 25° 区间内部站稳后判断。本轮没有改变下表的效果判据，也没有令 S4 效果或 FOLLOW 重定位通过。

**第五轮现场增补与暂停点。**按用户选择跳过断电前抬，带电未准备 IDLE 前抬完成且未报告明显异常；随后取得 RAM 零点，用户表示持续向约 25°前抬。新鲜反馈在约 21.03°已进入 READY，而主机要到 24.5°才提示停扶，脚本主动中止；无固件故障、无 `followstart`、无零目标／FOLLOW 测力或重定位结果。补发 `followstop` 得到 `idle_submitted; observing_stop`，未观察 STOPPED 终态；用户确认已关闭 24V 并承托。固件允许 20°–30°内任何新鲜站稳点进入 READY，约 25°主机提示目标与其不匹配；不能把这一反馈平台归因为用户主动停顿或已证实的实物角度。用户明确要求保留约 25°测力目标、停稳才读量具、视频事后对齐、异常即结束且不重复同一条件。先解决该流程冲突，再评估新的现场条件；不以约 21°替代本次测力目标，也不提高增益／帽或放宽安全角域。原始证据与边界见 [R4-3 现场记录](../../../docs/bench/s4-r4-3-20261009/README.md)。

**第五轮后主机离线准备。**下一轮仍跳过断电前抬：带电未准备 IDLE 由操作者按自己的节奏前抬、停稳观察、回垂臂并确认；之后才在同轮另行确认 RAM 取零。在约 25°停稳测力，约 2 秒 READY 零目标与约 3 秒 FOLLOW 的量具读数只从视频事后判读，不要求出力中口报。主机已离线增加提前 READY 守卫：若在 24.5°长音前 READY，须持续核新鲜反馈、`control_mode/input_mode=1/1`、闭环心跳和驱动精确零 `INPUT_TORQUE` 目标，才继续缓慢前抬；反馈到达或跨过 `24.5°` 时发长音，基线须处于 `24.5°–25.5°`，两秒内名义关节角跨度 `≤0.25°`、峰值速度 `≤2.5°/s`。新的 `--measure-only` 显式选项已新增，测力后跳过轻推并主动正常停止，旧 `--run` 与未指定该选项的 `--closeout` 路径保留。早 READY 和测力专用主机分支在 H 盘离线自检退出 0，独立审查未见 P0/P1；尚未接真实串口、上电、刷机、测力或物理停止验证。新 `closeout20261010-run6/` H 盘证据目录已建且为空；候选 BIN 与板上独立读回各 119036 B、SHA256 均为 `ca77d711b525df9e042696d83ca811e32623dd507e3f0f5ed4676f8be6846b0b`。增益 `0.18`、固件 `20°–30°`、`20 mNm`、`15°/s`、10 秒会话及停止办法均不变，第五轮原始失败和未测项保持原样。

**第六轮现场增补与暂停点。**用户授权同一隔离镜像及未变的角域、命令帽、速度门和 10 秒会话；`--direct-idle --measure-only` 的独立 H 盘 [摘要](../../../build/s4-r4-3-gain18-board-20261010/closeout20261010-run6/r4-3-effect-summary.json) 保留 `FAIL_OR_INCOMPLETE`。带电未准备 IDLE 段由操作者完成并报告正常，回垂臂确认后另行授权 `followprepare`，RAM 零点已建。准备起约 9.925 秒首次 READY，串口整数 `joint_mrad=357` 被主机换算为约 20.45°，不满足严格 `>20.5°` 的主机内侧门，脚本主动中止；该整数文本的截断不能证明固件内部精确角或实物角低于 20.5°。无约 25°长音、零目标／FOLLOW 测力、`followstart` 或非零补偿输出；只补发一次 `followstop` 并收到 `idle_submitted; observing_stop`，未证实 STOPPED。用户确认 24V 已关闭、机构静止承托，并表示无法准确手抬至 25°；随后明确希望研究“电机自动定位约 25°”。自动定位能力、适用模式和本机安全边界均待只读核查，不能因该请求直接当作已实现或可在当前镜像使用。此次是量化角反馈触发的**主机门中止**，不是已证实的固件故障或重力补偿效果失败；第六轮测力与轻推重定位均未执行。

**第六轮后测力方案更新（当时；仅主机离线准备）。**用户改选在固件 `20°–30°` 内能自然停稳的任一姿态，不再要求手抬准确到 25°；自动定位暂缓。显式 `--measure-only` 的主机入口采用宽的 `21°–27°` 停扶候选，运行时继续守 `20.5°–29.5°` 内侧余量。操作者在区间内停住并以同一量具点持续承托；主机以新鲜缓存反馈筛查约 1 秒姿态稳定，**长音表示已确认稳态、开始同姿态测力**，不是要求操作者在长音后才瞄准角度。随后约 2 秒 READY 零目标、约 3 秒 FOLLOW，同轮两段名义关节角差须 `≤0.5°`；力值及实物姿态仍由视频事后判读。早 READY 时可在零目标、闭环、模式和新鲜反馈持续成立下继续缓慢进入主机测力入口，但不得在贴近边界处开始两段比较。此次仅改主机测力流程，不改固件、已刷隔离镜像、`20 mNm` 命令帽、`15°/s` 速度门、10 秒会话或主动停止；新的量具读数和物理效果尚未取得。

**第七／八轮现场结果与取零纠正。**第七轮因操作者漏按 RESET，首次冷态读取仍是前轮 `phase=3/reader=7/seq=497`，脚本在 24V 上电前拒绝并留原始失败记录；补按 RESET 后另获许可的新目录第八轮冷态通过，带电未准备 IDLE 约 40.64 秒由操作者完成并报告正常。一次 `followprepare` 取 RAM 零点后，准备阶段名义角只在 `-13…0 mrad`，从未进 `READY`；主机在留足 2 秒基线与启动余量的期限中止，未发长音或 `followstart`，也没有净托力数据。补发 `followstop` 收 `read_cancel_pending`；操作者关闭 24V 后只读见 `phase=3/reader=3/output_allowed=0/fault=0`，不能当作带动力正常停止验收。用户澄清三短音后抬起的观察段，外部角标估计一直在 20°–30°，但未停住扶稳；随后确认**发送 `followprepare`、建立 RAM 零点时仍在抬**，因此本轮 RAM 零点未对应约定的垂臂静止姿态。软件站稳判据无法独立核实垂臂；相对角未进入安全区，因而未提交输出准备。零点转子位置比第五／六轮高约 0.58 转，与错误取零解释相容，但跨上电参考及取零瞬间的精确外部角度未验证。原始 [第七轮](../../../build/s4-r4-3-gain18-board-20261010/closeout20261010-run7/r4-3-effect-summary.json) 与 [第八轮](../../../build/s4-r4-3-gain18-board-20261010/closeout20261010-run8/r4-3-effect-summary.json) 均保留 `FAIL_OR_INCOMPLETE`。当前保持断电；下次须先回外部垂臂标记、完全停下并承托，经确认才取零，三短音后才前抬。S4 效果和轻推重定位仍未证实，固件与参数不变。

**第九／十轮现场结果与当前暂停点。**同一隔离镜像下，第九轮冷态通过，但上电确认后的首条驱动心跳为 `state=0/error=0/fault=0`，主机在 `followprepare` 前拒绝；单帧不能证明持续异常或移动所致。第十轮上电静止约 5 秒后 IDLE 正常，操作者完成约 30.79 秒带电未准备观察，确认回外部垂臂 `0°`、静止承托后取得 `2905 mrev` RAM 零点。三短音后曾短暂到 READY，反馈角从约 `21.54°` 上升；未等到稳态长音就转故障。断电后的同版 ELF 故障 RAM 确认 `FAULT/HEALTH/SAFETY_SPEED`，最后反馈约 `23.159°/17.939°/s`，超过 `15°/s` 速度门；`armed_ms=0/follow_session_used=0/target=0/applied=0`，未发送 `followstart`，两轮都没有量具对照或 S4 减负结论。操作者已确认 24V 关闭、机构静止承托且无明显异常。现有速度保护会在手动进入区间的准备／零目标 READY 阶段触发，已反复阻断测力；保持断电，不按同一条件重复上电，先离线核查分阶段保护与停扶交接，不能直接放宽门限或提高输出。原始证据见 [R4-3 现场记录](../../../docs/bench/s4-r4-3-20261009/README.md)与唯一开发日志。

**第十轮后主机测力门精简（当前仅离线）。**用户要求继续以区间同点扶持力对照收尾，主机仅对显式 `--measure-only` 改为在 RAM 垂臂零点后等待新鲜零目标 READY 落入主机 `20.5°–29.5°` 内侧，并核闭环心跳、模式 `1/1` 与零 `INPUT_TORQUE` 读回；不再额外等待主机 1 秒／4 样本或施加 `0.25°`／`2.5°/s` 门。长音表示有效 READY 后**停止移动、扶稳同一点**，不代表已经测到静止。后续约 2 秒 READY 基线仍需全段同点名义角跨度 `≤0.5°`、持续零目标／新鲜反馈，启动前再核最终 READY、闭环心跳与同声标姿态差 `≤0.5°`；FOLLOW 约 3 秒后主动 `followstop`。旧 `--run` 和非测力 `--closeout` 判据、固件 `20°–30°`／`15°/s`／`20 mNm`／10 秒硬限制均不变。主机 H 盘离线自检退出 0，独立只读审查无阻断性发现，未刷机、未重新上电或取得量具效果。第十轮 `SAFETY_SPEED` 在 `followstart` 前触发，这次主机修改不能单独消除；**当前保持断电，不按同一动力条件重试**。先离线明确准备期速度保护和停扶交接，再设计新的有界现场验证。参考索引第 25／28 行及 GALAXEA-A1Z／BetaRobot 固定版本源码仅提供分阶段监督思路，无本机可直接复用的交接规则；细节见 [R4-3 现场记录](../../../docs/bench/s4-r4-3-20261009/README.md)。

**待确认的固件交接方案（未实现、未获新的动力许可）。**把当前 `followprepare` 后自动在首次区间内站稳时调用 `OutputGateway_Prepare()` 的步骤，拆成“垂臂 RAM 取零和只读手动摆位”与“操作者停扶后显式提交零目标／力矩模式／闭环准备”两段。第一段网关继续 `SAFE_OFF`、不发电机控制帧，持续要求读链健康并设有限超时；第二段拟增加独立提交命令，须复查新鲜区间反馈、原 200 ms 站稳、模式／零目标与停止条件，提交后继续执行固件 `20°–30°` 和 `15°/s` 保护，`followstart` 仍只允许在零目标 READY。离线验证须覆盖摆位期间零控制帧、超时／故障／取消撤权、提交后速度故障及启动前模式读回。这个状态交接在已查的 ExoKit、GALAXEA-A1Z、BetaRobot、StateSmith 中没有可直接移植的实现；需要在本机自主设计，风险是模式／闭环切换时阻力或出力变化与超时交接错误。按仓库 `AGENTS.md` 2.1，需向用户说明后取得确认，才修改固件；当前不烧录或上电验证。

### 自动定位到 25° 的后续阶段（用户决定暂缓；仅保留只读核查）

用户随后决定暂不做自动定位；本节只保存已有资料核查与后续需解决的问题，当前不推进该功能。

用户希望电机只负责从已确认的垂臂起点有限地定位到约 25°，到位后仍按 S4 的**零目标→FOLLOW**测扶持力；定位期间及位置保持产生的力值不算 S4 重力补偿效果。当前隔离镜像只有力矩模式准备和输出链，不能自动定位。开源索引的 GIM6010-8 适配、关节角换算、通电保持、模式切换与限值条目提供查证入口；[SteadyWin GDS68 rev2.2 手册](https://www.steadywin.cn/col.jsp?id=124)记载位置模式、`Set_Input_Pos (0x00C)` 和轨迹速度／加减速命令，[ExoKit `ActiveJointMoveToReaction`](https://github.com/HCI-Lab-Saarland/ExoKit/blob/main/Arduino%20API/lib/exokit_api/src/event/reaction/ActiveJointMoveToReaction.cpp)可借鉴限速定位、到位和取消的动作语义。两者都不能证明本机 GDS68 固件 0.6.5 的具体参数、模式切换或带负载效果；ExoKit 使用 Dynamixel，不能照搬其退出时恢复旧模式的做法。

后续须先离线明确一个与 S4 FOLLOW 分开的 `0→约25°` 有限动作：核实起点、目标、零位、正方向及关节角到驱动转子位置的换算；为该动作独立规定并读回控制／输入模式、角度、速度、加速度和**驱动侧电流／力矩限制**，加上反馈新鲜度、watchdog、限位、超时、主动停止与失力时的人工承托。现有 `20 mNm` 是 S4 转子**力矩目标命令帽**，不约束位置控制器内部可能产生的转矩，不能沿用为自动定位的输出限值。到位后的“位置环退出→操作者和量具承托→驱动零力矩目标与模式实读→FOLLOW”交接还需单独设计和验证，特别是模式切换瞬间的失力与停止是否已被观察；资料没有提供可直接复用的无扰交接。以上是下一阶段调查与设计事项，当前 24V OFF，尚未实现、上电或批准新的动力试验。

| 顺序 | 一次窗口内的最少动作与证据 | 决策出口 |
|---|---|---|
| 先离线准备 | 固定现有隔离镜像及 `20°–30°`、转子目标帽 `20 mNm`、速度门 `15°/s`、会话 `10 s`；核对现有脚本能给姿态、目标、模式、心跳、停止和操作者观察留下可对齐的时间标记。标记同一肘姿态、负载、肩角及扶持点，并量扶持力臂。用户已确认可用约 `0.1 N` 分辨率的测力工具；先核其重复性，视频同时拍下读数、关节角标记并录入提示音。 | 不改增益、命令帽、角域、速度门或固件；准备好一次对照的记录方式，再申请具体现场窗口许可。理想力量级按 `20 mNm × 8 / 实测扶持力臂` 估算，不是实测输出或测力工具的固定验收阈值。 |
| 先查补偿前阻力 | 按用户当前选择，**不重复断电前抬**。同次冷启动开启 24V 后，在未准备的 IDLE 中确认新鲜心跳、无错误、`output_allowed=0`；操作者按自己的速度缓慢前抬、停稳读量具、回垂臂承托并确认。此段不发 `followobserve` 或 `followprepare`，视频仅供事后对齐；不能据此计算断电／带电差值。 | 若带电 IDLE 已明显变重或状态异常，立即停窗、保存状态和条件，不继续准备或提高输出；单次 IDLE 手感不能把原因定为电流环、位置环或摩擦。 |
| 再做同姿态净助力对照 | 上述状态无异常时，先让手臂回到外部垂臂标记、完全停下并保持承托；操作者确认这一实物姿态后才通过最后门执行本次 RAM 零点 `followprepare`，三短音后才再次前抬。操作者缓慢进入固件 `20°–30°` 区间，在可自然扶稳的位置停住。主机只在 `21°–27°` 入口候选及运行时 `20.5°–29.5°` 内侧余量中筛查约 1 秒新鲜缓存姿态稳定；固件提前 READY 时须持续验证新鲜零目标、`control_mode/input_mode=1/1` 与闭环心跳。**长音确认已站稳**，随后同点记录约 2 秒 READY 零目标扶持力；一次 `followstart` 后在同姿态、同负载、同扶持点记录约 3 秒补偿段，主机筛查两段名义关节角差 `≤0.5°`，并记录反馈、`INPUT_TORQUE` 目标、模式和异常运动，随后主动停止，不做轻推。 | 视频事后判读实物角度与量具稳态力值：扶持力下降须超过两段稳态波动，且至少跨两个 `0.1 N` 刻度，运行和停止也须正常，才记“可辨的定量减负”。主机角度筛查不能代替视频和测力；`INPUT_TORQUE` 只是目标，不能代替实测托力或 Kt。若无可辨差异，保持“效果未证实”，先查实际电流（若 FW0.6.5 支持读取）、传动／装夹及测力几何，再讨论参数。 |
| 最后核正常停止；轻推另轮 | 本轮约 3 秒 FOLLOW 测力后主动执行 `followstop`，核渐撤、停止终态与新鲜 IDLE 心跳，关闭 24V 并承托；异常时立即停止。原计划的小范围轻推重定位**本轮不执行，保持未测**，另行决定是否需要下一现场窗口。测力值留待视频事后判读，不要求操作者在出力中口报。 | 只有同姿态扶持力对照与正常停止得到证据，才能判本轮定量效果；不因省去轻推而称需求蓝图 P0.2 的“可重定位并恢复支撑”已通过，也不恢复旧两姿态或 `0.5+5 s` 数值门槛。 |

**测力几何。**`--support-lever-arm-mm` 记录肩轴到量具施力作用线的垂距，而不是仅记录肩轴到抓握点的直线距离；测力方向与接触点也应在视频中保持一致。脚本不把该长度或力矩目标换算为实测托力。

**分支与边界。**若补偿前阻力、模式／闭环读回、净助力或重定位任何一项失败，先保留原始复现并查根因；不为了完成计划连续重试或盲目调参。若需要多轮参数整定，再单独设计 RAM 内运行时增益：安全确认位不可经 CLI 修改，独立 `20 mNm` 帽与区间仍由编译期冻结，增益必须经完整区间端点预算、斜率和状态转换检查。当前增益 `0.18` 在 30°的理想目标已约 `18.45 mNm`；`0–0.5` 不能直接作为现有配置的 CLI 范围。周期 `0x029` 已有非零帧，实时曲线可作为主机侧读数显示优化，两者均非本轮效果判定前置。旧的运行中人工切 24V 试验按用户决定保持未测；现有正常停止证据不外推为失联或物理断电验证。若日后越过 `20 mNm` 或扩至水平位，先单独审查机械余量、独立停止、驱动 watchdog 的本机实效与供电保护，再确定新范围和当次授权。

**水平位标定另列后续。**当前隔离镜像只覆盖 20°–30°，不在本次收尾去 90°“刚好托起”。一次上行起动阈值混合重力、静摩擦、传动损失和驱动响应，不能同时独立求出重力幅值、Kt 与摩擦死区；若以后要辨识这些量，应设计不同姿态、上／下行以及外力和实际电流的对照。制订本计划时未执行新上电、手推、准备、输出、编译、烧录或固件修改；后续第四、第五轮现场结果已在本节开头增补。任何新的真实 `followprepare`／`followstart` 前仍需给出具体镜像、限值、时长和停止办法，取得当次许可。

## 2026-10-10｜R4-3 当前范围（覆盖下方旧效果门槛）

2026-10-09 第二次 15°/s 低姿态短窗已记录约 22°处准备、非零目标命令和新鲜反馈并行、主动停止、渐撤与停后新鲜 IDLE 心跳。用户明确选择“低姿态基础联调通过，效果另测”；据此仅将**低姿态基础联调**记为通过，保留脚本原始 `LOW_ACTIVE_OBSERVED_PENDING_OPERATOR`，不把目标力矩或相近角度说成实测减负。操作者事后说“好像没有扶稳”，故当轮效果未证实。2026-10-10 再做一次约 25°同姿态零目标／补偿对照：反馈、非零目标与正常停止链记录完整，用户答“没感觉”；未测扶持力或机械转矩，**S4 效果验收仍未完成**。本次试后用户确认 24V 已关闭、机构承托且静止无异常；详细证据见 [R4-3 现场记录](../../../docs/bench/s4-r4-3-20261009/README.md)。

当前效果验证简化为同一可重复姿态、固定肘姿态和负载、相同扶持位置的零目标／补偿开启对照，记录扶持力或明确手感，并确认反馈新鲜、无自行加速／明显振荡／触边、正常停止。原两姿态和松手后 `0.5+5 s` 的 `0.5°/0.75°` 漂移门槛不再用于当前个人原型验收；它们未执行，不写通过。这个调整符合早期原型先观察运行和控制效果、再量化减负的分层做法：有源上肢原型论文先观察角度／速度与本体重力补偿（[Hussain 等，2023](https://arxiv.org/html/2309.04698v1#S7.SS2)），工具支撑原型在台架用端部力传感器比较支撑力（[Hull 等，2022](https://vtechworks.lib.vt.edu/bitstreams/33d4a698-6caa-49a9-b610-531612213313/download)）；文献不替代本机实测。当前 `20 mNm` 转子命令帽按名义 8:1 仅为理想 `0.16 Nm` 关节侧上限，不能用接近定角来要求这轮部分补偿。

## 2026-10-09｜剩余执行合同（以下 R4-3 旧门槛由上方更新覆盖）

**2026-10-09 最新 R4-2B 执行结果：**用户重新要求进行短时持续出力与停止。沿同一 SHA256 `c3a1c620…e763` 隔离镜像，第七次冷启动在 `393 mrad` 新鲜 READY 后执行 `followstart`，观察到命令出力与七属性读取并行、自然到期渐撤、`phase=3/reader=7/stop=1/fault=0` 和读链停止观察；原主机脚本因 S4 下不适用的 `s3reader hvalid=1` 检查误报 `FAIL`，保留原始结果并依据源码及原始回报单独判读。第八次冷启动在 `402 mrad` READY 后启动，约 1.072 s 时独立 `followstop`，得到渐撤、同样的正常停止终态、`observed=1` 和年龄 107 ms 的新鲜 IDLE 心跳；驱动 `INPUT_TORQUE` 目标读回约 `0.00724 Nm`，不是机械力矩实测。两次肩侧 CAN 错误均为 0，用户均确认随后 24V 关闭、机构静止无异常。原始证据和边界见[现场记录](../../../docs/bench/s4-first-window-20261009/README.md#2026-10-09-r4-2b-短时持续输出与停止)及唯一开发日志。正常到期和主动停止短窗已有证据；原计划的运行中人工切断 24V 短会话由用户明确决定跳过，保留未测，不再作为本次 S4 台架范围的额外门槛。真实力矩／减负、R4-3 两姿态与重定位漂移未测，S4 尚未完成。以下早期段落中的“未开始／不继续”只描述各自记录时点。

当前 active task 仍为 `s4-shoulder-follow-plan` / `EXECUTE` / `T-s4-shoulder-follow-plan`。R0–R3 的离线实现和检查已完成；R4-0 只得到 B≈+79.5° 的粗量级参考，A′ 差异未解释、映射未验收；R4-1 修复版在一次 8 秒只读窗口取得 404 组完整七属性和正常停止，未证明读发并行或长期 CAN 可靠性。2026-10-08 又完成一次旧 S3 镜像的 +20 mNm／100 ms 方向候选诊断并恢复输出禁用的 S4 只读镜像；可选 RAM 会话垂臂零点路径当时只通过离线测试；10 月 9 日三轮 `followprepare` 已在板上建立本次 RAM 零点。当前生产 `enabled_axes=0`、`output_enabled=false`、`session_zero_on_prepare=false`，带动力 FOLLOW 尚未开始。以上状态以唯一开发日志和当前源码为准，不重复已完成的 30 秒只读采集或旧微脉冲。

2026-10-09 首窗现场更新：用户对具体 BIN `4210a776…cdc4e8`、20 mNm 命令帽、增益 0.09、20°–30° 和 3 秒窗口作了当次许可；24V 关闭时备份、刷写及独立读回已完成。同镜像的三次冷启动均在 `followprepare` 后停下：第一、第三次未复位网关 RAM 给出 `OUTPUT_FAULT/HEALTH/SAFETY_SPEED`，第二次用户未手推，零点后角度保持 0°，故未 READY。三次均未 `followstart`、未见持续非零力矩命令；第一、第三次位置变化均为用户手动带动机构。用户报告三次都未听到 MotorWizard 启动声；该感知不能单独判定驱动器内部状态，第三次 ClosedLoop 是否已提交仍未知。R4-2B 的持续出力、正常渐撤、物理切断和 R4-3 均未验收；具体证据见唯一开发日志与首窗 `verification.json`。

2026-10-09 准备时机修订（当时仅离线）：本机首次合格的区间反馈会立即触发 `OutputGateway_Prepare`，而后续手推反馈又触发准备阶段的 `SAFETY_SPEED`。先用主机轨迹复现了移动中已离开 `SAFE_OFF`，再复用独立的 `ShoulderStandstill`，要求区间内新鲜反馈连续站稳既有 200 ms 候选驻留才开始零目标／模式／闭环准备；不放宽 10°/s 保护。修复后的隔离 HSE BIN 为 113676 B、SHA256 `c3a1c6205d8d2695c1d116d3ef872d9885b880c5f7fd1ef421fde102deb0e763`，保存在 `build/s4-entry-dwell-20261009/`。**该离线修订结束时**尚未刷写或获这份新镜像的现场许可，板上仍是旧试验镜像；这些是当时状态，不代表后续六次现场会话的当前状态。活动 site 仍输出禁用，软件通过不等于 S4 出力验收。

2026-10-09 六次新镜像准备会话更新：此后新 BIN 获当次现场许可，在 24V 关闭下刷写并独立读回一致。前五次分别停在未手推、速度保护、越过候选上限或 READY 后主机分步超期等准备阶段；第六次 `followprepare` 成功建立 RAM 垂臂零点，用户手动前屈，连续 89 条新鲜反馈为固件 `phase=6` READY，约 22.5°–22.8°位于 20°–30°软件小区间。主机脚本自设的 401 mrad（约 23°）较窄接纳下界未通过，故没有启动脚本调用。**依据原始记录，第六轮垂臂取零、手动入小区间和固件 READY 的准备测试通过**；用户表示“这个测试已经通过了”并要求停止后续测试，具体通过范围由上述证据界定。六次均未 `followstart` 或持续非零力矩；R4-2B 的持续出力、正常撤力／物理切断和 R4-3 台架验收仍未验证，不能记为通过。板上为新隔离试验镜像、活动 site 仍输出禁用；用户追问后确认当前 24V 已断开、机构承托。下方 R4-2B/R4-3 是原计划，现按用户要求不继续执行。

| 剩余轮次 | 执行和现场事实门 | 出口与停止条件 |
|---|---|---|
| R4-2A 证据与专用镜像准备 | 用户决定沿用 ExoKit 结构的设计角域，不再增加独立角度与同期转子配对测试。以自然垂臂为 RAM 会话零点、前屈为正，ExoKit 的约 −20°～+115°只作原设计参考；本机垂臂不能后伸、未装可选限位件，首窗只能考虑前屈内部的小区间。厂家标称 GIM6010-8 减速比 8:1、用户确认输出端直连肩轴、已有 A→B 转子增加和 +20 mNm／100 ms 微脉冲转子增加，构成名义比例与方向候选；旧 A 只定义当次位置的相对原点，未确认为垂臂零点；B≈+79.5°不能当作垂臂角或映射验收。首窗候选定为垂臂 RAM 零点后前屈 20°–30°：用户在包含两端目视停机余量、24V 切断可及性的合并提问下答“是，20°–30°可用”；这只是现场口述可行性，不是独立量角、实际停距或切断效能测量，也不凭 ExoKit 上界宣称实物止挡。1.64 Nm **关节侧部分负载粗估**只作首版正弦模型暂定幅值；垂臂 RAM 会话零点为相位参考。首轮转子侧 `max_motor_command ≤ 0.020 Nm`，且 `assist_gain ≤ min(1, 0.020 × rotor_per_joint / 1.64)`；名义比例为 8 时上式约为 0.098，`assist_gain=0.09` 仅是离线算例。用已有只读数据定读链时序候选；取零间隔 `session_zero_dwell_ms` 与停止后 `shoulder_standstill.dwell_ms` 分开，前者小于反馈期限。发送间隔、正常撤力斜率、零 Nm／IDLE 提交预算与会话期限逐项注明来源和未验证部分。 | 名义比例、方向趋势或 ExoKit 设计角域均不能单独使本机 `mapping.confirmed`、`limits_confirmed` 等输出确认位为 true；活动 site 保持输出禁用。旧隔离候选在首轮具体许可后刷写并经历三次准备尝试；站稳修订候选随后也已刷写并经历六次准备会话，其中第六次固件 READY 准备测试依据原始记录通过。用户要求停止后续测试，带动力 FOLLOW 尚未开始；不能因用户口述区间可用便将实测停距、读发或物理切断记为通过。离线候选及相关主机、生成配置和目标构建检查只能证明软件自洽；实际出力前另核具体镜像、限值、窗口并取得当次许可。 |
| R4-2B 有界持续输出和停止 | 仅在修订镜像完成离线核查并取得新的当次许可后，沿用已验证的板上 HSE 时钟方案；24V 关闭时先备份、烧录、独立回读并核对镜像身份，USB-only 确认开机 `SAFE_OFF`。在垂臂位显式 `followprepare`，用两组请求之后完整新鲜七属性建立本次 RAM 零点；网关仍 `SAFE_OFF`，手动移到小区间内部约 25°并保持，取得连续新鲜的 200 ms 站稳反馈后才允许零目标准备，再由独立 `followstart` 开始首个 ≤20 mNm 的短时持续出力。记录读发并行、转子与关节趋势、命令侧扭矩、零 Nm TXOK／排空后 IDLE 的 HAL 入队、后续新鲜 IDLE 心跳及有限尾窗读链；不能把 HAL 入队写成 IDLE TXOK，停后 `followangle` 不提供新鲜角度；正常 `followstop` 与到期分别在冷启动会话中验证，集中同一台架窗口完成。原计划的运行中人工 24V 切断短会话由用户明确决定跳过，保留未测；不能据此宣称 CAN 断线会自动停止。 | 方向矛盾、无足够余量、自行加速、明显振荡、反馈／CAN 异常或撤力与切断不符时立即结束本轮、保留原始证据并查根因，不自动加大参数或重试。每次实际电机出力前先给用户具体镜像、限值及窗口并取得当次许可。 |
| R4-3 FOLLOW 效果对照 | 已有低姿态基础联调有界证据。后续固定同一肘姿态、负载和扶持位置，在一个可重复肩姿态比较零目标与补偿开启的扶持力或明确手感；记录新鲜反馈、活动范围、是否自走/振荡/触边，以及正常停止。 | 低姿态基础联调通过；只有可比对照显示减轻扶持努力，且运行和停止正常，才标记当前 S4 效果验收完成。若没有可辨差异或扶持条件变化，效果记未证实；不要求两姿态或原 `0.5+5 s` 漂移阈值。运行中人工切断 24V 按用户决定跳过并保留未测，不因效果门槛调整而提高力矩或速度。 |

接口维持现有 `S4SiteProfile`、`OutputGateway` 及 `followprepare/start/stop/status/angle`；本轮不新建控制出口、不切换 MIT。隔离试验 profile 中预置为 true 的确认位，只表示厂家资料、用户口述和离线预算已被用于一次受监督首窗试验假设；它们不声称映射、读发并行、停止预算或物理卸力已经实测。活动 site 仍保持输出禁用。每轮实质结果只追加到 `Main_ project/docs/development_schedule.md`，保护当前未提交修改；正常取消的命令侧渐撤、故障硬撤和真实物理卸力分别记录。最终可重复台架镜像即使准许编译期输出，上电仍须默认 `SAFE_OFF` 且每冷启动人工许可；人体穿戴不属于 S4 验收。

2026-10-09 证据与离线门控更新（早于下述区间确认）：用户口述确认 GDS68 输出端与肩轴直接相连、无额外传动；舒适内侧位置的独立角度、同期新鲜转子读数及小区间两端停机余量仍暂无，因此 8:1 和关节正向仍是候选，不能填写输出确认位。既有 R4-1 的 404 组七属性在抽样段平均约 20.002 ms／组，但没有全程最慢组间隔；6.743 ms 只读取消也不是出力撤力时间。当前记录不能确认读发并行、发送间隔、零 Nm／IDLE 提交预算或物理卸力。离线配置门已增加模型峰值与独立电机命令上限的比较，以及独立的 `session_zero_dwell_ms` 必须小于反馈期限；这只是参数自洽，不能代替现场证据。若总比最终确为 8，试验候选 `assist_gain=0.09` 的正弦峰值为 18.45 mNm，仍须由真实映射和小区间依据审查，未写入生产 site。

2026-10-09 ExoKit 结构依据与用户决定：用户确认本机沿用 ExoKit 结构，并接受以其设计角域作为 S4 离线参考，不做新增 A/B 量角配对。ExoKit [手册 V1](https://github.com/HCI-Lab-Saarland/ExoKit/blob/main/ExoKit%20Manual%20V1.pdf)的肩侧示意为自然垂臂约 0°、后伸约 −20°、前屈约 +115°；[ArmBuilder.cpp](https://github.com/HCI-Lab-Saarland/ExoKit/blob/main/Arduino%20API/lib/exokit_api/src/exo/builder/ArmBuilder.cpp#L485-L519)填写肩侧 20/115 范围，[ExoskeletonJointActiveHandle.cpp](https://github.com/HCI-Lab-Saarland/ExoKit/blob/main/Arduino%20API/lib/exokit_api/src/exo/ExoskeletonJointActiveHandle.cpp)将 lower 用于负向边界。按现有文件比对，本机 04–10 号打印件与 ExoKit 对应单件的计算体积逐件相同；本机没有装可选 `restriction_*` 限位插件，电机及安装适配也不同，故这不是当前装配的实测行程或硬止挡。ExoKit [Calibrator](https://github.com/HCI-Lab-Saarland/ExoKit/blob/main/Arduino%20API/lib/exokit_api/src/exo/builder/ExoskeletonCalibrator.cpp#L849-L870)把最大后伸摆位作为 tare 零点，与本机垂臂会话零点不相同，不移植该零位。厂家 [GIM6010-8 产品资料](https://www.steadywin.cn/en/pd.jsp?fromColId=0&id=116)标称减速比 8:1；与直连口述和旧转子趋势合用，可写离线候选换算 `Δ关节角[°]≈45×Δ转子圈数`，不能据此填本机输出门确认位。旧 A→B 的 +1.765707 圈换算约 +79.46°，A 未确认为垂臂零点；随后用户口述确认前屈 20°–30° 可用；实际停距仍未量得。

按名义 8:1 作理想无损换算，20 mNm 转子命令仅对应关节侧 0.16 Nm，约为 1.64 Nm 粗估峰值的 9.8%；`assist_gain=0.09` 时模型转子峰值为 18.45 mNm。二者均是**命令与模型算例**，不是实际输出力矩或可感减负的预测。`followprepare` 也会发送零目标、模式和闭环请求；真实板上 prepare/start 均属于须先说明镜像、限值、动作窗口并取得当次许可的步骤。

| 活动输出禁用 site 的离线依据 | 数值与依据 | 当前结论 |
|---|---|---|
| `rotor_per_joint`、`direction` | 名义 `8`、前屈候选 `+1`；厂家减速比、用户直连口述、旧 A→B 与正微脉冲趋势。 | 可用于换算算例；`mapping.confirmed=false`。 |
| 重力幅值、相位、符号 | `amplitude_nm=1.64` 关节侧部分负载粗估；垂臂 RAM 零点时 `gravity_zero_rad=0`、前屈助力候选 `sign_g=+1`。 | 可用于离线模型；`gravity.confirmed=false`。 |
| 首轮助力与独立命令帽 | 名义 8:1 下 `assist_gain=0.09` 对应峰值 18.45 mNm；候选转子命令帽 20 mNm。 | 仅计算自洽，不是已实测持续力矩或效果。 |
| 已有七属性只读时序 | 当前只读 site 的 reply/period 为 20/20 ms、反馈期限 150 ms、会话窗 30000 ms；R4-1 曾在 8 秒内完成 404 组。 | 保留为读链候选；不据此确认读发并行最坏值、发送周期或停止预算。 |
| 本机小区间、速度、发送与停止 | 用户对合并提问确认垂臂零点后前屈 20°–30° 可用；两端目视余量及 24V 切断可及性仅为同次口述条件。停止距离、零 Nm／IDLE、读发并行与独立切断效能尚无实测预算。 | 20°–30° 是首窗离线候选；`limits_confirmed=false`，相关时序／保护／停止确认位保持 false；`enabled_axes=0`、`output_enabled=false`。 |

2026-10-09 停止预算配置门补充：离线回归先复现了 `stop_submit_budget_ms < service_limit_ms` 仍能通过 `S4Site_ProfileReady()` 的缺口；现要求提交预算至少覆盖前一零 Nm 帧允许占用的完整服务期限，随后才可排队 IDLE。这个下界仅是配置自洽的必要条件，未包含轮询抖动、后续 IDLE 实际提交耗时或电机物理卸力；读发并行及完整停止预算仍待专用短窗验证。

### 2026-10-09 隔离 HSE 首窗候选与后续现场结果

专用候选源位于 `firmware/Exoskeleton_F446/docs/bench/s4-first-window-20261009/`；它将肩侧 site 初始化和已验证过的 HSE_BYPASS 时钟配置，仅叠加到 H 盘 `firmware/Exoskeleton_F446/build/s4-first-window-final-20261009/` 的独立构建副本。活动 `App/application/s4_site.c` 仍为 `enabled_axes=0`、`output_enabled=false`、`session_zero_on_prepare=false`。隔离候选中的输出许可、映射、重力、保护、时序、停止与限位确认位是**一次有人值守、可人工断 24V 的短窗试验假设**：来源分别为厂家名义参数、用户直连及区间口述、旧方向趋势、离线读链和保守工程预算；不是上述事实的实测验收。冷启动的 `rotor_reference_confirmed[0]` 仍为 false，只有本次垂臂取零成功后才写入 RAM。

| 隔离候选字段 | 首窗暂定值与依据 |
|---|---|
| 姿态、模型和命令 | 垂臂 RAM 零点后前屈 +20°～+30°；厂家名义 `rotor_per_joint=8`、方向候选 `+1`；关节侧部分负载粗估 `amplitude_nm=1.64`、`gravity_zero_rad=0`、`sign_g=+1`、`assist_gain=0.09`。理想模型转子峰值 18.45 mNm；独立转子命令上限 `0.020 Nm`、增力斜率 `0.020 Nm/s`、关节速度上限候选 10°/s。 |
| 反馈、发送和请求 | 七属性 reply/period 为 20/20 ms，反馈期限 150 ms；候选发送最小间隔 50 ms、发送服务期限 100 ms、单请求期限 250 ms。只读均值与代码约束支持离线算术，不证明持续输出时读发并行的最坏延迟。 |
| 会话、撤力和取零 | `prepare_timeout_ms=20000`；`session_ms=3000`、`ramp_max_ms=1000`、`stop_submit_budget_ms=250`，正常转子撤力斜率 `0.040 Nm/s`，停止后读链尾窗 1500 ms；两次新鲜完整反馈的垂臂取零间隔至少 20 ms。停止后站稳取 200 ms／反馈新鲜度 150 ms，与取零间隔分开。 |
| 独立构建证据 | 静态配置门主机 probe 通过；隔离 HSE 新源构建退出 0、0 编译警告／0 CMake 提示。BIN 113500 B、SHA256 `4210a776d6440e601f972acce30a7859bb9080760709e7a739b92f1baecdc4e8`；结果与输入清单在上述 H 盘副本的 `build/Debug/` 和 `source-manifest.json`。本行记录离线生成时的状态；随后用户给出当次许可，刷写与独立读回一致，但三轮都未进入 FOLLOW，详见本页首窗现场更新。 |

首窗按**单次冷启动直接 `followprepare`**取得两组请求后的新鲜七属性与 RAM 垂臂零点；不能先运行 `followobserve`，因为本次冷启动的 reader 所有权会被消耗。手动移入 +20°～+30° 后用新的完整反馈再允许零目标准备，单独 `followstart` 才进入 3 秒持续输出。该 3 秒只用于读发、方向和撤力短窗；不足以完成松手缓冲 0.5 秒加连续 5 秒的 R4-3 重定位验收。正常停机只把 IDLE 的 HAL 入队与随后新鲜 IDLE 心跳、读链停止分开记录；`followangle` 停后不报新鲜角度。任何真实板上 `followprepare` 或 `followstart` 前须先给用户上述候选镜像身份、限值和动作窗口并取得当次许可；这段是首窗前流程；候选后来已刷入并在准备阶段触发故障，后续须先查明 SPEED 与准备帧交付，再获得新的当次许可，不能凭旧许可继续尝试。

日期：2026-10-07（Asia/Shanghai）｜auto-embedded 阶段：`EXECUTE`｜trace_id：`T-s4-shoulder-follow-plan`｜2026-10-07 用户要求开始执行；本轮先做 R0/R1 默认禁用输出的离线工作。研究依据和路线比较分别见同任务 `research.md`、`innovate.md`；阶段状态仅以 `Main_ project/docs/development_schedule.md` 为准。

执行更新（2026-10-07，当时状态）：R0–R3 已完成离线实现、主机回归与 WindowsDebug 构建，当时停在 R4-0 首次只读实物测量前；本页以下 R4-0/R4-1 测量和只读步骤仅供追溯、不再执行；以页首剩余合同为准。原计划文本保留，`edits.md` 是截至当时的历史编辑清单，后续实际结果以唯一开发日志为准。生产输出保持禁用，当轮未刷写、读取或操作实物。历史 R4-0 现场分工与操作顺序见 `r4_user_cooperation_checklist.md`，空白记录表见 `r4_readonly_measurement_template.md`。

R4-0 现场协作修订（2026-10-07，当时方案）：用户希望用量角尺**核对我从转子读数计算的角度**，不在每次穿戴时重做完整三点测角。首次台架仍需固定物理参考 A（定义相对 0°）、一个非零 B 的独立量角核对值和 A′ 返回检查，才能判断方向与总传动比例；8:1 仅作驱动器候选换算。当时拟研究在 `SAFE_OFF` 对齐同一 A 后建立本次会话的软件转子偏移；2026-10-08 已完成可选 RAM 会话零点的离线实现，但本机比例、方向、重力关系和范围仍待确认，尚未上板。FW0.6.5 断电后转子多圈位置可否恢复、佩戴对齐及人体带动力安全均未验证，不能据此声称现已“穿上即用”。

R4-0 当前姿态补充（2026-10-07）：用户选择**测试时当前位置为本次相对 A=0°**，随后做小幅 B、回 A′；应先在固定件/活动件留下 A 对位标记，首次采集到的新鲜 `r_A` 才作为本轮原点。该选择只改变本次相对角坐标，不替代重力模型零位、机械范围证据或以后每次穿戴都可复现的固定基准。

## 1. 目标与完成定义

**目标**：右肩侧在固定、承托且已知的肘姿态下，依新鲜关节角生成随姿态变化的重力补偿，经唯一 `OutputGateway` 送 GDS68；人手可在一个已确认的小区间内推到另一姿态，停下后仍有支撑。开机默认为 `SAFE_OFF`，只有当次人工准备、许可和有效数据才进入 `FOLLOW`。本项目阶段验收只针对台架，不描述人体或临床效果。

**可观察完成标准**：

1. 离线：同一关节坐标的模型输出符号、单位、零位和峰值均可解释；S4 正常取消与期限到来按有界斜坡撤至零再发 IDLE，故障立即撤权并尝试 IDLE；所有路径不重放旧请求。非有限参数/反馈、越界、过速、CAN 发送失败、读事务异常均按故障处理；S3 单次脉冲的原停止语义回归不变；WindowsDebug 目标构建成功并检查新增警告。
2. 上板只读：先以已有 EncoderStream/原始位置读数记录两个承托姿态；映射证据齐备后，实际 `JointState` 与姿态变化一致，S4 计算值只显示、不发送。同一运行窗口记录属性快照序列/年龄、CAN 错误增量和预计输出符号；出现异常即回 RESEARCH 查因。
3. 有界出力：在肘部固定、肩侧小区间、人工可切断 24V 的受监督窗口，确认从零缓升、正常取消/到期按期限内斜坡撤力、故障立即撤权，没有持续自行运动或明显振荡。到期前给操作者一次明确的扶稳提示；提示排队失败不得延后撤力。旧 20 mNm/100 ms 单次微动只给首个 S4 窗口的**转矩幅值**上界参考；S4 总时长须容纳完整撤力/IDLE，不机械沿用 100 ms，也不自动扩成长期许可。
4. S4 当前效果验收：保留已通过的低姿态基础联调结论；固定肘姿态、负载及扶持位置，在一个可重复肩姿态比较零目标与补偿开启时的扶持力或明确手感。只有同条件显示扶持努力减小，同时反馈新鲜、保持在小区间且无自行加速／明显振荡／触边、能正常停止，才写效果通过。原两姿态和松手后 `0.5+5 s` 的 `0.5°/0.75°` 漂移要求不作为此阶段硬门槛；没有有效对照则只写基础联调通过、效果未证实。人体减负及全行程不属于此阶段。

**范围外**：肘部出力/双关节、Lock、Amplify、IMU/HX711、Flash 零位持久化、MIT 模式切换、改时钟/针脚/接线、自动上电使能、人体实验。S3 先前被用户跳过的全测角/再次动作/异常停止追加验收不恢复为 S3 门槛；S4 新持续出力需要的最少事实另行取得。

## 2. 冻结的技术路线与接口合同

- 先用现有 SIMPLE 转矩编码，GDS68 指令入口为转子侧 Nm。`GravityCompensation_Compute()` 只输出关节侧 Nm；应用层按已确认的 `rotor_per_joint` 和本机确定的 `assist_gain` 转为正关节方向的转子侧 Nm，唯一出口再按 `JointMapping.direction` 编码电机符号。位置 `rad`、速度 `rad/s`、力矩 `Nm` 全程不混用；模型零位不是旧的单点 rev。
- 固定肘姿态首版使用一个关节的静态正弦候选 `tau_joint(q)=sign_g × amplitude_nm × sin(q−gravity_zero_rad)`。按本机几何手核符号/维度并由主机数值检查；配置约束 `amplitude_nm>0`、`sign_g∈{−1,+1}`、`0≤assist_gain≤1`，再由独立电机侧限制裁剪。`amplitude_nm`、`gravity_zero_rad`、`sign_g`、`assist_gain` 和关节映射均有来源记录，否则 `S4Site_ProfileReady()` 为假、输出始终禁用。只读模型观察可由更严格的反馈/映射核查 `S4Site_ObservationReady()` 开启，绝不绑定输出。
- `CanMonitor` 继续只在主循环分发帧；复用七属性 reader 的请求/回复关联、完整快照及失效锁存。S4 一次冷启动只占有一次 reader 会话，主动停止/超时后不可再启动；S3、诊断和 encoder stream 不得同时启动。因此 `followobserve` 与后续带动力试验需分开的冷启动和镜像，连续两次带动力试验也须人工检查、冷启动、重新 Prepare/Arm。S4 site 另有编译期 `output_enabled`，读/计算观察时保持 false，不能由 CLI 改真。`OutputGateway` 仍是唯一电机出力/撤权入口；模型、CLI 和 reader 不直接提交转矩 CAN 帧。
- S4 独立 stop policy 只允许肩轴；它要求显式零目标/扭矩模式/闭环准备及新鲜反馈后人工许可，允许**同一有限会话中逐个新完整样本刷新目标**，而 S3 受监督策略仍限一次请求。`OutputGateway_Step()` 的 S4 分支必须按最小发送间隔节流：仅对新序列或仍新鲜样本所需的有界斜率步进发帧，不在每个 1 ms 服务 tick 重发旧目标；读邮箱与输出邮箱互不抢占，忙/错误/饥饿有明确撤权结果。逐样本 TTL、调度健康和整段会话期限并存；故障/断言失败锁存，冷启动及人工重新许可，不自动重启。
- **正常撤力**：`followstop`/S4 会话 `cancel` 立即撤销新目标许可并冻结最后被传输层接受的力矩命令 `applied`，但网关保留仅能递减至零的短时停机授权，进入 `OUTPUT_RAMPING_DOWN`。按有界的转子侧撤力斜率每个发送周期减小绝对力矩；传输层接受零 Nm 帧后再清旧帧、提交 IDLE 并进入既有限读尾窗。整段会话硬截止 `D` 是零力/IDLE 提交的最晚时间，最迟在 `D−ramp_max_ms` 开始斜坡；人工取消从当下起最多 `ramp_max_ms` 完成。斜坡期间仍须新鲜反馈、健康调度/CAN，禁止新的模型目标、许可或第二次会话；任何故障或无法在硬截止前完成，立即走故障硬撤权，不延长输出时间。命令被接受不等于电机已按该斜率实际卸力，首次带动力短窗须观察实际卸力感和新鲜反馈。
- **到期提示**：`followstart` 成功时总回复 ASCII `OK FOLLOW started; expires_in_ms=<N>; ramp_in_ms=<M>; support arm before ramp\r\n`，其中 `N=session_ms`、`M=session_ms−ramp_max_ms`；短会话（`M≤2000`）以这条即时回复充当到期提醒。长会话在计划撤力开始前 2 s，即 `D−ramp_max_ms−2000`，通过现有非阻塞 UART 尝试一次 `WARN FOLLOW ending soon; support arm; torque will ramp down\r\n`；实现时先用有界历时比较判断长短会话，避免无符号时间减法下溢/回绕误触发。主动 `followstop`/S4 `cancel` 开始正常撤力时回复 `OK FOLLOW ramping_down; support arm\r\n`；若已触发故障硬撤，则不得回复正在斜坡撤力。`followstatus` 报告 `remaining_ms`、`ramp_in_ms` 或 `ramp_remaining_ms`、撤力状态与命令侧是否仍可能出力。提示只作为操作提醒，入队成功不等于操作者收到；队列繁忙时有界重试并记录未排队，绝不阻塞控制或推迟撤力。串口本身故障按现有安全路径硬撤权。
- **故障撤力**：反馈、范围、调度、CAN 或 UART 故障立即撤销许可/旧候选、清正常邮箱并尝试 IDLE，不经过斜坡；CAN 停止帧可能因断线到不了驱动器，首版带动力窗口采用用户选定的现场可人工切断 24V 作为独立手段。FW0.6.5 watchdog 仅在读到配置并验证适用行为后可作为断线停止依据，不按 schema 字段存在就宣称生效。

## 3. 候选参数来源与出力前核对

| 必须冻结的量 | 来源、核对方法和记录 | 未取得时的代码行为 |
|---|---|---|
| 重力参考姿态 `gravity_zero_rad`、关节正方向和 `rotor_per_joint` | 沿用本机自然垂臂的 RAM 会话零点；不采用 ExoKit 最大后伸 tare。厂家名义 8:1 与用户确认输出直连给比例候选，旧 A→B 与单次微脉冲给前屈正向候选；A 仅是当次位置的相对原点，未确认为垂臂零点；不能把 B≈+79.5°当作相对垂臂的实测角。用户已取消新增独立角度／同期转子配对测试。记录候选来源及其与本机实际零位、机械余量的区别；不把 2.8982579708099365 rev 当零位。 | 只有设计或标称依据而无足够本机依据时，`mapping.confirmed=false`、S4 site 不许可。 |
| 安全小区间、速度和转矩上限 | ExoKit −20°～+115°只作原设计角域；本机垂臂不能后伸，首窗候选限定于前屈方向的内部小段。实际两端停机余量、线束余量和速度×停止响应距离仍需有本机适用依据；旧 B≈+79.5°不属于垂臂坐标，不据此选试验边界。转矩上限沿用已观察的单次 +20 mNm 幅值作为首轮独立命令上限候选，不能取额定、堵转或 SDK 量程作运行限制。 | 范围/速度/转矩确认位 false，模型只可离线算。 |
| 模型幅值、符号、助力比例 | 用户于2026-10-09接受以约1.64 Nm关节侧部分负载粗估作为首版模型的**暂定幅值**，不另要求本轮称重；它不是真实峰值或安全上限。固定肘/负载，以垂臂 RAM 零点作为候选相位，先核关节／电机正方向，首窗用转子侧≤20 mNm独立限幅及 `assist_gain ≤ min(1, 0.020 × rotor_per_joint / 1.64)`。之后用垂臂附近、前屈方向两个可重复姿态的同条件无／有补偿响应判断减负；若反向先查坐标／单位，不调大增益。保存候选与观察，不把主观省力感写成力矩标定。 | 映射、方向、区间或独立输出限幅未确认时不进入带动力 FOLLOW；约1.64 Nm不能单独使配置门就绪。 |
| 读/发周期、反馈过期和整段时限 | 先用已有 8 秒／404 组完整七属性只读记录估计读周期、最慢完整快照间隔和错误增量，仅作候选时序依据。零 Nm、模式及 IDLE 帧也属于电机命令；读发并行、邮箱忙、零力 TXOK／排空和实际停止时延只能在取得当次许可的 R4-2B 现场窗口验证。反馈时限须早于已确认工作区间的边界风险，数值与证据条件一同冻结。 | 时间参数为 0，`S4Site_ProfileReady()` 拒绝；只读数据不能把发/停预算写成已实测。 |
| 正常撤力斜率、最大撤力预算与提醒 | 首轮 `release_slew_nm_s` 是待实测的正值试验参数：依据此前 20 mNm 单次动作幅值、当前承托方式和只读/零输出时测得的发送周期，先选一个不会占满停止余量的缓降时长，再算斜率，记录选择依据；在首个受监督短窗观察卸力感及新鲜反馈后冻结或收紧，不能预称已验证舒适性。按最大许可转矩计算降到 0 所需时间，向上取整到输出周期，再加零 Nm 和 IDLE 提交的有界预算，得 `ramp_max_ms`。`session_ms > ramp_max_ms`，警示提前量固定 2000 ms；若会话太短，成功 `followstart` 回复当场提示。斜率/预算和读发负载共同核定，不照搬参考项目的 0.3 s。 | 没有可解释的试验值/发送周期、无法在期限内零力/提交 IDLE，则禁止 `followstart`；首窗实际卸力异常则停止升级并查因。 |
| FOLLOW 重定位漂移判据 | 本轮台架判据预设为松手缓冲 0.5 s、随后 5 s 内首末 0.5 s 中位数差≤0.5°、所有 0.5 s 中位数相对起点偏离≤0.75°；全程用新鲜完整反馈并核小区间余量。先用只读静止数据检查关节角噪声足够低、映射可信；数值是验收容差，不是测得的性能。 | 映射/噪声/余量不足或会话不能覆盖完整 5.5 s 时，不能判重定位通过；不能放宽安全包络来凑验收。 |
| 停止与异常切断 | 核本机 FW0.6.5 watchdog 实读值/厂家适用说明；在正式持续助力前明确本窗口谁可触及 24V 断电、停止后怎样观察 IDLE 和新鲜位置；只针对所采用的切断路线保留一项实际证据。 | `stop_method_confirmed=false`，不能 Arm。 |

这些动作是 S4 新功能的最少前提，不要求恢复 S3 原 A–D 全表，也不在此计划中填入猜测常数。当前板上是只读镜像，实际带动力窗口需先确认现场电源与镜像身份，不能由建计划触发刷写或运动。

## 4. 分轮实施清单

以下为初版 PLAN 清单，R4-0/R4-1 的执行结果及用户取消新增角度配对的决定，均以页首 2026-10-09 剩余合同和唯一开发日志为准；表内旧测量动作不再作为当前待办。表内函数是 PLAN 时拟新增/拟修改的签名，实际完成情况以 `edits.md` 和源码为准。`App/`、`tests/`、`CMakeLists.txt` 相对唯一活动工程 `firmware/Exoskeleton_F446/`；`Main_ project/` 路径相对仓库根。L1–L4 采用本项目四层架构：L1 平台、L2 设备、L3 控制、L4 应用。`review:true` 表示该项须有依据和审查；用户已在看到详细 PLAN 后要求开始执行，当前授权范围先从默认禁用输出的离线轮次开始，不代表许可电机动作。

| 轮次/编号 | 文件、层级与具体操作 | 验证标准及停止条件 | review |
|---|---|---|---|
| R0-1 证据审计 | `Main_ project/Hardware_information_list/hardware_interface_registry.md`（硬件事实）、`Main_ project/docs/development_schedule.md`（唯一阶段日志）核既有 S4 坐标/负载/停止来源；`.auto-embedded/tasks/s4-shoulder-follow-plan/research.md` 列原始证据和缺口，不为缺失值造数据。 | 每个已知值有日期、条件、来源和验证级别；角度/方向/限幅没有依据时仍标未确认，但 R1–R3 的默认禁用离线工作可继续。 | true |
| R1-1 重力纯函数 | 新建 `App/control/gravity_compensation.h/.c`（L3）：`bool GravityCompensation_Compute(const GravityCompensationConfig *config, const JointState *state, float *joint_torque_nm)`；一自由度静态式的势能、导数和符号定义记录在同任务 `research.md`；同步把 `.c` 显式加入顶层 `CMakeLists.txt`。 | 参考零位处输出 0、正负姿态符号符合定义、Nm/rad 一致；非有限输入/无效反馈拒绝且不产出有效候选；不调用 HAL/CAN。 | true |
| R1-2 默认禁用配置 | 在 R1-1 类型已存在后新建 `App/application/s4_site.h/.c`（L4）并加入顶层 `CMakeLists.txt`：`const S4SiteProfile *S4Site_GetProfile(void)`、`bool S4Site_ReadReady(const S4SiteProfile *p)`、`bool S4Site_ObservationReady(const S4SiteProfile *p)`、`bool S4Site_ProfileReady(const S4SiteProfile *p)`。第一项就绪门仅核只读 node、属性时序/反馈过期，不要求 `SafetySupervisor_ConfigReady()`；第二项再核映射/模型以显示候选；第三项核全部输出/停止/会话参数。`S4SiteProfile` 汇集一份 `S3SiteProfile` 读/安全配置、重力配置、助力比例、发送间隔、总会话期限、`release_slew_nm_s`、`ramp_max_ms`、固定 2000 ms 提醒提前量和编译期 `output_enabled`；初值 `enabled_axes=0`、增益/撤力斜率/撤力预算均为 0、确认位 false、`output_enabled=false`。输出就绪门要求最大许可转矩在 `ramp_max_ms` 内能按发送周期降到零并提交 IDLE，且会话长度大于该预算。 | 默认构建下 S4 不能 Observe/Prepare/Arm；R4-0 只读参数齐备后可打开 reader，输出仍为零许可；未冻结斜率/时限时不能 Start；S3 默认 site 不变。 | true |
| R1-3 模型检查 | 新建 `tests/host/test_s4_gravity.c`（主机测试），修改 `tests/host/test_s1.c` 的 `main()` 用例分派及 `tests/run_host_tests.py` 的显式源码/用例表，覆盖零位、两侧姿态、极值/NaN/失效；传动/助力换算留待 R3 控制链。 | 主机用真实控制模块编译运行；符号/单位与手算相符，未取得现场映射时不宣称电机侧上限已验证；失败先定位根因。 | true |
| R2-1 读事务独占 | 修改 `App/application/shoulder_read_diagnostic.h/.c`（L4）增加 `ShoulderRead_ClaimS4()` 和对应 owner 枚举，阻止诊断在 S4 占用时另起请求；修改 `App/application/s3_shoulder_service.h/.c`（L4）新增显式 S3/S4 读服务模式：`S3ShoulderService_Init(const S3SiteProfile *p, ShoulderReaderMode mode)`，保留现有 `S3ShoulderService_ProfileReady()` 的 S3 输出门，另增 `S3ShoulderService_ReadProfileReady()` 只检节点、属性时序/新鲜度/窗口而不调用完整 `SafetySupervisor_ConfigReady()`；S4 `Start()` 申领一次独占会话，`healthy()` 的 S4 只读分支不读取不存在的 stop GPIO，仍查总线和驱动心跳。此轮修改 `App/application/can_monitor.h/.c`，使 `CanMonitor_Init(const S3SiteProfile *active_profile, ShoulderReaderMode mode)` 显式接收 reader profile/模式；同步修改 `App/application/app_main.c` 和 `tests/host/test_can.c`、`test_c620.c`、`test_sdo.c`、`test_shoulder_properties.c`、`test_encoder_stream.c`、`test_s3.c`、`test_shoulder_qualification.c` 的全部调用点，暂传现有 `S3Site_GetProfile()` 和 S3 模式；`tests/host/test_shoulder_chain.c` 直接调用 `S3ShoulderService_Init()` 也同步更新。带动力的有限读尾窗在 R3-1 接入。 | S4 与 S3/encstream/诊断互斥；只读 reader 在 `enabled_axes=0` 时可工作但不能授予输出；新旧请求/回复不串配；reader 期限、超时、忙邮箱和 CAN 错误失败关闭；原 S3/只读主机情形继续通过。 | true |
| R2-2 S4 只读数据链 | 修改 `App/application/s3_feedback.h/.c`（L4）增加不授予输出的 `bool S3Feedback_ReadShoulderObservation(const S3SiteProfile *p, uint32_t now_ms, S3ShoulderObservation *out)`：独立返回原生七属性/心跳、时戳和读质量；仅在反馈协议、参考位、方向/比例确证后另给有效 `JointState`。不得把 `S3Feedback_Read()` 的整体返回值当作 S4 观察成功，因为 `enabled_axes=0` 时它恒为 false；非 S4 路径保持原样。新建 `App/application/s4_follow.h/.c`（L4）：`void S4Follow_Init(void)`、`void S4Follow_Poll(uint32_t now_ms, uint32_t elapsed_ms)`、`bool S4Follow_Observe(uint32_t now_ms)`、`void S4Follow_Cancel(void)`、`void S4Follow_GetStatus(S4FollowStatus *out)`，只读/算、不绑定输出，并把 `.c` 加入顶层 `CMakeLists.txt`。新建 `tests/host/test_s4_observe.c`，同步加入 `tests/host/test_s1.c` 分派和 `tests/run_host_tests.py` 源码/用例表。 | 未有新完整快照时只报原生值/不形成有效候选；映射确认后才报告关节角和模型值；每个候选追溯到属性序列与时戳；停止/错误退役 reader；R2 本身能编译并运行只读主机情形。 | true |
| R3-1 持续门控 | 修改 `App/control/safety_supervisor.h/.c`（L3）增加仅肩轴的 S4 受监督策略，并逐一检查 `ConfigReady`、`CheckBeforePrepare`、`Check`、`StopObserved` 的轴/限值/超时/停止语义；只将站稳作为观察结果，不当成独立急停。修改 `App/application/output_gateway.h/.c`（L4）增加 `bool OutputGateway_RefreshFollow(OutputGateway *gateway, float motor_effort_nm, uint32_t sample_ms, uint32_t sequence, uint32_t now_ms)`；核 `Prepare/Arm/AcknowledgeFault` 的零目标准备、显式人工许可和本次会话后禁止故障清除重臂。`RefreshFollow` 只更新最新目标，`OutputGateway_Step()` 的 S4 分支在新序列或仍新鲜样本的有界斜率步进且到最小发送间隔时才 purge/submit；每次让七属性读邮箱可继续前进，读/发忙或饥饿触发有界超时停机。限制转子 Nm、逐样本 TTL、整段会话硬截止；逐样本过期属于故障，整段到期走 R3-1B 正常撤力；`Replace()` 对 S4 拒绝，S3 单次语义保持。修改 `App/application/s3_feedback.c` 使 S4 `feedback_stationary` 使用肩侧停稳观察。 | 准备未完成、重复/过期样本、调度/读邮箱饥饿、越界/过速、发送/清邮箱失败立即故障撤权；一个发送周期只用最新有效候选，无旧帧积压；S4 不每 tick 重发，S3 原用例通过；故障和停后不在同一冷启动重臂。 | true |
| R3-1B 正常撤力与读尾窗 | 修改 `App/application/output_gateway.h/.c`（L4）加 `OUTPUT_RAMPING_DOWN` 和 `bool OutputGateway_BeginNormalStop(OutputGateway *gateway, OutputReason reason, uint32_t now_ms)`，只允许 S4 健康的人工 Cancel/整段会话预定到期进入；冻结最后被传输层接受的 `applied` 命令为起点，立即拒绝新候选/再 Arm，按 `release_slew_nm_s` 的绝对值单调递减，至零 Nm 帧提交成功再经唯一 transport purge/submit 发 IDLE。硬截止前最迟 `D−ramp_max_ms` 起斜坡；主动取消最多 `ramp_max_ms`，不能完成或中途故障则沿原 `withdraw(..., fault=true)` 硬撤。修改 `App/application/s3_shoulder_service.c` 保持 ramp 期间 reader 活跃，IDLE 提交后才由 `BeforeOutputStop`/`AfterOutputStop` 开始有限尾窗，记录新鲜 IDLE/位置并退役；核 `App/application/output_can_transport.c` hook 在 S4 路径仍正确。 | 取消立即失去新目标许可但命令侧撤力进度保持可见；正常斜坡的已提交转矩命令绝对值单调不增，零 Nm→IDLE 顺序可证，硬截止前完成；实际卸力另在现场核实；故障立即硬撤，无健康假设时不继续斜坡；旧 S3 Cancel/TTL 仍即时停止。 | true |
| R3-2 启动、提示与命令 | 给 `App/application/s4_follow.h/.c`（L4）增 `bool S4Follow_Prepare(uint32_t now_ms)`、`bool S4Follow_Start(uint32_t now_ms)` 并调用 R3-1/R3-1B 网关接口；`followprepare` 在新冷启动里启动唯一 S4 reader，取得新鲜模式/零目标证据后方可人工 `followstart`。带动力阶段用 `S3Feedback_Read()` 的完整安全输入，不能用 R2 观察接口替代。S4 服务在 `D−ramp_max_ms−2000` 前后只排队一次固定 UART 扶稳警示；短会话由 `followstart` 成功回复给出剩余/撤力时刻和扶稳提示，具体 ASCII 文案见第 2 节。提示使用 `BoardConsole_TxFree()` 留足 `S1_REPLY_SIZE` 回复空间，有界重试/记录 `warning_due/queued/failed`，不得阻塞或影响撤力；`followstatus` 显示 `remaining_ms`、`ramp_in_ms`/`ramp_remaining_ms`、`ramping_down`、`motor_effort_active`，其中 `motor_effort_active` 是网关命令状态而非物理力矩测量，`output_allowed=0` 只表示拒绝新助力，不表示电机已撤力。修改 `App/application/app_main.c` 按 `S4Site_ReadReady()` 选择 S4 reader/服务，仅 `S4Site_ProfileReady() && output_enabled` 时绑定当次唯一输出门；S4 模式不初始化 S3 出力服务，沿用既有 1 ms 调度且斜坡及尾窗均继续轮询。修改 `App/application/console_commands.c` 增 `followobserve/followprepare/followstart/followstop/followstatus`，`followstop`/S4 `cancel` 正常撤力开始时按第 2 节回复；零 Nm/IDLE 提交成功后状态才报告停止命令完成，故障硬撤不得误报正常斜坡完成；S4 活跃时旧 `prepare/arm/ackfault/shoulderpulse/elbowpulse` 全部拒绝，公共 `cancel` 必定作用于 S4；S3 模式不暴露 S4 输出入口。复核顶层 `CMakeLists.txt` 已包含 R1/R2 新源。 | S4 默认为 SAFE_OFF；只读 observe 不绑定输出，准备后仍需人工 start；提醒按时、单次、非阻塞，队列失败也准时撤力；状态区别新目标许可与命令侧尚在撤力，物理撤力靠现场观察核实；禁用配置下旧 S3/只读命令不倒退。 | true |
| R3-3 控制链主机回归 | 新建 `tests/host/test_s4_follow.c`（主机测试），更新 `tests/host/test_s1.c` 的 S4 用例分派、`tests/run_host_tests.py` 的显式源码/用例表及现有 S3 受影响 mock。 | 覆盖无效 site、七属性缺项、重复序列、增益/传动换算、读/发并行与邮箱饥饿、会话期限、正常取消/到期的单调斜坡与零 Nm→IDLE 顺序、斜坡中反馈/CAN/UART/调度故障硬撤、临期预算不足、提醒时刻/短窗回复/队列满、发送失败、故障锁存、停后禁止重启；S4 活跃时旧 S3 出力 CLI 拒绝且公共 cancel 路由正确；S3 单次、C620 隔离和原 reader 测试仍过。 | true |
| R3-4 目标构建 | 使用仓库 README 的 WindowsDebug 预设和 auto-embedded `aemb-build-cmake` 完成一次配置/构建；运行相关主机测试及 `tests/check_generated.py`，审查本轮新增告警。 | 构建退出 0、无新增警告；新增文件确在 ELF；`main.c`、.ioc、HAL Drivers、引脚/IRQ/CAN 位时序不变。失败停在软件轮次，不刷写。 | true |
| R4-0 小区间只读定参 | 在承托下先复用现有 EncoderStream/原始位置只读入口，给 `Main_ project/Hardware_information_list/hardware_interface_registry.md` 记录重力参考姿态、关节正向、转子/关节比例、小区间与固定肘/负载的来源；只把证据足以支持的读/模型字段写入 `App/application/s4_site.c`，保持 `enabled_axes=0`、`output_enabled=false`。 | 新鲜转子值与两个可重复实物姿态对应，转换后方向一致；不足以支持映射的字段仍未确认，仅可观察原生值。修改 site 后运行相关主机检查并重建 WindowsDebug，记录新 ELF 身份；此轮不出力。 | true |
| R4-1 上板只读验证 | 确认 R4-0 新镜像的构建产物和烧录/读回身份、启动时 SAFE_OFF 与 `output_enabled=false`，冷启动后执行一次 `followobserve`；`followstatus` 输出原生值、可用时的新鲜关节姿态/模型候选、序列/年龄和故障原因；在 `Main_ project/docs/development_schedule.md` 记录原始采集文件及镜像身份。 | 同一窗口静态/手推的小区间变化对应模型符号；映射后的静止角度噪声显著小于 0.5° 漂移门槛，两个试验姿态及重定位点有足够小区间/停机余量，CAN 错误增量/读请求时序可解释。观察结束 reader 退役，再做带动力试验须重建并冷启动；异常先保留原始日志查因，不靠放宽漂移/范围阈值通过。 | true |
| R4-2 现场配置与短窗 | 仅在 R0/R4-1 证据齐备后集中填写 `App/application/s4_site.c`（L4）的本机输出/安全参数，包括撤力斜率和最坏 `ramp_max_ms`；`Main_ project/Hardware_information_list/hardware_interface_registry.md` 记录每个值和来源；重跑相关主机用例、WindowsDebug 构建，核新 ELF/烧录读回身份并实测启动 SAFE_OFF，然后在固定承托、可人工切 24V 的条件下安排受监督短窗。 | 首次新动作转矩幅值不超过旧 20 mNm，窗口时长只按验证过的读发周期、撤力预算/IDLE 和承托余量设定，不能硬套 100 ms；实际方向、开始/到期前提示、正常 Cancel/到期的零 Nm→IDLE、故障硬撤、有限读尾窗的新鲜 IDLE/位置及人工 24V 切断可达均有记录。后续是否延长按本次结果决定，不自动升档或重试；缺停止证据即只做 R4-1。 | true |
| R4-3 FOLLOW 效果对照 | 复用低姿态基础联调证据；在固定肘姿态、负载和扶持点下取得同一可重复肩姿态的零目标与补偿开启对照，记录角度/反馈新鲜度、扶持力或明确手感、自走/振荡/触边与停止结果。一次必要现场窗口即可；原始摘要和用户观察分别保留。 | 同条件扶持努力有可辨减小，运行与停止正常，才写 S4 效果通过；手感不清或支撑条件变化则记未证实。两姿态及原漂移数值不再是硬门槛；未测项保留未测。 | true |

### 新文件 include 白名单与依赖方向

**R1 执行注记（2026-10-07）**：`S4Site_ReadReady`/`ObservationReady` 只检查配置及确认标志，不检查实时反馈年龄；R2/R3 reader 和安全门负责新鲜度。`S4SiteProfile` 额外列出 `stop_submit_budget_ms`，使斜坡预算显式包含零 Nm/IDLE 提交余量。当前 `S4Site_ProfileReady` 只承认已有且已配置的外部停止输入策略；现场无此输入，默认输出门保持关闭。R3 增加专用 S4 受监督策略后须扩展此门，不能借 S3 单次脉冲策略直接放行。R1 主机检查增加了默认拒绝及读/观察配置门用例；代码和验证结果见 `edits.md`。

**R2 执行注记（2026-10-07）**：`S4Follow_Init` 的实际签名改为 `S4Follow_Init(const S4SiteProfile *profile)`，让冷启动选中的只读 profile 显式传入，避免隐藏全局切换；R3 的 `app_main` 仍须只在冷启动选一次 S3/S4，不可运行中重调 `CanMonitor_Init`。输出配置门另要求已配置肩侧停稳观察、新鲜度可覆盖完整七属性周期，以及尾窗下界覆盖读回和 dwell；这些仅是参数自洽检查，不能代替现场新鲜 IDLE/位置及停止证据。R2 代码、测试和仍未接入主循环的边界见 `edits.md`。

| 文件 | 层级与命名 | 允许的直接 include |
|---|---|---|
| `App/control/gravity_compensation.h/.c` | L3 控制；沿用工程 `joint_state` 命名风格 | `joint_state.h`、`stdbool.h`、`stdint.h`；实现文件可加 `math.h`、本模块头。禁止 HAL/BoardCan。 |
| `App/application/s4_site.h/.c` | L4 应用配置；沿用 `s3_site` 风格 | `s3_site.h`、`gravity_compensation.h`、`stdbool.h`、`stdint.h`、本模块头。禁止直接厂商 HAL 头。 |
| `App/application/s4_follow.h/.c` | L4 应用服务；沿用 `s3_bench` 风格 | `s4_site.h`、`s3_feedback.h`、`s3_shoulder_service.h`、`can_monitor.h`、`output_gateway.h`、`output_can_transport.h`、`board_runtime.h`、`stdint.h`、`stdbool.h`、本模块头。仅 L1 平台接口可见，禁止直接 HAL 寄存器/发送电机帧。 |
| `tests/host/test_s4_gravity.c`、`tests/host/test_s4_observe.c`、`tests/host/test_s4_follow.c` | 主机测试，不进入目标运行层 | 只引用对应公开头与主机 mock，不引用板上 `main.c`。 |

## 5. 轮次出口与错误回流

- R1/R2/R3 可先纯离线完成，S4 site 一直禁用；这不依赖另一次电机运动。R4-1 是新 S4 镜像的只读观察，R4-2/R4-3 才是带动力工作。
- 方向矛盾、CAN Form/Bus-off 复现、读/发并行失效、停止帧/备用切断无适用证据、手推时自行加速或振荡：立即停本轮、冻结日志，按根 AGENTS §2.1 查明原因再改代码/参数；同一 bug 三次失败回到接口/架构讨论。
- 每轮代码只改该轮目标，提交前核 `git diff` 保护用户已有 `.vscode/`；有实质变更/测试只在 schedule 开发日志追加一条。此 PLAN 不要求本轮编译、刷写或现场运行。

## 6. 审查门

第 17/18/21 行重力模型与合成需本项目自主实现：ExoKit 无连续重力模块，GALAXEA 的 Python/六轴模型及限值不能直接搬进 F446，SymPy 只作为离线推导工具。自主实现风险是方向/单位、时序、并行 CAN、异常停机；用户已在阅读详细 PLAN 后于 2026-10-07 要求开始执行，任务转入 EXECUTE，先完成默认禁用的离线轮次。此确认不等于输出许可、刷写或电机运动授权。
