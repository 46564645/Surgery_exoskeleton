# S4 R4-3 两姿态对照与重定位：现场证据

**记录范围：2026-10-09 的低姿态现场尝试；2026-10-10 复核后，第二次 15°/s 短窗的低姿态基础联调按用户选定范围通过，减负效果另测，S4 效果验收未完成。** 本页保存可追溯的阶段事实；不把准备、软件构建或口头感受预写为减负效果。

## 候选与证据层级

- 隔离 HSE 试验副本由 [`prepare_candidate.py`](prepare_candidate.py) 从已核对的首窗材料化副本生成，只把属性读取窗 `30000→35000 ms`、FOLLOW 会话 `3000→10000 ms`。固件软件区间仍为垂臂前屈 `20°–30°`，反馈速度保护 `10°/s`，转子侧命令帽 `20 mNm`，正常撤力最长 `1 s`；活动生产 `App/application/s4_site.c` 仍保持输出禁用。该 10 秒会话提供 R4-3 计划中的 0.5 秒松手缓冲和随后 5 秒观察时间，但时间预算不等于漂移证据。
- 隔离构建记录位于被 Git 忽略的 [`build-result.json`](../../../build/s4-r4-3-20261009/build/Debug/build-result.json)：0 编译警告、0 CMake 提示，BIN `113676 B`，SHA256 `27a3a327d6c58e0a7cbf563e1c6627670dba8ab4475be0b1f02bfc4246374010`。主机配置探针得到 `read_ready=1/cold_profile_ready=0/session_zero_ready=1/after_zero_ready=1`、10 秒会话与 35 秒读取窗；这仅检查软件配置。构建记录生成时写的“not flashed”是当时状态，后续刷写事实以下述板上记录为准。
- 用户确认 24V 关闭、机构承托后，现场先保存完整 512 KB Flash；其 SHA256 为 `4a5e3263729d83ae979d3796bffad830eea6b8c35fe6fc6dbe9360629a2ddc9f`，旧镜像前缀与已知 `c3a1c620…0e763` 一致。STM32CubeProgrammer 2.23.0 一次刷写同一隔离 ELF 并验证；另行读回的 `113676 B` SHA256 与候选 BIN 一致。详见 [`attempt.json`](../../../build/s4-r4-3-board-20261009/attempt.json) 及同目录 `backup/flash/readback` 日志。刷写和独立读回证明板上镜像身份，不证明电机输出或物理效果。
- 现场低姿态由 [`capture_once.py`](capture_once.py) 逐次冷启尝试；`--mode high-drift` 的第二姿态尚无现场结果。R4-3 原计划要求同一肘姿态和负载下的两个可重复姿态分别有无补偿比较，并在至少一次重定位松手后缓冲 0.5 秒、连续 5 秒以每 0.5 秒新鲜角度中位数判定首末差 `≤0.5°`、全窗相对起点最大偏离 `≤0.75°`。这些是验收判据，不是现有观察值；参见 [S4 计划](../../../.auto-embedded/tasks/s4-shoulder-follow-plan/prd.md)。

## 原 10°/s 候选已执行的低姿态尝试

各轮原始摘要与逐命令事件都在被忽略的 `firmware/Exoskeleton_F446/build/s4-r4-3-board-20261009/attemptN/`。六份摘要均为 `result=FAIL_OR_INCOMPLETE`、`samples=[]`、`operator_effort_and_touch_observation=null`、`followstart_attempted=false`。因此尚无持续助力、两姿态手持努力对照或 5 秒漂移结果。

| 轮次 | 有界事实与终止原因 |
|---|---|
| [1](../../../build/s4-r4-3-board-20261009/attempt1/r4-3-low-summary.json) | USB-only 为 `SAFE_OFF`；上电后有新鲜 IDLE 心跳，`followprepare` 建立 RAM 零点 `2928 mrev`，但 19 秒内未取得初始姿态的新鲜固件 READY。脚本标记不完整并发出 `followstop`；未启动 FOLLOW。摘要不支持推断是设备故障还是人工姿态原因。 |
| [2](../../../build/s4-r4-3-board-20261009/attempt2/r4-3-low-summary.json) | 冷启 USB-only 为 `SAFE_OFF`，上电 IDLE 心跳新鲜；RAM 零点 `2916 mrev`，仍未在 19 秒内取得初始 READY。脚本发出 `followstop`，未启动 FOLLOW；同样不凭超时猜根因。 |
| [3](../../../build/s4-r4-3-board-20261009/attempt3/r4-3-low-summary.json) | 冷启 USB-only 为 `SAFE_OFF`，本次上电预检未取得要求的新鲜电机心跳，脚本在 `followprepare` 前退出。没有出力指令，也没有证据确定心跳缺失的根因。 |
| [4](../../../build/s4-r4-3-board-20261009/attempt4/r4-3-low-summary.json) | RAM 零点 `2986 mrev`；手动前抬时新鲜反馈到 `phase=6` READY、`seq=414/joint_mrad=372`（约 `21.3°`），长音提示此时发出。随后在零目标、零输出许可的基线观察开始时转为 `FAULT`；脚本 `followstop` 收到 `ERR fault`，未发送 `followstart`。故障后只读串口快照见下节，不能将该次 READY 写成减负对照通过。 |
| [5](../../../build/s4-r4-3-board-20261009/attempt5/r4-3-low-summary.json) | 用户再次确认 24V 关闭且静止后复位，USB-only `PASS/SAFE_OFF`；上电 5 秒正常，新鲜 IDLE 心跳。`followprepare` 建立 `2897 mrev` RAM 零点；提前的长音于 `20.5119°/seq=896` 发出，但已接近准备后 18 秒。随后新鲜 `phase=6` READY（`seq=919/joint_mrad=367`，约 `21.0°`，`fault=0/output_allowed=0/cmd_torque=0`）。主机脚本因超过为 1.5 秒无补偿基线预留的 17 秒门而拒绝继续，`followstop` 回复 `OK FOLLOW idle_submitted; observing_stop`。未发送 `followstart`；用户随后确认 24V 已关闭、机构静止无异常。此轮是准备时间不足，并非第 4 轮速度故障复现；回复只表示停止请求已提交/进入观察，不能单独证明帧 TXOK 或物理卸力。 |
| [6](../../../build/s4-r4-3-board-20261009/attempt6/r4-3-low-summary.json) | 用户在 24V 关闭时复位，USB-only `PASS/SAFE_OFF`；上电 5 秒正常，预检见新鲜 IDLE 心跳。`followprepare` 建立 `2964 mrev` RAM 零点，三声短音后用户手动前抬。主机尚未发出长音，串口先见新鲜 READY `seq=628/joint_mrad=353`（约 `20.225°`，`cmd_torque=0/output_allowed=0/fault=0`），紧接 `seq=629` 转为 `FAULT`，`followstop` 收到 `ERR fault`。未发送 `followstart`；用户随后关闭 24V、承托机构，确认静止无异常。故障 RAM 判读见下节。 |

## 第 4 轮速度故障的证据边界

用户切断 24V 后，以 SWD Hot Plug 从未复位 RAM 保存 [`gateway-after-fault-232b.bin`](../../../build/s4-r4-3-board-20261009/attempt4/gateway-after-fault-232b.bin) 与 [`status-after-fault-228b.bin`](../../../build/s4-r4-3-board-20261009/attempt4/status-after-fault-228b.bin)。按该候选 ELF 的结构布局解码：`OutputGateway.state=3 (FAULT)`、`reason=2 (HEALTH)`、`safety_reason=7 (SAFETY_SPEED)`；保留的反馈关节速度为 `0.22170819 rad/s≈12.70294°/s`，超过软件门 `0.17453293 rad/s=10°/s`。`stop_attempted=1/stop_accepted=1/purge_ok=1/stop_delivery_failed=0/follow_session_used=0`。这直接支持**固件看到速度越限并撤销许可**；该速度来自驱动器/编码器反馈换算，现场没有独立量角测速，不能确定是实际手推速度还是反馈尖峰。

24V 关闭后的 [`fault-readonly.json`](../../../build/s4-r4-3-board-20261009/attempt4/fault-readonly.json) 读取到 `phase=4/reader=7/fault=1/output_allowed=0`，但只读脚本因 `canstatus` 不在自身白名单而把整次任务标记 `FAIL`；这项脚本失败不推翻已保存的串口原始回复和 RAM。断电后的 `error=0x40/fault=9`、陈旧心跳属于断电后状态，不能倒推为准备阶段故障原因。第 4 轮没有持续非零转矩指令，不能以主观声音或手感判断驱动器是否真正闭环。

### 第 6 轮同类故障

第 6 轮 24V 关闭后也以 Hot Plug 保存未复位 RAM：[`gateway-after-fault-232b.bin`](../../../build/s4-r4-3-board-20261009/attempt6/gateway-after-fault-232b.bin) 与 [`status-after-fault-228b.bin`](../../../build/s4-r4-3-board-20261009/attempt6/status-after-fault-228b.bin)。按同一候选 ELF 布局，网关为 `state=3 FAULT/reason=2 HEALTH/safety_reason=7 SAFETY_SPEED`；`prepare_ms=34807/last_step_ms=34946`，`armed_ms=0/follow_session_used=0/target_applied=0`，`stop_attempted=1/stop_accepted=1/purge_ok=1/stop_delivery_failed=0`。故障保留的最后反馈位置 `0.3565459 rad≈20.429°`、关节速度 `0.2321944 rad/s≈13.304°/s`，再次高于 `10°/s` 门。这与第 4 轮同属固件看到速度越限的准备阶段故障；仍不能仅凭反馈确定为实际手动速度、准备/闭环切换影响，或反馈尖峰中的哪一种。现场停止继续带驱动准备，下一步只读检查速度时序与状态切换关系，查明原因前不改代码或放宽门限。

