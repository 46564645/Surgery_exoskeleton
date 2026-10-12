# S5 R4：肘部 FOLLOW 与 20° 扶持力读数

## 已有现场证据

此前 R4 `+1/2000 mA` 肘单轴隔离镜像完成过[备份、刷写和独立读回](../../../build/s5-r4-force20-board-20261011/attempt.json)：BIN 与当时板上读回的 SHA256 均为 `fa3f93140fe5e19474b69f7ecfa7d0d3f20adf9897fde8c777cf59919df9b309`。其电流指令帽为 `2 A`，不是实测电机电流或关节力矩。该镜像后来已被下述 `4 A` 候选替换。

此前两次现场窗口与本页新脚本须区分：

- [第一次 0°→20° 对照](../../../build/bench-logs/s5-r4-force20-20261011/s5-elbow-force-20261011-220413.jsonl)在约 `20.04°` 发出无辅助读数提示；随后操作者停在约 `22.81°`，旧主机脚本的 ±2° 同姿态门拒绝继续。没有发送 `followstart`，没有辅助读数。操作者报告无辅助力约 `0.4 N`，其具体姿态精度未核实。
- [第二次只测辅助](../../../build/bench-logs/s5-r4-force20-20261011/s5-elbow-F-on-20261011-220807.jsonl)在操作者口述约 20° 的姿态启动一次 FOLLOW；串口记录有 `phase=4/output_allowed=1/cmd_el_mA=2000` 目标、提示音和主动 `followstop`，最终 `phase=7/output_allowed=0`、记录错误为空。**尚无操作者报告的辅助力数值**，因此不能用先前约 `0.4 N` 算减负差值；第二次实体角度也未独立测定。

新脚本 [`measure_elbow_lift_then_follow.py`](measure_elbow_lift_then_follow.py) 按操作者最新要求改为“高音停、随后 FOLLOW 测力”。假串口离线检查已通过；现场曾在非零电流目标出现后约 `0.28 s` 见故障，最新一轮则在准备前因停后反馈角超出主机窗口而退出，**仍没有取得有辅助力读数**。旧 `follow_once.py` 是另一份手动小幅变姿态观察脚本，本轮扶持力读数不用它。

用户后续提出**实际扶持力翻倍**目标。R4 隔离配置现可在构建时显式选择 `1–4000 mA`（R5 仍为 `1–2000 mA`）。`+1/4000 mA` [候选 BIN](../../../build/s5-r4-force40-candidate-20261011/Exoskeleton_F446.bin) 大小 `137484 B`，SHA256 `600b6724d3d6e04a332ab71b8b1cb9565cb72ee457872c78cee70cc8d9631e84`；WindowsDebug 构建退出码 `0`，R4 site、elbow_follow、gateway_pair、gateway_stop 四项主机检查 `PASS`。24 V 关闭时，[刷写记录](../../../build/s5-r4-force40-board-20261011/attempt.json)核对旧 `2 A` 镜像前缀，单次刷入并独立读回与新 BIN 相同的 SHA256，状态 `FLASH_READBACK_MATCHED_USB_ONLY`；[断电只读串口](../../../build/s5-r4-force40-board-20261011/usb-only-readonly.jsonl)为 `ping=OK/phase=1/output_allowed=0`，肘反馈离线与当时 24 V 关闭相符。板上现为该 `4 A` 镜像。首次 4 A 现场窗口未启动辅助；第二次短时读到 `4000 mA` 电流目标后进入故障，**仍没有 `F_on` 的 N 读数**；`4 A` 配置不等于扶持力翻倍或台架通过。

## 新脚本现场记录与边界

