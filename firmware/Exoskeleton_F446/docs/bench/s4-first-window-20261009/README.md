# S4 首次 20°–30° 候选与现场准备记录

**最新状态：**第七、八次已执行 R4-2B 的 3 秒自然到期短窗与运行中主动 `followstop` 短窗；有界结果和原始证据见文末 [R4-2B 记录](#2026-10-09-r4-2b-短时持续输出与停止)。以下首次至第六次叙述保留各次发生时的历史状态，不能代表当前进度。

此目录原先只保存**隔离试验配方**；活动 `App/application/s4_site.c` 仍是输出禁用的默认值，配方脚本本身不烧录、复位、访问串口/CAN 或操作 24V。用户随后就 BIN SHA256 `4210a776…cdc4e8`、20 mNm 命令帽、增益 0.09、20°–30° 和 3 秒窗口给出当次现场许可，同一候选已在 24V 关闭时刷写并独立读回。三轮都只到 `followprepare`：第一与第三轮在准备阶段触发 `SAFETY_SPEED`，第二轮用户未手推，零点后保持约 0°而未 READY。三轮均未 `followstart`，没有持续非零力矩命令或 S4 减负验收。

此后三轮故障的准备时机已作离线修复，新候选 BIN SHA256 `c3a1c620…b0e763` 已刷写并独立读回。新镜像前两次上电会话均取得 RAM 零点，但用户未手动转动，均未进入 READY；第三次手动前屈至约 21.9°时触发软件速度保护；第四次越过 30°候选上限；第五次约 23.4°时到达 READY，但分步执行晚于准备许可期限而故障；第六次手动进入软件小区间并连续取得固件 READY。**依据原始记录，第六轮垂臂取零、手动入区间和固件 READY 的准备测试通过**；用户表示“这个测试已经通过了”并要求停止后续测试，具体范围由上述证据界定。六轮均未 `followstart`；最终确认 24V 已断开、机构承托，没有持续非零力矩或 S4 台架验收。

`trial_s4_site_init.inc` 只替换材料化副本内 `s4_site.c` 的静态初始化段；`S4Site_ObservationReady`、`ProfileReady`、`SessionZeroReady` 等函数逐字沿用当前活动源码。这里的 `true` 是首窗工程假设，不表示相关物理量已实测。冻结的 `.inc` 注释中“尚未许可”是生成快照时的文字；后来的**当次**许可、旧候选三轮及修订候选六轮现场事实与边界以本文件、`verification.json` 和唯一开发日志的追加记录为准。不可把初始化段复制到活动 site，也不可把软件门或准备阶段通过写成持续出力与 S4 台架验收。

## 2026-10-09 现场准备尝试

| 轮次 | 观察与停机 |
|---|---|
| 第一次 | 24V 关闭时备份 512 KB，SHA256 `cab14720…17f1f09c`；前 113184 B 与旧输出禁用镜像 `a170f97c…791cd0` 相符。刷写后独立读回 113500 B，SHA256 `4210a776…cdc4e8` 与候选 BIN 一致；USB-only 和 24V-on IDLE 预检通过（心跳年龄 71 ms）。`followprepare` 成功取垂臂 RAM 零点；用户手动移至候选约 27° 时，网关 RAM 记录 `OUTPUT_FAULT(3)/HEALTH(2)/SAFETY_SPEED(7)`，`prepare_ms=209521` 到 reader stop `209561` 相差 40 ms。反馈速度越过配置的 10°/s 门，但物理速度未独立量测，不归责用户。故障撤权 stop attempted/accepted、purge 均成功，reader 尾窗正常观察；用户随后确认 24V 关闭并承托。 |
| 第二次 | 用户许可继续后，同一 BIN 未重刷，复位与 USB-only 通过，24V-on IDLE 心跳年龄 76 ms。`followprepare` 零点成功；18 秒约 150 次 `followstatus` 一直 `phase=5/zero=1/fresh=1/pos_mrev=2916/joint_mrad=0`，未到 READY。用户后来确认该轮没有手动转动，因此恒定零位与现场一致，不是反馈故障。`followstop` 回 `read_cancel_pending`；用户切断 24V 后约 18 秒的记录为 `phase=3/reader=3/reason=0/output=0/fault=0`。此时陈旧心跳和 reader 错误来自断电后的采样，不记成窗内故障。 |
| 第三次 | 同一 BIN 未重刷，复位与 USB-only、24V-on IDLE 预检通过（心跳年龄 92 ms）。`followprepare` 再次取得 RAM 零点；用户手动移动机构，尚未 READY 即出现故障。未复位 RAM 的 232 字节快照 offset 120/121/122 为 `03/02/07`，即 `OUTPUT_FAULT/HEALTH/SAFETY_SPEED`；`prepare_ms=101850`、phase 时间 `101890`。是否已经提交 ClosedLoop 仍未知，不把该次手动变化写成电机主动运动。 |

用户补充三轮都未听到 MotorWizard 启动时的嗡声。该感知记录与“从未执行 `followstart`、未见持续非零力矩命令”并列保留；它自身不能证明驱动器内部闭环状态。首次故障原始 RAM、二/三轮采集及第三轮 RAM 位于 `firmware/Exoskeleton_F446/build/s4-first-window-board-20261009/`；哈希、路径和状态见 `verification.json`。下一步先核零目标、模式、ClosedLoop 准备帧是否实际到达及 SPEED 判据；不直接重复出力或宣称 S4 已开始。


## 参数、依据和未测项

| 字段或确认位 | 首窗值与依据 | 验证状态 |
|---|---|---|
| `mapping.direction`、`rotor_per_joint`、`mapping.confirmed` | `+1`、名义 `8:1`。厂家 GIM6010-8 标称减速比、用户确认 GDS68 输出端直连肩轴；既有 A→B 转子增加和一次 +20 mNm/100 ms 微脉冲支持正向趋势。 | **离线候选名义映射**；没有新增独立角度／同期转子配对，也不宣称实际总比实测。 |
| `rotor_reference_confirmed[0]`、`session_zero_on_prepare` | 静态 `false`、`true`。自然垂臂时 `followprepare` 等两组新鲜完整七属性建立 RAM 零点。 | 代码及主机用例通过；三轮 `followprepare` 均取得本次 RAM 零点，但都未进入 FOLLOW。ExoKit 的后伸 tare 零点不移植。 |
| `gravity`、`assist_gain` | 关节侧 `1.64 Nm`、相位 `0`、符号 `+1`、增益 `0.09`。1.64 Nm 是用户选定的部分负载粗估；符号按本机正向与重力补偿几何作首轮推定。 | **试验模型**，不是实测负载转矩、全行程上界或效果证明。 |
| `limits_confirmed`、速度 | 垂臂前屈 `20°–30°`，速度上限 `10°/s`。用户确认该区间可用且两端有目视停机余量；ExoKit 设计角域只作结构参考。 | 区间为用户确认的首窗候选；实际停机距离和映射误差未测。 |
| `max_motor_command`、`motor_slew_per_s` | 独立转子侧命令帽 `0.020 Nm`；缓升 `0.020 Nm/s`。前者沿用旧微脉冲幅值作为首个持续命令帽；后者是保守工程候选。 | 命令上限可在软件检查；旧微脉冲未验证持续力矩或实际卸力。 |
| `feedback_confirmed`、`protocol_confirmed`、`trusted_feedback_axes` | 只启用肩侧。沿用 R4-1 一次 8 秒输出禁用观察：404 组完整七属性、CAN 错误增量为零；驱动器协议仍用 SIMPLE 转矩命令。 | 只读链有界观察通过；持续读发并行尚未测。 |
| `feedback_timeout_ms`、属性 reply/period/window | `150 / 20 / 20 / 30000 ms`，沿用 R4-1 只读配置。七项逐项允许的上界为 `7×20=140 ms`，距离反馈期限仅 `10 ms`。 | 8 秒样本均值不能证明最坏并行时延；任一超时应停止首窗查因，不直接放宽门槛。 |
| `service_limit_ms`、`max_request_ms`、发送间隔 | `100 / 250 / 50 ms`。250 > 7×20 + 50 = 190，50 < 100。 | **离线预算**；周期性输出竞争 CAN 读事务的最坏值未测。 |
| 准备、会话与撤力 | `prepare=20000 ms`（含人工移入区间）、`session=3000 ms`、`ramp=1000 ms`、`stop_submit_budget=250 ms`、`release_slew=0.040 Nm/s`。 | 离线门自洽；首次 3 秒会话最多约 2 秒在正常撤力开始前，不足以做 0.5 秒缓冲加 5 秒重定位判据。 |
| `shoulder_standstill`、停止尾窗 | span `0.001 rev`、速度 `0.02 rev/s`、dwell `200 ms`、freshness `150 ms`；尾窗 `1500 ms`。 | 阈值是试验候选；第一轮仅见故障后的 reader 尾窗观察，正常持续输出撤力、零力／IDLE 交付及真实卸力未验收。 |
| `stop_method_confirmed`、`protection_confirmed`、`timing_confirmed` | 为离线评价首窗候选暂填：用户确认 24V 切断可及时、20°–30°有余量；代码限幅、超时、故障撤权及下表预算存在。 | 这些静态确认位本身不是许可或实测保护性能；用户后续只许可了上述具体现场轮次。24V 切断已在无持续非零输出的尝试后由用户执行，不证明有力矩时实际卸力；编码器站稳不是独立断电信号。 |

模型最坏转子命令为 `1.64×0.09÷8=0.01845 Nm`，低于独立 `0.020 Nm` 帽；在本次 20°–30° 正弦区间，模型命令仅约 `6.31–9.23 mNm`。若感受不到减负，只记录未通过，不自动增加输出。

配置门的离线余量：`stop_submit_budget=250 ≥ service_limit=100 ms`；尾窗下界 `2×7×20 + 20 + 100 + 200 = 600 ms`，小于 `1500 ms`；撤力扣除两个发送槽和提交预算后有 `floor((1000−100−250)/50)=13` 步，理论可撤 `13×50×0.040/1000=0.026 Nm`，覆盖 `0.020 Nm` 帽。这些不证明真实零力帧与 IDLE 的到达时间。

## 离线复现

在 `firmware/Exoskeleton_F446/` 中，用一个**尚不存在**的 H 盘 `build/` 子目录：

```powershell
python -B docs/bench/s4-first-window-20261009/materialize.py --output build/s4-first-window-my-copy
python -B docs/bench/s4-first-window-20261009/run_probe.py --source build/s4-first-window-my-copy
python -B docs/bench/s4-first-window-20261009/build_trial.py --source build/s4-first-window-my-copy --cmake 'C:/path/to/cmake.exe' --ninja 'C:/path/to/ninja.exe' --gcc-bin 'H:/path/to/arm-gnu-toolchain/bin'
```

`materialize.py` 只复制当前活动工程的 182 个选定构建输入，不复制旧 `build/`、采集数据或本机个人预设；拒绝覆盖旧目录。它把历史已核对的 `HSE_BYPASS` main/IOC 时钟差异施加于当前输入，要求结果逐字等于已归档的 HSE 覆盖文件，并生成各输入的 SHA256 `source-manifest.json`。`run_probe.py` 从材料化的 S3 reader 源码提取**原有**只读配置函数与 `duration`，和实际 S4/SafetySupervisor/站稳代码一起在主机运行；提取段及原文哈希都留在 H 盘。`build_trial.py` 校验清单后使用 CMake/GCC/Ninja 编译并生成 ELF/BIN 与 SHA256。两个执行脚本均把 `TEMP/TMP/TMPDIR` 指向材料化目录内 H 盘路径；工具可执行文件路径可在 C 盘，但项目临时文件及产物均在 H 盘。

本机 2026-10-09 从多个独立新目录得到相同 BIN：113500 字节，SHA256 `4210a776d6440e601f972acce30a7859bb9080760709e7a739b92f1baecdc4e8`；最终全新构建 0 编译警告、0 CMake 提示。Debug ELF 包含绝对路径，不同目录的 ELF 哈希不同。门探针给出 `cold_profile_ready=0`、`session_zero_ready=1`、`after_zero_ready=1`；这只证明本配置能通过软件门，不证明实物停止。确切记录见 `verification.json` 和 H 盘被忽略的材料化目录。

## 准备时机修复后的新候选

三轮现场准备中，第一、第三轮在手推进入区间后触发 `SAFETY_SPEED`，均未到 READY，也未执行 `followstart`。原 `S4Follow_Poll` 在首组进入 20°–30°且瞬时速度合格的反馈上就调用 `OutputGateway_Prepare`；继续手动移动时，网关准备阶段的速度保护会故障撤权。主机回归先复现了“尚在运动就离开 SAFE_OFF”（失败记录 `build/temp-s4/pre-fix-failure.log`），再用独立的 `ShoulderStandstill` 实例要求区间内新鲜位置和速度连续站稳既有的 200 ms 候选驻留后才准备。越界、超速、旧帧或反馈失效会清空该驻留；网关开始准备后的速度保护没有改变。站稳阈值仍需现场验证手持能否达到。

修复后的隔离 HSE 副本在 `build/s4-entry-dwell-20261009/`：输入清单 SHA256 `fb16f81f858f609a5d9a86dc17c2ff94ffb25c1dc87f2f5681600ab84ffa33c6`，BIN 113676 字节、SHA256 `c3a1c6205d8d2695c1d116d3ef872d9885b880c5f7fd1ef421fde102deb0e763`。四个相关主机情景、生成代码检查、输出禁用活动工程 WindowsDebug 构建和隔离 HSE 构建通过，后两项均为 0 编译警告；配置门 probe 仍给出 `cold_profile_ready=0`、`session_zero_ready=1`、`after_zero_ready=1`。auto-embedded 总检查退出 1：ARCH 扫入既有 `main.c` 和被忽略的构建/第三方文件，HW 报既有 IRQ 同优先级；SPEC 通过。这些是刷写前的离线检查，不能代替板上验收。新 BIN 的刷写和 USB-only 检查见下节。

## 2026-10-09 新候选刷写恢复与 USB-only 检查

- 24V 关闭时先读回完整 512 KB Flash，SHA256 `e2ce97100c2051554cdf543e2bf8faaa9b25829c299718ddbc64834566d14050`；其中前 113500 B 的 SHA256 为 `4210a776d6440e601f972acce30a7859bb9080760709e7a739b92f1baecdc4e8`，与三轮准备所用旧试验 BIN 一致。
- 首次用 STM32CubeProgrammer 2.20.0 的 Hot Plug 连接刷写同一新候选 ELF，工具在擦除阶段报告 `failed to erase memory`。失败后再次完整读回 512 KB：sector 0 的前 16 KB 全为 `FF`，其余字节与刷写前备份相同。可确认这次失败已擦除首扇区，不能把失败状态视作旧固件完好；为何擦除失败未查明，现有证据不能单独归因于工具版本或连接模式。
- 随后改用 STM32CubeProgrammer 2.23.0 的 Normal 连接刷写同一候选 ELF，工具报告下载验证成功并复位。独立读回 113676 B 的 SHA256 为 `c3a1c6205d8d2695c1d116d3ef872d9885b880c5f7fd1ef421fde102deb0e763`，与候选 BIN 逐字节一致。
- 仅 USB 供电冷启动后，`usb-only.json` 为 PASS：`phase=1`（`SAFE_OFF`）、`reader=1`、`zero=0`、`output_allowed=0`、`cmd_torque=0`、`fault=0`。证据位于被忽略的 `build/s4-entry-dwell-board-20261009/`。这是重刷后的首个 USB-only 核对；随后现场准备见下节。持续输出、停止与 S4 台架验收仍未验证。

## 2026-10-09 新镜像现场准备进度

- 第一次 24V-on 预检 `PASS`，新鲜 IDLE 心跳年龄 80 ms。`followprepare` 被接受，在垂臂位取得 RAM 零点 `pos_mrev=2952`（2.952 转），输出许可与命令转矩均为 0。随后约 14 秒的 117 条 `followstatus` 都新鲜，均显示 `phase=5/zero=1/pos_mrev=2952/joint_mrad=0/output_allowed=0/cmd_torque=0/fault=0`，没有 READY。脚本等待到期发出一次 `followstop`，回复 `OK FOLLOW read_cancel_pending`。用户确认本轮未手动转动，故静止垂臂和未进入候选区间相符；不把脚本的等待超时写成反馈或站稳门故障。未执行 `followstart`。之后用户确认 24V 关闭、机构承托。
- 第二轮在 24V 关闭时用 STM32CubeProgrammer 2.23.0 Normal 软件复位，未重刷。`attempt2/usb-only.json` 为 `PASS`：`SAFE_OFF`、`reader=1`、`zero=0`、`output_allowed=0`、`cmd_torque=0`、`fault=0`。这是第二次上电前的检查；随后的观察见下一条。证据均在 `build/s4-entry-dwell-board-20261009/`。
- 第二次 24V-on 预检 `PASS`，新鲜 IDLE 心跳年龄 70 ms；`followprepare` 取得 RAM 垂臂零点 `pos_mrev=2928`（2.928 转），输出许可与命令转矩仍为 0。14 秒内 116 条新鲜 `followstatus` 均显示 `phase=5/zero=1/pos_mrev=2928/joint_mrad=0/output_allowed=0/cmd_torque=0/fault=0`，未到 READY；等待到期后一次 `followstop` 回复 `OK FOLLOW read_cancel_pending`。用户确认本轮也未手动移动并已断开 24V，所以静止零位与未进区间相符，不能把脚本等待超时写成固件故障。未执行 `followstart`。当前等待用户说明未移动原因；原始记录在 `attempt2/powered-preflight.json`、`prepare-once.json` 和 `ready-monitor.json`。
- 第三次 USB-only 冷启及 24V-on 预检均 `PASS`，后者新鲜 IDLE 心跳年龄 81 ms；`followprepare` 的 RAM 零点为 `pos_mrev=2847`。脚本三声提示后用户确实手动前屈，准备阶段 `phase=5` 的原始位置 `2847→3330 mrev`，故障保留的新鲜样本为 `3333 mrev`，相对关节角约 `0→0.382 rad`（约 21.9°），但没有 READY；未发送 `followstart`。故障时读链转入停机观察，用户随后断开 24V 并承托，表示只感到手动转动，没有电机嗡鸣或异常阻力。

  24V 关闭后保存的 `attempt3/s4-gateway-after-fault-384b.bin` 与 `s4-status-after-fault-228b.bin` 按该候选 ELF 的 DWARF 布局解出：`preparation_submitted=1`、网关 `FAULT/HEALTH/SPEED`，撤权 `stop_attempted/accepted/purge_ok=1`、`stop_delivery_failed=0`、`follow_session_used=0`。故障保留的新鲜样本 `seq=316`、位置 `0.3819778 rad`、反馈速度 `0.1782654 rad/s≈10.214°/s`，确实越过软件 `10°/s` 门；同期心跳 `state=8/axis_error=0/fault=0/hb_ms=132122`。这证明速度保护触发，不能证明 ClosedLoop 帧 TXOK、电机真正闭环或持续助力；断电后查询的 `error=0x40/fault=9` 和旧心跳也不能反推故障原因。暂不改限值或代码，等待用户确认手推中是否曾短暂停顿。
- 第四次 24V 关闭时使用 STM32CubeProgrammer 2.23.0 Normal 软件复位，同一 BIN 未重刷；USB-only `PASS`、上电 `SAFE_OFF`，24V-on 预检 `PASS` 且 IDLE 心跳年龄 114 ms。`followprepare` 的 RAM 零点约 `2873–2874 mrev`。三声提示后用户连续手动前屈；14 秒的 116 条新鲜 `followstatus` 均为 `phase=5/zero=1/output_allowed=0/cmd_torque=0/fault=0`，角度最高 `592 mrad≈33.9°`、最后 `559 mrad≈32.0°`，越过 30°候选上限而未 READY。等待到期后一次 `followstop` 回复 `OK FOLLOW read_cancel_pending`；用户随后确认 24V 关闭、机构承托。未执行 `followstart`，没有持续出力或正常撤力验收。原始记录在 `attempt4/`。

  另作提示音听辨：500 Hz／700 ms 低音不清楚，1800 Hz／1000 ms 高音清楚可区分。下一轮拟在**新鲜角度约 24°**时发出高音，提示用户停住并观察 200 ms 站稳；本轮没有执行这种角度触发提示，固件限值也未改。

- 第五次 24V 关闭下复位、USB-only `PASS`／`SAFE_OFF`；24V-on 预检 `PASS`，IDLE 心跳年龄 93 ms；RAM 垂臂零点 `2928 mrev`。三声短音后，自动 24°长音未触发，但监测记录 `phase=6` READY：末端 `seq=249/fresh=1/joint_mrad=408`（约 23.4°），心跳 `state=8/error=0/fault=0`、年龄约 113 ms，MCU 时间约 `50677 ms`，输出许可和命令转矩仍为 0。随后单独补发长音；`start-observe` 前置复核在 MCU 约 `70542 ms` 已为 `FAULT`，没有发送 `followstart`。

  24V 断开后 Hot Plug 保存的 `attempt5/gateway-after-fault-232b.bin` 和 `status-after-fault-228b.bin` 按候选 ELF 解出网关 `reason=4 (EXPIRED)/safety_reason=0`、`prepare_ms=49883/prepare_timeout_ms=15772`，许可到期约 `65655 ms`；`stop_attempted/accepted=1/follow_session_used=0`，心跳 `state=8/error=0`、反馈速度约 `0.0135 rad/s`。本轮直接原因是主机分步操作超过准备许可时间，不是速度保护或已证实的电机故障。用户切 24V 后先说尚未承托，随后确认已承托。下一轮计划把 READY 核验和独立 `followstart` 命令衔接在同一有界流程；本轮没有执行。原始记录在 `attempt5/`。

- 第六次用户提出直接启动，但 24V 关闭下复位和 USB-only 检查均 `PASS`／`SAFE_OFF`，24V-on IDLE 心跳年龄 61 ms；`followprepare` 建立约 `2893–2894 mrev` 的 RAM 垂臂零点后，脚本发出三声短音。14 秒内 116 条新鲜 `followstatus` 均显示 `phase=5/6`、`zero=1/output_allowed=0/cmd_torque=0/fault=0`，其中 89 条为固件 `phase=6` READY；反馈角度稳定在 `393–398 mrad`（约 22.5°–22.8°），位于固件 20°–30°小区间。主机脚本自设 `401 mrad`（约 23°）READY 接纳下界，所以未通过**主机更窄的门**，约 24°长音也未触发。14 秒后 `followstop` 回复 `OK FOLLOW idle_submitted; observing_stop`；未调用启动脚本或发送 `followstart`。依据上述原始记录，本轮垂臂取零、手动入区间和固件 READY 的准备测试通过；用户表示“这个测试已经通过了”，不据此扩展为持续出力验收。

  断电提示后用户先回复“已开”，其含义不足以确认 24V 已关闭；追问后答“是的”，才确认 24V 关闭且机构承托。用户要求停止后续测试；原拟调整主机接纳范围至 22°–28°、在 22°发长音的方案未执行。固件 20°–30°、20 mNm 帽、10°/s 速度门均未改。原始记录在 `attempt6/`；没有持续出力、正常撤力或 S4 台架验收证据。

## 现场解释边界

- 本次当次许可已经用于上述失败的准备尝试；若原因查明后另获当次许可，冷启动应**直接**从垂臂位执行 `followprepare`；先 `followobserve` 会消耗同一次 reader 机会，不能随后再 prepare。`followprepare` 的零点阶段只读，但手动移入区间后会发送零目标、模式和闭环准备帧；它本身属于出力会话的实物命令。`followstart` 仍单独手动许可。上电默认 `SAFE_OFF`，没有自动 Arm。
- `stop_accepted` 只说明 IDLE 帧由 HAL 接受入队，不等于 TXOK 或电机实际卸力。正常停止需记录读链尾窗的新鲜 IDLE 心跳、位置和 `s3reader.stop_observed/observed_ms`；只有另有同步 CAN 分析仪或适用采集时，才可声称保存了停止前后的原始帧。`STOPPED` 后 `followangle fresh=0` 不能冒充停后新鲜反馈，也不能据此否定尾窗内部曾有新鲜样本。
- 本候选保持 `EXPERIMENT_CAN_BLACKBOX=OFF`。既有黑匣子用于首次 CAN 错误冻结，正常停止后 128 条滚动记录不保证保留零力／IDLE 附近时序；其 HAL `TX_RESULT` 也只是入队结果。打开它还会增加尚未量化的中断和发送路径记录负担，故不能为取得首窗证据而直接启用。
- 24V 切断是现场可触及的人工后备方式，当前未证明其在持续输出期间的实际卸力；CAN 停止命令不能代替断线后的独立切断。方向矛盾、异常运动、停止失败时结束本轮并查根因。
- 上述首次至第六次的准备记录属于历史现场窗口。用户随后要求执行 R4-2B，结果见下节；两姿态减负、重定位 `0.5+5 s`、带动力切断和 S4 台架验收仍缺证据。

## 2026-10-09 R4-2B 短时持续输出与停止

两次均沿用已独立读回的隔离试验 BIN（113676 B，SHA256 `c3a1c6205d8d2695c1d116d3ef872d9885b880c5f7fd1ef421fde102deb0e763`），固件保持 20°–30°、转子侧命令帽 20 mNm、3 秒会话。每次在 24V 关闭时冷复位并取得 USB-only `SAFE_OFF`，上电后新鲜 IDLE 预检通过；生产 `s4_site.c` 仍输出禁用，没有改固件或接线。

- 第七次（自然到期）：垂臂 RAM 取零后，固件在 `393 mrad` 新鲜 READY；`followstart` 于准备后约 5.886 s 获接纳。运行中 21 条 `cmd_torque=1` 且七属性持续更新，序号 299→392，关节换算角 392→386 mrad；随后进入渐撤，再到 `phase=3/reader=7/stop=1/fault=0`、`s3reader observed=1`，肩侧 CAN 错误为 0。原脚本摘要保留 `FAIL`：它错误要求 `s3reader hvalid=1`。该字段由默认 S3 site 的 `feedback_timeout_ms=0` 计算，在 S4 会话中为 0；它不推翻停止尾窗的 `observed=1`。脚本在已经停止后又发一次 `followstop`，回复 `session_closed`，不是运行中主动停止证据。用户确认随后 24V 关闭、机构静止且无异常。原始摘要和串口十六进制回报见 [第七次摘要](r4-2b-attempt7-expiry-summary.json)、[第七次事件](r4-2b-attempt7-expiry-events.jsonl)。
- 第八次（运行中主动停止）：新鲜 READY 为 `402 mrad`，`followstart` 于准备后约 9.809 s 获接纳；运行中持续有新鲜七属性与 `cmd_torque=1`。约 1.072 s 时单独 `followstop` 获 `ramping_down` 回复，随后观察到渐撤、`phase=3/reader=7/stop=1/fault=0`、`s3reader observed=1`、新鲜 IDLE 心跳年龄 107 ms，肩侧 CAN 错误为 0。缓存七属性中 `INPUT_TORQUE` 目标位 `3BED4320` 解为约 `0.00724 Nm`，这是驱动目标读回，不是实际机械力矩；同一查询的 `fresh=0` 也使用默认 S3 site，另以序号及请求/回复时间判断这笔读回。脚本结果为 `EXPLICIT_FOLLOWSTOP_OBSERVED`。用户确认随后 24V 关闭、机构静止且无异常。原始摘要和串口十六进制回报见 [第八次摘要](r4-2b-attempt8-followstop-summary.json)、[第八次事件](r4-2b-attempt8-followstop-events.jsonl)。

这两轮支持**短窗内命令出力与七属性读取并行、自然到期和主动停止渐撤**的台架结论。正常固件路径在零输出邮箱排空／TXOK 条件后才进入 STOPPED，可作源码与镜像身份支持的间接证据；`stop=1` 仅表示 IDLE 经 HAL 入队，未独立抓到 IDLE TXOK。用户的静止观察不等于测得物理卸力。原计划的运行中人工切断 24V 短会话，用户明确认为过于严格并决定跳过；该项保留为未测，不再作为本次 S4 台架范围的额外现场门槛。实际机械力矩、两姿态减负和重定位漂移仍未测试，R4-3 及 S4 台架验收未完成。