## 提示与后续记录位置

用户要求改为**更早的清晰提示音并慢速前抬，长音开始即停住扶稳**。第 4 轮长音在约 `21.3°` 才发出，随后发生速度保护；第 5 轮现有脚本在约 `20.51°` 发出长音，但移动到提示点耗掉近 18 秒，因准备许可时间余量不足而退出。主机提示只协助现场配合，不改变固件 `10°/s` 速度保护、`20°–30°` 区间或 `20 mNm` 命令帽；不能把提示提前解释为保护已获实测验收。

后续轮次须记录所用镜像 SHA、冷启与上电预检、零点/长音/READY 时刻、是否真正发送 `followstart`、无补偿与有补偿同姿态扶持观察、重定位释放与 0.5 秒＋5 秒角度样本、停止与供电/承托终态。**截至原 10°/s 候选第 6 轮，上述效果判据均未完成；后续不预填通过结论。**用户已决定跳过 R4-2B 独立运行中人工切断 24V 的追加试验，该项保持未测，不重新列为 S4 完成前置。

## 用户选择的临时 15°/s 隔离候选

前述 10°/s 候选的两轮 RAM 均显示速度保护触发，但未辨清实际手推、准备/闭环切换和反馈尖峰各自的作用。用户随后明确选择只为一次受限台架尝试把**隔离候选**速度门暂设为 `15°/s`；这不是运行限值已获安全验证，也不改变活动生产 site。对比两份 `source-manifest.json` 的 182 个构建输入，181 个内容哈希相同，仅隔离副本 `App/application/s4_site.c` 中 `.max_speed_rad_s` 一行由 `0.17453293f`（10°/s）改为 `0.26179939f`（15°/s）。`20°–30°` 区间、`20 mNm` 转子命令帽、站稳 `200 ms`、会话 `10 s` 和正常撤力最多 `1 s` 保持原值。

H 盘隔离副本的 [主机速度探针](../../../build/s4-r4-3-speed15-20261009/build/host-profile-r4-3/run.log) 为 `PASS`：按软件规则，原第 6 轮的 `13.304°/s` 可通过，而 `15.1°/s` 仍触发保护；同时核对角域和命令帽未变。离线 [构建记录](../../../build/s4-r4-3-speed15-20261009/build/Debug/build-result.json) 为 0 编译警告、0 CMake 提示；BIN `113676 B`、SHA256 `17abdac670a8095ab959f941c92d8207cb6596c314f717f998a335e9868f471b`，ELF SHA256 `deab2e41ecf08c3e84a70c8e54ace77eaf1653085394b9b98485e6160bc110f0`。探针证明代码对这两个输入的判定，不证明 15°/s 适合实物。

用户确认 24V 已关闭、机构承托后，先备份旧 Flash，确认旧镜像前缀为 `27a3a327…6374010`；再单次刷写并验证此新 ELF，独立读回的 113676 B 与候选 BIN SHA256 一致。[刷写与读回记录](../../../build/s4-r4-3-speed15-board-20261009/attempt.json) 标记 `FLASH_READBACK_MATCHED_USB_ONLY`；USB-only 检查 `PASS/SAFE_OFF`、输出许可 0。刷写和读回只证明镜像身份。用户再确认 24V 开启 5 秒正常，才进入首轮现场准备。

### 15°/s 候选第 1 轮现场结果

原始 [摘要](../../../build/s4-r4-3-speed15-board-20261009/attempt1/r4-3-low-summary.json) 与 [逐命令事件](../../../build/s4-r4-3-speed15-board-20261009/attempt1/r4-3-low-events.jsonl) 记录：`followprepare` 建立 `2964 mrev` RAM 垂臂零点，主机长音后在 `seq=623` 得到新鲜 READY，`joint_mrad=376`（约 `21.54°`）。零目标基线取得 18 条新鲜样本，中位数约 `21.715°`；固件没有测量人的扶持努力，摘要该字段仍待用户观察。随后 `followstart` 回复接受 10 秒会话、9 秒后开始撤力。运行中可见 `phase=7`、`cmd_torque=1/output_allowed=1` 及新鲜角度；故障 RAM 中 `INPUT_TORQUE` 属性值 `0.0069500115 Nm` 是驱动报告的**转子侧目标输入转矩**，不是机械实测力矩或减负幅度。

启动约 1.6 秒后，串口见 `phase=4/FAULT`，脚本补发 `followstop` 收到 `ERR fault`，摘要保留 `FAIL_OR_INCOMPLETE`。24V 关闭后 Hot Plug 保存的 [`gateway-after-fault-232b.bin`](../../../build/s4-r4-3-speed15-board-20261009/attempt1/gateway-after-fault-232b.bin) 与 [`status-after-fault-228b.bin`](../../../build/s4-r4-3-speed15-board-20261009/attempt1/status-after-fault-228b.bin) 按本候选 ELF 布局解为 `FAULT/HEALTH/SAFETY_SPEED`；最后反馈角约 `22.464°`、速度约 `17.338°/s`，超过本候选 `15°/s` 门。停止处理 `stop_attempted/accepted/purge_ok=1`、`stop_delivery_failed=0`。`seq=785/sample_ms=44463` 的 `joint_urad=382830` 到 RAM 保留的 `seq=787/sample_ms=44503` 的 `joint_urad=392073`，40 ms 内反馈位置差约 `0.530°`、两点平均约 `13.24°/s`；因此证据并非只有一个孤立的速度字段，但仍无独立机构测速，不能由此判定真实机械运动或速度峰值的来源。

用户关闭 24V 后确认机构承托、静止无异常；对有无补偿的手感回答“没有注意”。该轮证明在这个受限短窗内进入过 FOLLOW 并产生非零目标命令，也证明 15°/s 临时门仍因反馈速度越限而撤权；**低姿态同条件减负对照、高姿态对照及重定位后的 0.5 秒＋5 秒漂移均未完成，R4-3 未通过**。现场停止继续提高门限或重复带动力尝试，下一步只读分析速度与状态切换时序；更早的音频提示和临时 15°/s 数值均不作为保护效果证明。R4-2B 用户跳过的独立运行中人工切断 24V 仍为未测，不重新加为 S4 前置。

### 只读速度轨迹：区分手动移动与出力阶段

用户随后同意一次约 20 秒的**只读**诊断。仍使用已刷入、独立读回的上述 15°/s 隔离镜像；24V 关闭时冷启的 USB-only 检查为 `PASS/SAFE_OFF`，用户确认上电约 5 秒后状态正常。主机只发送一次 `followobserve`、只读查询和一次 `followstop`，没有 `followprepare`、`followstart`、模式或力矩命令。原始 [摘要](../../../build/s4-r4-3-speed15-trace-20261009-attempt1/summary.json) 与 [事件／351 条新鲜去重样本](../../../build/s4-r4-3-speed15-trace-20261009-attempt1/events.jsonl) 保留在 H 盘；全部样本为 `phase=2/reader=2/follow_fresh=1/output_allowed=0/cmd_torque=0/fault=0`。这轮不用于判断助力效果。

软件以**尚未独立校准的名义 8:1**换算，手动前抬时报告的关节速度峰值约 `22.230°/s`（序号 257，开始观察后约 5.14 秒）。其前后序号 255→257 的原始转子位置 `2.986621618→3.004265070 rev`、采样时间 `140812→140852 ms`，相邻差分折算约 `19.849°/s`；351 条报告速度中 14 条高于 10°/s，仅 1 条高于 15°/s，348 组相邻位置差分也仅 1 组高于 15°/s。这表明**在无电机出力命令的手动移动中，也能短暂越过 15°/s 软件门**，并非只有一个孤立的速度字段尖峰。位置与速度同源于驱动反馈，且无独立角速度量测；不能据此证明实际物理峰值，也不能把此前有动力运行后的 17.338°/s 故障完全归因于手推。

长音在约 14.55 秒开始；其后仍有约 `0.0193 rev`（名义 `0.87°`）的位置变化。用户表示长音后“好像没有扶稳”，因此该段**不能作为静止噪声、重定位漂移或补偿保持效果**。本轮 `followstop` 回 `OK FOLLOW read_cancel_pending`，下一条状态为 `phase=3/reader=3/output_allowed=0/cmd_torque=0/fault=0`，与只读取消后 `S3_READER_STOPPED=3` 的源码状态一致。原始主机摘要仍记 `FAIL_OR_INCOMPLETE/STOPPED status incomplete`，原因是采集脚本把读器正常停止误要求为 `reader=7`（到期观察完成）；原始失败记录未改写。仅修正该主机脚本的停后判据为接受 `reader=3` 或 `7` 且仍要求输出、命令、故障均为 0，`--offline-check` 通过；**没有重新上电或重采**。用户随后确认 24V 已关闭，机构静止无异常。