- [第一轮](../../../build/bench-logs/s5-r4-force20-20261011/s5-elbow-lift-follow-20261011-222121.jsonl)只取得伸肘零位并发出开始抬臂的两声低音。操作者要求先重启后，脚本中断；本轮没有 20° 高音、`followprepare` 或 `followstart`，没有动力辅助。
- [重启后的第二轮](../../../build/bench-logs/s5-r4-force20-20261011/s5-elbow-lift-follow-20261011-222308.jsonl)在相对伸肘 `20.039°` 发高音，停后反馈为 `22.368°/rpm=0`，固件给出 `READY/zero_el=1`。一次 `followstart` 被接受，随后读到 `RUNNING/output_allowed=1/cmd_el_mA=2000` **电流目标**并发出测力低音；约 `0.28 s` 后下次状态已为 `FAULT/gate=3/reason=3/safety=0/output_allowed=0/stop=1/stop_failed=0`。这次没有持续满计划的 2.2 秒，也没有量具 `F_on` 数值。故障后 `followstop` 回复 `ERR fault`，是已有故障态的回复，不能据此认定停止命令引起故障。
- [最新一轮 22:30:17](../../../build/bench-logs/s5-r4-force20-20261011/s5-elbow-lift-follow-20261011-223017.jsonl)从已复位的 `IDLE/输出关闭` 开始，伸肘零位 `3442 tick`；脚本首次跨过 20° 时在 `2941 tick/22.017°/rpm=-5` 发高音，约 1 秒后的取样为 `2805 tick/27.993°/rpm=0`。后者超出主机 `20°–25°` 窗口，脚本在 `followprepare` 和 `followstart` 前退出；`prepare_attempted=false/start_attempts=0/low_active_cue=null`，本轮无辅助输出、无 `F_on`。两次反馈之间的角度变化已记录，但不能据此认定操作者继续抬起或机构滑动。
- [4 A 镜像现场窗口](../../../build/bench-logs/s5-r4-force40-20261011/s5-elbow-lift-follow-4a-20261011-225015.jsonl)在 `20.522°` 发高音，停后为 `24.521°/rpm=0`；准备到 `READY/zero_el=1`，随后一次读取为 `24.653°/rpm=-5`，`followstart` 返回 `ERR blocked`。最终 `phase=8/reason=2(HEALTH)/safety=5(FEEDBACK)/output_allowed=0/stop=1/stop_failed=0`。没有进入 `RUNNING`，没有测力低音或 `F_on` 读数。`-5 rpm` 名义约 `30°/s`，超过固件 `20°/s` 会话门，与反馈失效故障链相符；串口并未原子记录触发帧，也未独立量得机械速度。操作者报告无异响。
- [4 A 第二次现场窗口](../../../build/bench-logs/s5-r4-force40-retry-20261011/s5-elbow-lift-follow-4a-20261011-230459.jsonl)高音时 `20.962°/rpm=-5`，停稳及 READY 为 `24.785°/rpm=0`。`followstart` 回 `OK`，短时读到 `phase=4/output_allowed=1/cmd_el_mA=4000` 目标并发出测力低音；约 `0.283 s` 后下一状态已为 `phase=8/reason=3(REQUEST)/safety=0/output_allowed=0/stop=1/stop_failed=0`，`followstop` 回 `ERR fault`，无正常 `STOPPED`。操作者明确说**开始助力时**感到有出力，前臂“往伸直方向回一下”，不是助力消失时回弹；没有量具 `F_on` 的 N 值。高音与停稳约 `3.82°` 的差发生在助力启动前，不能解释该启动感受；串口与现有代码也不能单独证明真实机械转向或位置锁定。试后操作者确认 24 V 已关闭，承托静止及 USB 状态尚未直接回答。
- 第二次故障后断电保存的[128 KiB SRAM](../../../build/s5-r4-force40-retry-20261011/fault-ram/sram-128k.bin) SHA256 为 `30EDF9A9CB326F13CD82BDF47C9E77A0FADCDEC7F46269DC5D670A6B885A34D2`；按同版布局核对为网关 `state=3/reason=3/safety=0`，`armed_ms=88364/last_step_ms=88476/axes_last_sample_ms[1]=88475`，`status.joint[1]` 无效且会话角已清零。`last_step_ms=armed_ms+112 ms` 只标出最后正常输出循环，既不是确切故障时间，也不是实测电流；故障后状态不能唯一指出触发分支，不能把会话角清零当根因。

旧 `2 A` 镜像第二轮故障后的[只读状态](../../../build/bench-logs/s5-r4-force20-20261011/usb-after-fault-no-reset.jsonl)曾显示 `FAULT/reason=3/输出关/stop=1`；首次 4 A 窗口是 `reason=2/safety=5` 启动前故障，第二次 4 A 窗口则是短时运行后 `reason=3/safety=0`，都没有故障瞬间触发帧，不能混作同一根因。`stop=1` 表示停止帧获发送接口接受，不等于独立证明机械已停止。主机脚本的离线通过只验证提示和单次命令流程，不能覆盖真实 FOLLOW 故障。