结论：只读采集完成并提供速度时序证据；此前 15°/s 带动力轮次的故障事实及 R4-3 未通过结论保持。下一次若继续效果试验，应先确保长音后的目标姿态能持续扶稳，再在同一小角域和原命令帽下观察；不凭这次轨迹继续提高速度门。

### 15°/s 候选第 2 轮：低姿态可比角度与正常停止

用户表示可在长音后用现有承托持续扶稳并同意再做一次低姿态对照。24V 仍关闭时复位同一已读回的隔离镜像，USB-only [检查](../../../build/s4-r4-3-speed15-board-20261009/attempt2/usb-only.json) `PASS/SAFE_OFF`；上电约 5 秒后用户确认姿态正常。仍为 `20°–30°` 小区间、`20 mNm` 转子侧命令帽、`15°/s` 试验速度门、10 秒会话。现场 [摘要](../../../build/s4-r4-3-speed15-board-20261009/attempt2/r4-3-low-summary.json) 与 [原始事件](../../../build/s4-r4-3-speed15-board-20261009/attempt2/r4-3-low-events.jsonl) 未改写。

垂臂 RAM 零点为 `2928 mrev`，固件 READY 时 `seq=608/joint_mrad=380`。零目标 READY 基线有 18 条新鲜样本，中位数约 `22.17347°`；`followstart` 获接受 10 秒会话，运行中 22 条新鲜角度样本（`seq=696–798`）的中位数约 `22.12903°`、范围 `22.11508–22.13960°`，两段中位差约 `0.04443°`。原始事件记录了持续新鲜的 `phase=7/cmd_torque=1/output_allowed=1/fault=0`；这只支持**低姿态在相近角度下有非零目标命令**，不是力矩实测或主观减负结论。用户事后回答“好像没有扶稳”，故该轮没有可信的同一扶持条件下手感比较，摘要原有 `operator_effort_and_touch_observation=null` 和 `LOW_ACTIVE_OBSERVED_PENDING_OPERATOR` 保持原样。

启动约 2.22 秒后，脚本主动 `followstop` 获 `ramping_down` 回执；先见 `phase=8` 渐撤，后到 `phase=3/reader=7/stop=1/fault=0/cmd_torque=0/output_allowed=0`，读链观察完成、后续新鲜 IDLE 心跳年龄 37 ms，CAN 错误及 bus-off 为 0。软件可观察的正常停止链完成；没有独立物理卸力测量。试后已提示用户切断 24V 并保持承托，**断电后的现场状态仍待回复**。高姿态对照及重定位 `0.5+5 s` 漂移尚未开始，原 R4-3 效果判据未通过。

### 2026-10-10｜低姿态基础联调的有界结论

用户选定“低姿态基础联调通过，效果另测”。依据本页第二次 15°/s 短窗原始记录，约 22°处的准备、非零目标命令与新鲜反馈并行、主动停止、渐撤和停后新鲜 IDLE 心跳均已观察到，故将**低姿态基础联调**记为通过；不改写原始脚本结果。用户说“好像没有扶稳”，没有可比的扶持努力记录，也没有支撑力实测，**重力减负效果仍未验证**。原两姿态和松手后 `0.5+5 s` 的 `0.5°/0.75°` 漂移判据不再作为当前个人原型效果验收的硬门槛；它们未执行，不写通过。后续效果试验采用同一可重复姿态、肘姿态和负载，在相同扶持位置比较零目标与补偿开启时的扶持力或明确手感，保留新鲜反馈、无自行加速／明显振荡／触边及正常停止记录。此轮只复核证据并调整记录，没有新一轮硬件操作；当前板上仍是隔离 15°/s 试验镜像，生产 site 输出仍禁用，试后 24V 物理状态未获确认。

### 2026-10-10｜同姿态零目标／补偿手感对照一次

用户同意继续一次同姿态手感对照，并确认试前 24V 关闭、机构承托、ST-LINK USB 已连接，垂臂零位、肘部与扶持位置已准备。新主机脚本 [`capture_effect_once.py`](capture_effect_once.py) 仅组织听觉提示、状态查询、一次 `followprepare`／`followstart`／`followstop` 和记录；没有修改固件、接线或隔离镜像参数。试前 USB-only [只读检查](../../../build/s4-r4-3-speed15-board-20261009/effect20261010-precheck/usb-only.json) 为 `PASS/SAFE_OFF`，试验 BIN 与独立读回 SHA256 同为 `17abdac670a8095ab959f941c92d8207cb6596c314f717f998a335e9868f471b`。用户上电约 5 秒后确认姿态正常。仍用 20°–30° 固件小区间、20 mNm 转子侧命令帽、15°/s 临时速度门、10 秒最长会话；脚本在约 3 秒补偿观察后主动停止。脚本 `--offline-check` 与语法解析通过，实际现场 [摘要](../../../build/s4-r4-3-speed15-board-20261009/effect20261010-run1/r4-3-effect-summary.json) 和 [原始事件](../../../build/s4-r4-3-speed15-board-20261009/effect20261010-run1/r4-3-effect-events.jsonl) 留在 H 盘，原始 `operator_observation` 空字段不改写。

本次 RAM 垂臂零点为 `2957 mrev`；固件 READY 时约 25.1°。零目标 READY 段约 1.03 秒，12 条新鲜角度样本中位 `25.09555°`；补偿段约 3.01 秒，31 条新鲜样本中位 `25.11194°`，两段中位差约 `0.01639°`。`followstart` 接受 10 秒会话，运行中观察到 `cmd_torque=1/output_allowed=1/fault=0` 和新鲜反馈；脚本主动 `followstop` 获 `ramping_down`，终态 `phase=3/reader=7/stop=1/fault=0/cmd_torque=0/output_allowed=0`，随后 IDLE 心跳年龄 115 ms，肩侧 CAN 错误与 bus-off 均为 0。角度接近只描述姿态，**不等于支撑力或减负测量**；`cmd_torque=1` 只表示存在非零目标，不能证明电机实际机械转矩。

试后用户确认已关闭 24V，机构承托、静止且无异常；对零目标与补偿两段的手感回答“没感觉”。本轮只支持**同姿态对照的命令／反馈／停止链正常，操作者未报告可辨减负**；没有仪器测力，也未取得明确的两段扶持努力差异或实际电机转矩，故 R4-3 补偿效果**未证实，S4 效果验收未完成**。既有低姿态基础联调通过结论保持。下面已用现有配置和记录核对目标量级；不凭这次“没感觉”盲目提高命令帽或重复同样短窗。

只读量级复核：READY 时 `model_mNm=695`；按镜像 `assist_gain=0.09`、名义传动比 8，软件计算的转子目标约 `0.695×0.09÷8=0.00782 Nm`，理想关节端约 `0.0626 Nm`，即本模型所估重力矩的 9%。这不是本轮实际转子目标读回，更不是机械托力。旧轮相近姿态的 `INPUT_TORQUE` 读回约 7 mNm，同样只是驱动目标值。`followstart` 回执中的 `ramp_in_ms=9000` 表示距自动渐撤尚有约 9 秒，并非 9 秒渐入；按本镜像 `0.020 Nm/s` 增力斜率，软件从零达到上述目标约需 0.4 秒，因此 3 秒段足以观察目标建立。现有记录仍无法区分“助力量太小而难以感知”和“实际机械托力未形成”；没有仪器时保留原因未定，不重复同条件试验。用户此前说明现场没有拉力计，若以后要判别实际减负，需另有能分辨该小变化的同姿态扶持力读数。

### 2026-10-10｜0.18 增益隔离候选与两次现场尝试

用户认为原助力太小并要求提高。功能对应索引第 18 行“连续重力补偿”：本次核对 GALAXEA-A1Z `a1z/robots/arm_robot.py::_update` 的重力项倍率与限幅流程，以及 `examples/gravity_comp.py` 的小倍率起步示例；只借鉴“模型目标按倍率调节并受命令边界约束”的思路，未采用其参数或硬件控制代码。旧轮“没感觉”支持试验性增加目标，但**不能证明原因为增益不足**，也没有实际机械托力测量。

在 H 盘新建 [隔离源码及构建记录](../../../build/s4-r4-3-gain18-20261010/build-result.json)，生产 `App/application/s4_site.c` 保持输出禁用。对比旧 15°/s 隔离源码清单的 182 个输入，仅新隔离副本的 `App/application/s4_site.c` 不同：`.assist_gain` 从 `0.09` 调到 `0.18`，并将原来按完整正弦波峰值检查的配置门改为在零点后完整落于单侧单调象限的 `20°–30°` 区间按区间端点保守检查；其他区间仍使用全波峰值检查。原因是原完整波峰门会拒绝本轮候选，尽管本轮有效区间的目标始终低于原上限。**转子侧 20 mNm 命令帽、20°–30° 软件角域、15°/s 试验速度门、10 s 最长会话、1 s 正常撤力均未放宽**。按未校准的 1.64 Nm 模型与名义 8:1，在 25° 的软件转子目标约 15.6 mNm，30° 区间上界约 18.45 mNm；不是实测转矩。旧门上失败、新门上的 [主机探针](../../../build/s4-r4-3-gain18-20261010/build/host-profile-r4-3-gain18/run.log) 通过，且检查 `0.20` 增益与扩大到 90° 均会被拒绝；GCC Debug 构建 0 错误、0 警告。BIN `119036 B`、SHA256 `ca77d711b525df9e042696d83ca811e32623dd507e3f0f5ed4676f8be6846b0b`。

用户确认 24V 关闭、机构承托且 ST-LINK USB 已连接后，执行 [一次备份／刷写／独立读回](../../../build/s4-r4-3-gain18-board-20261010/attempt.json)：板上原 15°/s 镜像前缀匹配，刷入上述 ELF，读回 119036 B 与候选 BIN 一致；USB-only [冷态检查](../../../build/s4-r4-3-gain18-board-20261010/precheck/usb-only.json) 为 `PASS/SAFE_OFF/output_allowed=0`。这些只证明镜像身份与上电前状态。用户确认开启 24V 后状态正常；首次 [原始摘要](../../../build/s4-r4-3-gain18-board-20261010/effect-run1/r4-3-effect-summary.json) 与 [事件](../../../build/s4-r4-3-gain18-board-20261010/effect-run1/r4-3-effect-events.jsonl) 记录，`followprepare` 后第一条位置反馈即转 `FAULT`，`cmd_torque=0/output_allowed=0`，从未发送 `followstart`。用户说明当时动了一下，可能与垂臂零点要求静止有关；串口未给细分故障码，不据此断定唯一原因。24V 关闭后用户确认机构静止无异常。

用户同意同一镜像再试一次。24V 关闭时仅复位开发板，[第二次 USB-only 检查](../../../build/s4-r4-3-gain18-board-20261010/precheck2/usb-only.json) 为 `PASS/SAFE_OFF`，无需重刷；用户再确认 24V 开启后状态正常。第二次 [原始摘要](../../../build/s4-r4-3-gain18-board-20261010/effect-run2/r4-3-effect-summary.json) 与 [事件](../../../build/s4-r4-3-gain18-board-20261010/effect-run2/r4-3-effect-events.jsonl) 中 RAM 垂臂零点已取得；约 21.2° 的零目标段持续约 1.03 s、有 12 条新鲜角度样本。`followstart` 接受 10 s 会话；`phase=7/cmd_torque=1/output_allowed=1/fault=0` 与 5 条新鲜角度样本持续到约 21.33°，但从启动到故障只有约 0.70 s，高音后的观察约半秒，未完成预定 3 s。READY 的 `model_mNm≈594` 按增益 0.18、名义 8:1 只对应约 13.4 mNm 转子软件目标；本轮未读回实际驱动目标，且约 21° 的姿态与上一轮约 25° 不同，不能直接比较两轮手感。

断电后未复位 RAM 的 [网关](../../../build/s4-r4-3-gain18-board-20261010/fault-read-run2/gateway-232b.bin) 和 [FOLLOW 状态](../../../build/s4-r4-3-gain18-board-20261010/fault-read-run2/status-228b.bin) 按本轮 ELF 布局解码为 `FAULT/HEALTH/SAFETY_SPEED`，最后关节速度反馈约 `23.69°/s`，超过 15°/s 试验门；故障撤权记录 `stop_attempted=1/stop_accepted=1/purge_ok=1/stop_delivery_failed=0`；这些标志不是独立的停帧 TXOK 或机械停转证明。脚本补发 `followstop` 收 `ERR fault`，符合故障后不能再正常取消。反馈速度没有独立测速；用户不记得高音后是否继续手动移动，不能将越限归因于人或电机。用户确认 24V 已关闭、机构静止无异常，且表示“助力还是不够感觉”。该手感如实记录，但本轮未形成稳定 3 s 比较，也未读到本轮实际驱动输入力矩或机械托力，故**提高后的减负效果仍未证实，S4 效果验收未完成**。当前板上保留增益 0.18 的隔离镜像，故障态需在 24V 关闭下冷启动后才能进行新会话；本轮不继续带动力尝试，也不据未完成短窗提高 20 mNm 上限。

### 2026-10-10｜同一 0.18 镜像完整短窗与速度时序

本次仅新增主机端 [`capture_effect_speed_once.py`](capture_effect_speed_once.py)：沿用原效果脚本的准备、听觉提示、一次 `followstart`、约 3 秒观察、主动停止和故障撤权路径；从固件既有 `s3feedback`、`s3properties` 读取缓存转子位置／速度与驱动报告的 `INPUT_TORQUE` **目标**。脚本 `--offline-check` 通过；未改固件或重刷。试验前用户确认 24V 关闭、机构承托、ST-LINK USB 连接并许可同镜像短测；断电复位及 [USB-only 检查](../../../build/s4-r4-3-gain18-board-20261010/precheck3/usb-only.json) 为 `PASS/SAFE_OFF`，BIN／独立板上读回 SHA256 均为 `ca77d711b525df9e042696d83ca811e32623dd507e3f0f5ed4676f8be6846b0b`。用户上电约 5 秒后确认姿态正常。[原始摘要](../../../build/s4-r4-3-gain18-board-20261010/effect-speed-run3/r4-3-effect-summary.json)、[逐命令事件](../../../build/s4-r4-3-gain18-board-20261010/effect-speed-run3/r4-3-effect-events.jsonl) 与主机日志位于 H 盘同目录；原始摘要内预留的操作者观察空字段不改写，下面的观察来自用户试后回复。

三短音后到长音的缓慢前抬约 6.83 秒，70 次 `followstatus` 均为 `phase=5/cmd_torque=0/output_allowed=0`。首次进入约 20°距长音仅约 91 ms；固件要求先在小区间站稳 200 ms，之后才发送零目标、转矩模式和闭环准备帧，所以这段**尚无电机准备帧，也无重力补偿命令**。不能把这段主观阻力归因于正向补偿“顶着手”。这 70 次查询提供离散角度，按主机查询时刻作位置差分约 6.83 秒平均 `3.38°/s`，个别相邻点算到约 `16.3°/s`；查询时刻并非属性采样时刻、角度又量化为 1 mrad，不能把该差分与固件 15°/s 的原始反馈速度门直接等同。全程无故障。

长音后 READY 的零目标段约 1.03 秒：9 条新鲜角度样本 `22.689°–22.746°`，缓存 `INPUT_TORQUE` 目标读回全为 0，所采反馈速度最大绝对值约 `1.72°/s`。随后 FOLLOW 补偿段约 3.11 秒：25 条新鲜角度样本 `22.836°–22.948°`；缓存目标回读从 `0.004000 Nm` 逐步上升，到约 0.97 秒达 `0.014385 Nm`，其后维持约 `0.01437–0.01439 Nm`；所采反馈速度最大绝对值约 `1.80°/s`。位置、速度和目标请求龄均不超过 26 ms，目标序号与位置序号相同或差 1。`s3feedback` 控制台的 `fresh=0/out=0` 使用旧 S3 默认 site 口径，不能覆盖本轮 S4 的新鲜状态；此处以属性请求／回复时刻和 S4 `phase=7/fresh=1/cmd_torque=1/output_allowed=1` 联合判读。目标回读是驱动报告的设定值，**不是电机轴实测力矩或手部托力**。脚本主动 `followstop` 经渐撤到 `phase=3/reader=7/stop=1/fault=0/cmd_torque=0/output_allowed=0`，随后新鲜 IDLE 心跳年龄 59 ms、肩 CAN 错误／bus-off 无增加。用户确认断电后机构承托、静止无异常。

用户反馈：高音后保持同一扶持点、没有主动继续抬动，但与低音零目标段比较**没有太大的省力感觉**；三短音后缓慢前抬时感觉“有点阻力”。阻力发生在补偿开始前。原隔离配置的 `direction=+1/sign_g=+1`、正转矩命令和旧 +20 mNm 微脉冲同向转子变化支持软件命令符号一致，但缺独立机械托力／方向测量，不能凭这段手感证明或否定实际助力方向。用户已被更正告知：前抬时尚未进入闭环准备，先前口头称“已进入闭环转矩模式”不准确。当前可重复的结论是**0.18 增益确实建立了约 14.4 mNm 转子目标并稳定维持约 3 秒，速度越限本轮未复现，主观减负仍不可辨**。不把旧越限归因于操作者，也不因本轮无故障称根因已查明；不继续提高增益或命令帽。R4-3 效果与 S4 台架效果验收未完成。

补充复核上一轮：故障后保存的 [FOLLOW RAM](../../../build/s4-r4-3-gain18-board-20261010/fault-read-run2/status-228b.bin) 之 `S4FollowStatus.observation.properties` 在本轮 ELF 布局的转子目标 float32 字段（offset 68）为 `0.013000001 Nm`，属性请求与回复时刻均为 `33511 ms`，故旧文“本轮未读回实际驱动目标”指当时尚未解出；现已取得**目标读回**，仍没有实际机械转矩。旧轮故障速度 `23.69°/s` 与位置 40 ms 增约 `0.51°` 同向，不能认作孤立一个速度字段噪点，也不能仅凭同源反馈区分人推与电机带动。本轮原始采样峰值约 `1.80°/s` 只涵盖每约 0.12 秒一次的主机样本，不代表每个固件属性周期的数学最大值；固件本轮没有触发 15°/s 保护。用户随后在 24V 关闭、机构承托下沿相同方向小幅前抬，反馈“断电后明显更轻”；因此阻力与本次带电状态有关，但尚无证据确认具体是驱动 IDLE 行为、其他电气拖滞还是机构条件差异。下一步优先核对带电 IDLE 状态与阻力来源，不因该感觉直接更改重力增益、力矩符号或速度门。