## 已用流程：主机 20°–30° 工作窗口

操作者把实体肘关节伸直作为 `0°`，保持肩姿态、前臂量具测点和施力方向不变。主机从 C620 单圈编码器取该姿态的原始计数，按操作者报告的肘部同轴直连 `1:1` 换算相对角；两声短低音表示可以慢慢屈肘。[2 A 脚本](measure_elbow_lift_then_follow.py)及本轮[4 A 脚本](measure_elbow_lift_then_follow_4a.py)均在首次读到 `20°–30°` 时发**一声高音**，操作者停止抬起。脚本留 `0.7 s` 供停稳，在实际停住的 `20°–30°` 位置调用一次 `followprepare`，由固件取得本次 RAM 零位并等待 `READY`；随后只调用一次 `followstart`。只有读到 `RUNNING/output_allowed=1` 和非零 `cmd_el_mA` 目标才发**一声低音**供读取有辅助力 `F_on`。正常运行时约 `2.2 s` 后主机调用 `followstop` 并记录状态。首次 4 A 现场窗口在启动前故障；第二次到达测力低音但很快故障，均无量具 `F_on` 数值。

主机保留的判断只有本次流程所需项：启动前是 S5 肘单轴 `IDLE/输出关` 且 C620 反馈有效、年龄不超过 `100 ms`；高音首次跨 20° 时的采样以及停后和 `READY` 时的伸肘零位相对角均处于 `20°–30°`。若一个采样直接越过 30°，不发高音也不启动；固件必须给出肘零位 `READY/输出关`，启动后须见非零目标和持续 `RUNNING/输出许可`，最后确认 `STOPPED/输出关`。主机**没有另设 rpm 静止阈值、相对高音角的漂移阈值或反复测力条件**；静止由固件自身的 READY 判定。拒绝、通信失效或运行状态退出即不重试启动，并尝试一次停止。

`20°–30°` 仅为从实体伸肘零位计算的**主机测量工作窗口，不是机械限位**。当前板上 4 A R4 固件以 `followprepare` 时的 RAM 零位为中心约 `±5°`，速度门 `20°/s`，电流指令帽 `4 A`，升流 `20 A/s`，肘反馈超时 `100 ms`，FOLLOW 会话最长 `5 s`、撤力最长 `250 ms`；此前 2 A 镜像除电流目标及指令帽外采用同组限制。主机抬臂等待上限 `120 s`，准备等待 `2.3 s`，非零目标等待 `0.8 s`，停止观察 `2 s`；这些是防止脚本无限等待的时间边界。机构仍须有人承托，操作者可立即切断 24 V。`cmd_el_mA` 是电流目标，提示音和串口状态都不等于实测扶持力；扶持力以量具读数和相应姿态记录为准。

## 调用与记录

脚本固定使用显式 ST-LINK VCP 端口、115200 8N1、无流控，串口请求和响应原字节保存为 H 盘 JSONL；不扫描端口，不刷写，也不切换 24 V。4 A 脚本的假串口检查不会打开设备：

```powershell
python -B .\measure_elbow_lift_then_follow_4a.py --offline-check
```

以下是 4 A 镜像对应的现场入口格式；本轮已在启动前故障，当前暂停再次动力运行：

```powershell
python -B .\measure_elbow_lift_then_follow_4a.py --run --port $vcp --output-dir $hLogDir
```

两个脚本各自将对应镜像的 SHA256 固定写入日志作身份标记；运行时不重新读取板上 Flash，也不作哈希验证。主机窗口已从 `20°–25°` 改为 `20°–30°`；H 盘临时目录下的假串口离线检查包含约 `28°` 停止后完成一次准备／启动／停止、超过 `30°` 不启动，以及反馈过期、准备或启动拒绝、运行目标一直为零的路径。这些仅是脚本离线结果；首次 4 A 现场未启动 FOLLOW，第二次虽短时启动却很快故障，定量测力仍未通过。