### 2026-10-10｜S4 收尾主机准备，待台架窗口

现有 [`capture_effect_speed_once.py`](capture_effect_speed_once.py) 新增仅由 `--run --closeout` 启用的流程，旧 `--run` 保留。它从操作者确认 24V 关闭、机构承托的冷态 `SAFE_OFF` 开始；用提示音和主机时间戳记录断电与带电未准备的同点缓慢前抬。带电 IDLE 只查询状态、心跳和肩 CAN 计数，不调用会占用本次 reader 的 `followobserve`。若状态异常或操作者报告明显增阻，脚本保存记录并停在 `followprepare` 前；MCU 无 24V 物理输入，供电与阻力由操作者确认。

若带电 IDLE 正常，操作者回垂臂后才进入一次 RAM 零点准备；在约 25°扶稳，记录约 2 秒 READY 零目标与约 3 秒 FOLLOW 非零目标，并在同一 10 秒会话内轻缓推到邻近姿态、停扶观察，主动 `followstop`。串口读回的模式、`INPUT_TORQUE`、角度和时间戳只能说明控制状态与力矩目标，不能代替扶持力。视频应同时拍下 0.1 N 刻度量具、关节角标记并录入提示音；固定肘姿态、负载、扶持点和实测力臂。事后只有扶持力下降超过两段稳态波动且至少跨两个刻度，才记为可辨的定量减负；轻推另核是否明显对抗、自行加速、振荡、触边，以及停在新姿态后支撑是否返回。

本次固定使用已刷入的增益 0.18 隔离镜像：`build/s4-r4-3-gain18-20261010/build/Debug/Exoskeleton_F446.bin` 与独立板上读回 `build/s4-r4-3-gain18-board-20261010/postflash-119036.bin` 在准备时只读复核均为 119036 B、SHA256 `ca77d711b525df9e042696d83ca811e32623dd507e3f0f5ed4676f8be6846b0b`。首次现场证据目录为 `build/s4-r4-3-gain18-board-20261010/closeout20261010-run1/`。脚本离线自检通过；准备时尚无上电、出力或新测力结果。

现场开始前须重新确认 24V 关闭、机构承托、接线未变、测力点固定、断电手段可触及；填写视频标识、实测力臂和量具标识，并在新空目录中保存证据。展示镜像和原固件边界 `20°–30°`、转子侧目标帽 `20 mNm`、速度门 `15°/s`、会话 `10 s`、末段撤力最多 `1 s` 后，取得当次真实上电与 `followprepare`／`followstart` 许可。异常时承托并停止动作；脚本在准备后最多尝试一次 `followstop`，必须见到后续 STOPPED 和新鲜 IDLE 心跳才算软件停止链完成，停止回复本身不证明物理卸力。停止未确认则由操作者断 24V；正常结束也需操作者关 24V 并确认。水平位托起、运行中人工切 24V、故意失联与 watchdog 试验不属于本窗口。

以下是当时用于第四轮的现场命令，现仅供证据追溯；`closeout20261010-run4/` 已有原始文件，不得复用或覆盖：

```powershell
$root = 'H:\Desktop\Surgery_exoskeleton-git\firmware\Exoskeleton_F446'
$hostTmp = Join-Path $root 'build\s4-closeout-host-check-20261010\tmp'
New-Item -ItemType Directory -Force -Path $hostTmp | Out-Null
$env:TEMP = $hostTmp; $env:TMP = $hostTmp; $env:TMPDIR = $hostTmp; $env:PYTHONPYCACHEPREFIX = $hostTmp
$videoRef = Read-Host '视频标识'
$leverArmMm = [double](Read-Host '实测扶持力臂 mm')
$forceTool = Read-Host '量具标识'
python -B (Join-Path $root 'docs\bench\s4-r4-3-20261009\capture_effect_speed_once.py') --run --closeout --direct-idle --bin (Join-Path $root 'build\s4-r4-3-gain18-20261010\build\Debug\Exoskeleton_F446.bin') --readback (Join-Path $root 'build\s4-r4-3-gain18-board-20261010\postflash-119036.bin') --image-sha256 ca77d711b525df9e042696d83ca811e32623dd507e3f0f5ed4676f8be6846b0b --out-dir (Join-Path $root 'build\s4-r4-3-gain18-board-20261010\closeout20261010-run4') --video-reference $videoRef --support-lever-arm-mm $leverArmMm --force-tool-label $forceTool
```

这里的实测力臂是肩轴到量具施力作用线的**垂距**；视频还应记录施力方向，不能只量肩轴到抓握点的直线距离。

### 2026-10-10｜收尾首轮在断电段后中止

用户确认 24V 关闭、机构承托、接线和板上镜像未变，并在 24V 关闭时按 NUCLEO RESET 冷启动。首轮脚本读取 `SAFE_OFF/reader=1/output_allowed=0/cmd_torque=0`，随后在断电段查询 `followstatus` 和 `status`；原始 [摘要](../../../build/s4-r4-3-gain18-board-20261010/closeout20261010-run1/r4-3-effect-summary.json) 与 [逐命令事件](../../../build/s4-r4-3-gain18-board-20261010/closeout20261010-run1/r4-3-effect-events.jsonl) 保留不改。事件显示 8 秒断电段结束后的确认门超时，摘要为 `FAIL_OR_INCOMPLETE`、`prepared=false/followstart_attempted=false/followstop_attempted=false`；全部 109 条串口记录仅有 `followstatus` 55 条和 `status` 54 条，没有 `followprepare`、`followstart` 或出力命令。

用户说明未做断电前抬，未弄清何时开始和结束；试后确认 24V 仍关闭、手臂已回垂臂并承托。因此本轮**没有断电测力结果，也未进入带电 IDLE 对照或补偿段**。脚本只在断电段开始发一声提示，段末没有结束音；下一轮先补清楚音频边界并离线检查，再用新的 `closeout20261010-run2/` 空目录。首轮目录不可复用，不能把它的空白 8 秒写成测力对照。

下一轮主机提示已修订：断电 IDLE 以**单低音**开始 8 秒、**双低音**结束；带电未准备 IDLE 以**单高音**开始、**双高音**结束。每段需在开始音后缓慢前抬、同点比较并回到垂臂，结束音之后的动作不计入该段。四个人工确认门放宽到 120 秒，均在 `followprepare` 前，超时仍中止；最终带电 IDLE 确认门的 Y 经最后一次只读健康检查后立即授权 `followprepare`。READY/FOLLOW 的短会话时序不变。H 盘环境下两次离线自检退出码均为 0，独立只读复核未见此改动引入高优先级安全回归；这只验证主机分支，不证明物理动作或测力。新目录 `closeout20261010-run2/` 已创建并核实为空，BIN 与独立板上读回仍各 119036 B 且 SHA256 一致。新一轮开始前须重新在 24V 关闭、机构承托下冷启动 MCU，并由操作者理解声标后确认。

### 2026-10-10｜收尾第二轮再次停在断电段

用户确认听懂当时的单／双低音提示、24V 关闭、机构承托、量具和角度标记入镜且视频开始录制，在 24V 关闭下再次按 RESET。脚本的 [第二轮摘要](../../../build/s4-r4-3-gain18-board-20261010/closeout20261010-run2/r4-3-effect-summary.json) 为 `FAIL_OR_INCOMPLETE`，8.027 秒断电段后操作者答“未完成或有异常”，确认门收到 N，`prepared=false/followstart_attempted=false/followstop_attempted=false`。逐命令 [原始事件](../../../build/s4-r4-3-gain18-board-20261010/closeout20261010-run2/r4-3-effect-events.jsonl) 仅含 `followstatus` 54 条、`status` 53 条，并记录单低音后的开始和双低音前的结束时间标记；未发 `followprepare`、`followstart`。用户随后表示仍不理解量具与前抬如何配合，未提供可用的断电测力读数或完成姿态证据；不能把第二轮视为阻力对照。试后用户确认 24V 一直关闭、手臂已回垂臂承托、无异常。用户提出直接查带电未准备的 IDLE；断电段原本只为同条件阻力对照，并非出力前置安全门，因此后续若跳过它只能得到带电 IDLE 定性观察，不能宣称本次完成断电／上电定量差值；暂不自动进入第三轮或出力。

用户进一步选择“带电 IDLE 正常则同轮继续 FOLLOW 测力”，并确认量具可在约 25°停稳时沿固定方向承担主要扶持力，视频能连续拍到稳定读数。为此脚本新增**显式** `--direct-idle`：冷态 `SAFE_OFF` 与 24V 关闭仍需确认，只跳过断电前抬并在摘要标记 `off_idle.comparison_skipped=true`；用户确认上电后持续只读检查健康 IDLE，单高音开始带电段，完成前抬、停稳读数和回垂臂后才按 Y 结束并听双高音，最长 120 秒，N/Esc／超时／状态异常均零 `followprepare`。随后有独立的最终 Y 门，明确其经最后只读检查后立即授权 `followprepare`；READY/FOLLOW 与原停止链不变。H 盘离线自检和独立只读审查未见 P0/P1，未开串口或上 24V；键盘模拟及物理回位仍须现场确认。下一轮用新的 `closeout20261010-run3/` 空目录。

### 2026-10-10｜直接 IDLE 首轮遇主机时戳误判，准备前中止

第三轮在 24V 关闭、机构承托下冷启动，视频标签 `s4-closeout-20261010-video3`；用户上电后确认静止正常。脚本先读到健康新鲜 IDLE 心跳，并发出单高音，但首次持续检查报“Powered IDLE heartbeat not yet fresh”，在 `followprepare` 前退出。用户随即确认 24V 已关闭、手臂承托，且尚未前抬、无异常运动或阻力。因此本轮没有有效 IDLE 前抬或测力，更没有 FOLLOW。原始 [摘要](../../../build/s4-r4-3-gain18-board-20261010/closeout20261010-run3/r4-3-effect-summary.json) 保留 `FAIL_OR_INCOMPLETE`、`prepared=false/followstart_attempted=false/followstop_attempted=false`；[事件](../../../build/s4-r4-3-gain18-board-20261010/closeout20261010-run3/r4-3-effect-events.jsonl) 中仅有 `followstatus` 53 条、`status` 52 条、`shoulderfeedback` 26 条，无运动命令。

根因已由连续原始回复定位：`status` 先返回 `uptime_ms=49233`，随后 `shoulderfeedback` 返回 `hb_ms=49237/state=1/error=0/output_allowed=0`。心跳在两次非原子查询间更新；原主机代码做 `(49233-49237) & 0xffffffff`，下溢为巨大“年龄”，误判为过期，不能将其写成驱动心跳真正失联。先以这一时序构造离线失败复现（修前退出 1），再仅改主机 `closeout_idle_guard`：心跳回复后重读 `status`，再次确认 `output_allowed=0`，用较晚的 MCU 时间计算年龄，保留原 150 ms 门槛。修后 H 盘离线自检退出 0，覆盖 255 ms 真实过期及输出许可翻转仍被拒绝；未改固件、增益、安全上限或接线。第三轮目录不可复用，下一轮使用 `closeout20261010-run4/`。

### 2026-10-10｜收尾第四轮在 20° 下边界准备时故障

用户重新确认 24V 关闭、垂臂承托、接线和镜像未变后冷启动并录制视频 `s4-closeout-20261010-video4`；量具标称 0.1 N 刻度，在肘轴附近垂直于肩轴—肘轴连线施力，力臂约 350 mm。采用 `--direct-idle`，未做断电前抬。用户确认上电静止正常；带电未准备 IDLE 段结束时曾报告“无明显增阻”，但事后明确补充“第一次不上idle我没有做好”。按其纠正，本轮 IDLE 前抬不构成有效阻力对照，原口头手感不能用来判断带电 IDLE 阻力；断电／带电差值更未测。原始 [摘要](../../../build/s4-r4-3-gain18-board-20261010/closeout20261010-run4/r4-3-effect-summary.json) 和 [逐命令事件](../../../build/s4-r4-3-gain18-board-20261010/closeout20261010-run4/r4-3-effect-events.jsonl) 保留 `FAIL_OR_INCOMPLETE`，视频及量具稳态读数尚未取得供判读。

用户另行确认可继续取零和 FOLLOW 测力后，脚本发一次 `followprepare`；RAM 垂臂零点成功，`zero=1`。三短音后用户开始前抬，串口最后成功的 `followstatus` 仍为 `phase=5/reader=2/cmd_torque=0/output_allowed=0`，角度四舍五入为 `349 mrad`；约 91 ms 后转为 `phase=4/reader=6/fault=1`。脚本只补发一次 `followstop`，收到 `ERR fault`；**没有 `followstart`、READY 稳态、FOLLOW 输出或减负测力**。用户随后确认已关闭 24V、机构静止承托，前抬时无明显对抗、自走或异常。

在用户确认 NUCLEO USB 持续供电且未复位后，以 STM32CubeProgrammer SWD Hot Plug **只读**保存该版 ELF 对应的 [网关 RAM](../../../build/s4-r4-3-gain18-board-20261010/fault-read-run4/gateway-232b.bin)、[FOLLOW RAM](../../../build/s4-r4-3-gain18-board-20261010/fault-read-run4/status-228b.bin) 和 [解码](../../../build/s4-r4-3-gain18-board-20261010/fault-read-run4/decoded-run4.json)。两次读取长度、退出码与工具成功提示均核实；先后读取并非原子快照。按同一 gain18 ELF 的 DWARF／map，网关为 `OUTPUT_FAULT/OUTPUT_REASON_HEALTH/SAFETY_RANGE`，不是先前另一轮出现的 `SAFETY_SPEED`。故障前有效关节反馈为 `0.34897232 rad=19.99464°`，比软件 20° 下限低约 `0.00536°`；同期速度约 `-1.03°/s`，远低于 15°/s 门。源码在 `OutputGateway_PollPreparation()` 中对已提交准备的每次检查调用 `SafetySupervisor_CheckBeforePrepare()`；后者对位置小于下限返回范围故障。本轮 `prepare_ms=152878`、`last_step_ms=152957`，准备约 79 ms 后故障。网关 RAM 的 `armed_ms=0/follow_session_used=0/target=0/applied=0` 与主机无 `followstart` 一致；`stop_attempted/stop_accepted/purge_ok/stop_delivery_failed=1/1/1/0` 仅是软件撤权记录，不是独立的 CAN TXOK 或机械停转证明。0.005° 的软件越界不能独立证明实物关节越界、操作者回摆还是反馈／零点误差；它足以解释本次固件按当前反馈退出准备。继续保持 24V 关闭，不把本次记作效果试验，不自动更改安全角域、速度门、增益或命令帽，也不立即开展第五轮。

### 2026-10-10｜收尾第五轮在约 21° 提前 READY，主机主动中止

用户确认 24V 关闭、垂臂承托、接线和板上固件未变后冷启动并录制视频 `s4-closeout-20261010-video5`；隔离 BIN 与独立板上读回各 119036 B，SHA256 均为 `ca77d711b525df9e042696d83ca811e32623dd507e3f0f5ed4676f8be6846b0b`。本轮按用户选择跳过重复断电前抬；带电但未准备的 IDLE 前抬和回位由用户完成，报告无明显异常。该段脚本只读监看约 20.97 秒，心跳及状态记录正常，但不采量具力值、也不能独立证明实物回位；没有断电／带电定量差值。用户再次确认垂臂承托后，脚本发一次 `followprepare` 并取得 RAM 零点。原始 [摘要](../../../build/s4-r4-3-gain18-board-20261010/closeout20261010-run5/r4-3-effect-summary.json) 和 [逐命令事件](../../../build/s4-r4-3-gain18-board-20261010/closeout20261010-run5/r4-3-effect-events.jsonl) 保留 `FAIL_OR_INCOMPLETE`。

三短音后，用户表示一直朝约 25°连续前抬、没有刻意在 20°–21°停住。主机采到新鲜关节反馈从约 2°逐步上升；约 21.03°时首次 `phase=6/READY`，此前该读数附近约 0.27 秒变化很小。固件设计是在 20°–30°内新鲜反馈满足 200 ms 站稳条件即提交准备，**不等待 25°**；主机收尾脚本却要到 24.5°才发长音。两道条件不一致是本轮主机主动中止的直接原因；没有逐周期内部速度／站稳状态或可判读外部角标，不能把反馈平台判为用户实际停顿，也不能断定机械角度恰为 21°。本轮无固件故障，READY 状态仍为 `cmd_torque=0/output_allowed=0/fault=0`。主机没有发长音、`followstart` 或取得两段测力数据；补发一次 `followstop` 得到 `idle_submitted; observing_stop` 回复，未在日志中进一步确认 STOPPED 和新鲜 IDLE 终态。用户随后确认已关闭 24V、机构静止承托，前抬无明显异常。

用户明确要求后续仍在约 25°做零目标／FOLLOW 测力，量具只在停稳时读、视频事后对齐，并且异常即结束、不在同一条件重复。故不把约 21° READY 改称约 25°对照，也不只为继续试验而放宽角域、速度门、增益或输出帽；下一次现场窗口前须先解决约 25°测力目标与固件“区间内任意站稳即 READY”的流程冲突，并明确新的可审核条件。本轮没有持久主机或固件代码变更，板上镜像未重刷；S4 低姿态基础联调旧结论不变，定量减负和 FOLLOW 重定位仍未证实。

### 2026-10-10｜第五轮后主机离线准备，尚无新现场结果

用户确认下一轮跳过断电前抬；带电但未准备的 IDLE 阶段由操作者按自己的节奏缓慢前抬、停稳观察并回垂臂承托，确认状态正常后，才在同一窗口另行确认并执行 RAM 会话取零。随后仍以约 25°为零目标／FOLLOW 测力姿态，停稳后让量具沿固定方向承担主要扶持力；约 2 秒 READY 零目标和约 3 秒 FOLLOW 补偿的读数由视频事后对齐判读，不要求操作者在出力中口报数值。本轮测力后主动正常 `followstop`；原计划的小范围轻推重定位本轮跳过，保持未测，不能据此称 FOLLOW 重定位通过。

主机早 READY 守卫已在离线脚本中新增：固件若在约 25°长音前进入 READY，先核新鲜反馈、闭环心跳 `state=8`、`control_mode/input_mode=1/1` 及驱动读回的精确零 `INPUT_TORQUE` 目标，条件持续成立才允许继续缓慢前抬；反馈到达或跨过 `24.5°` 时发长音提示停扶，后续基线须处于 `24.5°–25.5°`。两秒零目标基线增加名义关节角跨度 `≤0.25°`、反馈峰值速度 `≤2.5°/s` 的姿态筛查，扶持力是否稳态仍以视频判断；任一条件失效即中止，不因提前 READY 放松固件安全角域或允许提前 `followstart`。主机已新增仅显式启用的 `--measure-only`：在两段测力后跳过轻推并主动正常停止，不改变旧 `--run` 或未指定此选项的 `--closeout` 路径。早 READY 和测力专用分支在 H 盘离线自检退出 0，独立审查未见 P0/P1；这只支持主机软件分支，尚未接真实串口、上电或验证物理托力与停止。

隔离镜像、增益 `0.18`、固件 `20°–30°` 区间、转子侧 `20 mNm` 命令帽、`15°/s` 速度门、10 秒会话及停止办法均不变；候选 BIN 与独立板上读回各 119036 B、SHA256 同为 `ca77d711b525df9e042696d83ca811e32623dd507e3f0f5ed4676f8be6846b0b`。新 `closeout20261010-run6/` H 盘目录已建且为空；本次仅做主机离线准备和文档核对，未给 24V 上电、未刷机或进行第六轮测力。第五轮原始 `FAIL_OR_INCOMPLETE` 与未观察 STOPPED 的边界保持不变。

### 2026-10-10｜收尾第六轮停在主机内侧角度门，未进入测力

用户授权同一已刷、独立读回的增益 `0.18` 隔离镜像和未变的 `20°–30°`、`20 mNm`、`15°/s`、10 秒边界；本轮使用 H 盘独立 [原始摘要](../../../build/s4-r4-3-gain18-board-20261010/closeout20261010-run6/r4-3-effect-summary.json) 与 [事件](../../../build/s4-r4-3-gain18-board-20261010/closeout20261010-run6/r4-3-effect-events.jsonl)，原始 `FAIL_OR_INCOMPLETE` 不改。脚本显式使用 `--direct-idle --measure-only`；带电未准备 IDLE 段约 32.69 秒、177 次只读样本后，操作者确认前抬和回垂臂已完成、状态正常。主机不能独立感知实物回位或读取量具力值。随后用户另行确认，脚本发一次 `followprepare`，RAM 垂臂零点建立。

准备起约 9.925 秒时，第一条 `phase=6/READY` 为 `seq=496/fresh=1/zero=1/fault=0/cmd_torque=0/output_allowed=0`，串口整数回报 `joint_mrad=357`，主机按该整数换算约 `20.45°`，未满足主机严格 `>20.5°` 内侧门，故早 READY 守卫主动中止。串口正角毫弧度文本截断，`357 mrad` 不能证明固件内部精确角或实物角低于 `20.5°`；能确定的是**量化后的主机内侧门拒绝**，不是已证实的固件故障。主机未发约 25°停扶长音，也未开始 2 秒零目标或 3 秒 FOLLOW 测力；没有 `followstart` 或非零补偿目标。异常路径只发一次 `followstop`，收到 `idle_submitted; observing_stop`，原始事件未证实 STOPPED 终态或物理卸力。用户随后确认 24V 已关闭、机构静止承托。

用户反馈“无法准确抬到 25”，随后明确希望研究**电机自动定位约 25°**再测；目前只读核查其模式、源码和本机边界，不能据此写成已有自动抬臂能力或新的现场执行方案。本轮止于主机条件，未形成托力对照，**既不能判补偿效果失败，也不能把测力或重定位记为通过**。第五轮后离线准备记录保留为当时状态，不用第六轮结果倒改其自检结论。

### 2026-10-10｜第六轮后改为区间内稳态测力（仅主机离线准备）

用户重新选择在固件 `20°–30°` 区间内找**能够自然停稳的同一姿态**，取消固定约 25°的手抬目标；此前提出的电机自动定位暂缓。当前只对显式 `--measure-only` 收尾路径修改主机提示与接纳条件：停扶候选宽入口为 `21°–27°`，运行时仍须处于 `20.5°–29.5°` 主机内侧余量，不改变固件 `20°–30°` 安全角域。用户在三短音后缓慢前抬、于区间内自行停稳并在固定点用量具承托；主机先以新鲜缓存角度和速度筛查约 1 秒稳定，**长音表示已确认稳态，接下来继续扶稳测力**。提前 READY 的零目标、模式 `1/1`、闭环心跳及反馈新鲜度守卫继续有效；第六轮的 `357 mrad` 量化读数不能再直接被当作测力入口合格，但允许在这些零输出条件持续成立时继续缓慢进入安全内侧。

同一停扶姿态记录约 2 秒 READY 零目标和约 3 秒 FOLLOW 的量具视频读数，主机筛查两段名义关节角差 `≤0.5°`；原 `0.25°/2.5°/s` 姿态稳定筛查仍用于缓存反馈，实物姿态、扶持点和力值稳定性由视频事后判读。移动时无需读量具，也不要求操作者在出力几秒内报数；两段后主动 `followstop`，本轮不做轻推重定位。固件、板上增益 `0.18` 隔离镜像、`20 mNm` 转子命令帽、`15°/s` 速度门、10 秒会话和停止办法均未改，未重刷或新上电。本节记录的是主机离线方案，**尚无新的测力、净减负或物理停止证据**；第六轮原始 `FAIL_OR_INCOMPLETE` 和 STOPPED 未证实的结论保持不变。

### 2026-10-10｜收尾第七轮冷态门拒绝、第八轮未取得区间姿态

第七轮开始前再次只读核对隔离 BIN 与独立板上读回：各 119036 B，SHA256 均为 `ca77d711b525df9e042696d83ca811e32623dd507e3f0f5ed4676f8be6846b0b`；COM10 仍为同一 ST-LINK VCP。操作者确认 24V 关闭、机构承托并称已按 RESET；脚本首次 `followstatus` 却是 `phase=3/reader=7/seq=497`，与第六轮末尾 `seq=496` 连续，未满足冷态 `SAFE_OFF/reader=1/seq=0`，因此在上电门前退出。操作者随后说明当时忘记按 RESET，并已补按。第七轮 [原始摘要](../../../build/s4-r4-3-gain18-board-20261010/closeout20261010-run7/r4-3-effect-summary.json) 与 [事件](../../../build/s4-r4-3-gain18-board-20261010/closeout20261010-run7/r4-3-effect-events.jsonl) 保留 `FAIL_OR_INCOMPLETE`；无 24V 上电、`followprepare`、`followstart` 或停止命令。

补按 RESET、重新确认实物状态和取得当次许可后，用新空目录做第八轮 `--direct-idle --measure-only`。冷态 `phase=1/reader=1/seq=0` 通过；操作者完成约 40.64 秒带电未准备 IDLE 前抬与回位，报告阻力正常，但脚本不能独立感知回位或量具数值。再经现场确认发送一次 `followprepare`，RAM 零点取在转子缓存 `pos_mrev=3544`。三短音后到约 16.5 秒准备余量耗尽期间，176 条有角度文本的 `followstatus` 均为 `phase=5`，相对 `joint_mrad` 仅 `-13…0`（约 `-0.75°…0°`），`cmd_torque=0/output_allowed=0/fault=0`；未取得 `READY`、稳定长音或零目标／FOLLOW 两段力值。脚本按期限中止，补发一次 `followstop`，收到 `read_cancel_pending; support arm`；**没有发送 `followstart` 或非零补偿目标**。操作者澄清：三短音后确有前抬，抬起后按外部角标估计**一直位于 20°–30°**，但没有停住扶稳；另听到一声短暂电机声。这些观察尚无视频可同步核对，短声不能归因于 S4 出力。操作者确认已关闭 24V；断电后只读复核为 `phase=3/reader=3`（STOPPED）、`output_allowed=0/cmd_torque=0/fault=0`，但不是本轮带动力正常渐撤或新鲜 IDLE 心跳证明。保留第八轮 [原始摘要](../../../build/s4-r4-3-gain18-board-20261010/closeout20261010-run8/r4-3-effect-summary.json)、[逐命令事件](../../../build/s4-r4-3-gain18-board-20261010/closeout20261010-run8/r4-3-effect-events.jsonl) 和 [断电后只读回复](../../../build/s4-r4-3-gain18-board-20261010/closeout20261010-run8/post-abort-readonly.txt)，原始失败状态不改。

操作者在获知取零发生于最后一次确认后的 `followprepare`、三短音之前时，明确补充“那会还在抬”。因此**第八轮取零时未保持垂臂静止，约定的 RAM 零点前提不成立**；软件虽通过驱动读数的站稳判据，却不能独立识别实物垂臂。后续相对角未进入 20°–30°，故 `OutputGateway_Prepare()` 尚未提交，这是本轮未到 READY 的直接流程解释。零点 `3544 mrev` 比第五／六轮的 `2969/2953 mrev` 高 `575/591 mrev`；若驱动位置参考与传动关系跨上电保持一致，名义 8:1 换算约为 `25.9°/26.6°`，与错误取零的解释相容，但**不能用跨轮位置差反推本轮取零的精确实物角度**。断电后缓存模式 `cm=3/im=3` 也不能倒推三短音时驱动产生了什么力。保持断电，不以同一条件继续动力尝试；下一次取零前，先让手臂回到外部垂臂标记、完全停下并保持承托，确认后才通过最后门，三短音后才再次前抬。视频若可取得，再核对取零瞬间的角标和提示音。S4 定量减负和 FOLLOW 重定位均未验证。

### 2026-10-10｜收尾第九／十轮：IDLE 心跳拒绝与准备期速度故障

操作者要求重新开始并授权沿用同一隔离镜像及 `20°–30°`、转子侧目标帽 `20 mNm`、`15°/s`、最长 10 秒限制。候选 BIN 和独立板上读回各 119036 B，SHA256 均为 `ca77d711b525df9e042696d83ca811e32623dd507e3f0f5ed4676f8be6846b0b`；未改固件、参数或接线。第九轮冷态 `SAFE_OFF` 通过；上电确认后的首条心跳为 `state=0/error=0/fault=0/output_allowed=0`，脚本要求 `state=1`，因而在 `followprepare` 前中止。它只读到这一帧，不能确认 `state=0` 是否为上电过渡或与操作者所述上电后移动有关；现有主机等待循环对 `state=0` 会立即拒绝，不会继续观察满 5 秒。原始 [摘要](../../../build/s4-r4-3-gain18-board-20261010/closeout20261010-run9/r4-3-effect-summary.json) 与 [事件](../../../build/s4-r4-3-gain18-board-20261010/closeout20261010-run9/r4-3-effect-events.jsonl) 保留 `FAIL_OR_INCOMPLETE`；本轮没有取零、输出准备、FOLLOW 或测力。

操作者确认断电承托后要求再开始。第十轮独立冷启动通过，上电静止约 5 秒后首条健康 IDLE 心跳通过；带电未准备 IDLE 段约 30.79 秒由操作者完成并报告正常。操作者再次确认外部垂臂 `0°`、静止承托后，`followprepare` 建立 `2905 mrev` RAM 零点；三短音后前抬，串口在 `seq=520/joint_mrad=376`（约 `21.54°`）短暂见 READY、零目标和零许可，随后反馈角仍上升。约 0.42 秒后转 `FAULT`，未发长音、未取得 2 秒零目标基线、未发送 `followstart` 或非零补偿目标。主机补发 `followstop` 收 `ERR fault`，操作者确认已关闭 24V、手臂静止承托且无明显异常。原始 [摘要](../../../build/s4-r4-3-gain18-board-20261010/closeout20261010-run10/r4-3-effect-summary.json) 与 [事件](../../../build/s4-r4-3-gain18-board-20261010/closeout20261010-run10/r4-3-effect-events.jsonl) 保留 `FAIL_OR_INCOMPLETE`。

在 24V 关闭后，SWD Hot Plug 只读保全第十轮同 ELF 的 [网关 RAM](../../../build/s4-r4-3-gain18-board-20261010/fault-read-run10/gateway-232b.bin)、[FOLLOW RAM](../../../build/s4-r4-3-gain18-board-20261010/fault-read-run10/status-228b.bin) 和 [解码](../../../build/s4-r4-3-gain18-board-20261010/fault-read-run10/decoded-run10.json)。按该 ELF 的 map／DWARF，网关为 `OUTPUT_FAULT/OUTPUT_REASON_HEALTH/SAFETY_SPEED`；保留反馈角约 `23.159°`、速度约 `17.939°/s`，超过 `15°/s` 门，角度仍在软件区间内。`armed_ms=0/follow_session_used=0/target=0/applied=0`，停止处理标志 `stop_attempted/stop_accepted/purge_ok/stop_delivery_failed=1/1/1/0`；这些只是软件撤权记录，不是独立 CAN TXOK 或物理停转证明。两块 RAM 顺序读取并非原子快照；同源电机反馈不能单独判定实际手抬速度、驱动自行运动或反馈瞬变。第十轮直接触发门已确认是**准备／零目标 READY 阶段的速度门**。它与本流程要求操作者手动进入测力区间反复冲突；不再按同一动力条件重试，也不盲目放宽速度门、提高增益或命令帽。下一步先离线复核速度保护的阶段语义和停扶交接方案；S4 定量减负仍未证实。

### 2026-10-10｜第十轮后仅调整主机测力门（离线）

按用户要求，仅在显式 `--measure-only` 路径精简主机 `followstart` 前的测力判断：RAM 垂臂零点后，首次进入主机内侧 `20.5°–29.5°` 的新鲜零目标 READY，须经闭环心跳、驱动 `control_mode/input_mode=1/1` 和零 `INPUT_TORQUE` 读回，并在读回后复查 READY；不再额外等主机 1 秒／4 样本、`0.25°` 跨度或 `2.5°/s`。长音现在是**有效 READY 后停止移动并扶稳**的提示，不宣称在响音前已物理静止。随后约 2 秒零目标段仍持续检查零输出／新鲜状态及模式和目标读回，同点缓存角跨度须 `≤0.5°`；`followstart` 前再读闭环心跳与最终 READY，同提示音姿态差须 `≤0.5°`。任一检查失败就中止并走既有撤权／停止路径。旧 `--run`、未指定 `--measure-only` 的 `--closeout` 判据、固件 `20°–30°`／`15°/s`／`20 mNm`／10 秒限制和主动 `followstop` 未改。

对应表第 25／28 行的 [GALAXEA-A1Z 按状态做速度检查](https://github.com/userguide-galaxea/GALAXEA-A1Z/blob/9006107aa8d4334c762b8bf8e8fe40b3f9477717/a1z/robots/arm_robot.py) 和 [BetaRobot 电机使能／限幅结构](https://github.com/itgz123/BetaRobot/blob/c83dd84d6c7717105c948d8d12f2fbb5f4d6b48c/drv/drv_motor/drv_motor_base.h) 只作为分阶段监督的思路参考；均无本机可直接复用的手推摆位交接。本次依据第十轮 RAM 和本机脚本实现改主机检查，没有修改固件速度保护。H 盘临时目录下执行 `--offline-check` 退出 0，覆盖内侧入口、零目标／模式、心跳、基线漂移及最后 READY 故障；**未接串口、未再次上电、未取得新的扶持力或减负证据**。第十轮速度故障发生在主机调用 `followstart` 以前，因此本次主机改动不能单独解决它；继续保持断电，先讨论准备阶段的速度保护与停扶交接，再决定新的动力试验。

### 2026-10-10｜操作者停扶后触发的短时测力分支与第十三轮

第十一轮在操作者继续调整姿态时被旧主机同姿态门中止，第十二轮外部角标据报在区间内，但固件相对角约 `18.56°`，未到 READY。用户据此改为：垂臂静止时先取得 RAM 零点，三短音后自行抬到原 `20°–30°` 区间内、用量具停扶，发消息才触发支撑；约 3 秒后主动撤支撑，再留约 2 秒同点读数。显式 `--run --closeout --static-idle --measure-only --operator-triggered` 只去掉主机额外的角度变化／同姿态起动门，**不改变**固件的 READY、`20°–30°`、`15°/s`、`20 mNm`、10 秒会话和故障撤权。驱动 24V 实物状态仍由操作者确认，主机不能直接测量；`followprepare` 的 RAM 零点必须在真实垂臂取得，固件准备约 20 秒后超时，不可在任意已抬姿态直接取零并等候消息。

第十三轮 [原始摘要](../../../build/s4-r4-3-gain18-board-20261010/closeout20261010-run13-operator/r4-3-effect-summary.json) 与 [事件](../../../build/s4-r4-3-gain18-board-20261010/closeout20261010-run13-operator/r4-3-effect-events.jsonl) 记录：用户停扶时 `phase=6/READY`，主机转发后 `followstart` 返回 OK；约 3.07 秒出力窗口内反馈角约 `25.89°–26.43°`，读回的转子 `INPUT_TORQUE` **目标**最高约 `16.43 mNm`。主机在约 3.27 秒主动 `followstop`，读到渐撤、STOPPED 和 IDLE、`cmd_torque=0/output_allowed=0/fault=0`，随后约 2.05 秒撤支撑读数窗口。软件停止后等待“已关 24V”的键盘门 60 秒超时，原始摘要因此为 `FAIL_OR_INCOMPLETE`；用户稍后在聊天确认已关 24V，不能把这项事后确认倒填成门内成功。两段量具数值仍待用户提供，不能由目标或听到电机声推定减负。此对照是 FOLLOW 对停止后带电 IDLE，尚不能单独分离重力补偿与驱动使能阻力。

第十三轮之后只对主机脚本做了一处离线修订：观察到 STOPPED／IDLE 后立即提示操作者断 24V，物理断电状态另行回报和记录，不再让跨聊天的 60 秒键盘等待决定控制结果。脚本仍不能自行切电或把未回报写为“已断电”。修订后 H 盘临时环境中 `--offline-check` 和 `py_compile` 退出 0；未用该新版本再次进行台架试验。
