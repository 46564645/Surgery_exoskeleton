# 肩侧周期 0x029 替代 SDO 372/378 的可行性（只读调研，2026-10-16）

> 范围：只读调研。没有改固件、没有接触硬件、没有提交。结论依据下文列出的本地资料、上游源码和已归档台架日志；没有新增实测。

## 结论：闭环期间大概率可行，切换前需要做一次有界台架对照

- **IDLE 下不能用。** GDS68 冷上电后，在首次进入 CLOSED_LOOP 之前，0x029 一直全零。原因已在上游源码中找到（见第2节），不是本机解码问题。
- **首次闭环之后可用。** 已归档的 S4 台架日志里有 3 帧非零 0x029，解出的位置与同时段 SDO 372 读数相差在 1 mrev 以内（见第3节）。
- **还缺证据的一点：** 这 3 帧都采于停止后的静止阶段。**运动中**还没有把 0x029 和 SDO 372/378 放在一起比对过，所以速度一致性和连续性都没有验证。
- **对固件设计的影响：** `followprepare` 是在 IDLE 下取 RAM 零点的。0x029 在这个阶段还是 0，所以 IDLE 零点和七属性启动读取仍要用 SDO。0x029 能替代的只是**进入闭环之后**每 20 ms 一次的 372/378 轮询。

## 1. 厂家手册（GIM6010-8 rev2.2；读 Word，页码按原 PDF）

| 内容 | 位置 | 要点 |
|---|---|---|
| 帧格式 | p41–42 §4.1.1/4.1.2 | CAN ID = node_id<<5 + cmd_id。node 1 的 0x009 帧即 0x029；数据小端，浮点为 IEEE754。 |
| 0x009 Get_Encoder_Estimates | p48 | byte0–3 `Pos_Estimate` float32 rev，对应 `axis0.encoder.pos_estimate`；byte4–7 `Vel_Estimate` float32 rev/s，对应 `axis0.encoder.vel_estimate`。两者都是**转子侧**量。 |
| 周期配置 | p44–45 §4.1.5 | 配置项名为 `axis0.config.can.encoder_rate_ms`（**不是** ODrive 0.6 的 `encoder_msg_rate_ms`），默认 10 ms，写 0 关闭。心跳 `heartbeat_rate_ms` 默认 100 ms。 |
| IDLE 与闭环下的行为 | — | **手册没有写。** 第48页把它映射到 encoder 估计量，但与实机 IDLE 全零的现象不符，说明手册这一处描述与实际取值路径不一致。 |

实机 endpoint0 schema（HW3.11.1/FW0.6.5，`firmware/Exoskeleton_F446/docs/reference/gds_hw3.11.1_fw0.6.5_endpoint0.json`）的对应端点：
- 183：`config.can.encoder_rate_ms`
- 322：`controller.config.load_encoder_axis`
- 372/378：`encoder.pos_estimate`、`encoder.vel_estimate`

schema 中**没有 `pos_vel_mapper` 对象**，CAN 配置项也沿用 0.5.x 的命名。可见厂家的 “0.6.5” 是 ODrive v3.6/0.5.x 结构的分支，ODrive 官方 0.6 文档对该帧的描述不适用。

## 2. 上游 ODrive 源码与论坛

0.5.5 与 0.5.6 中 `can_simple.cpp` 的 `get_encoder_estimates_callback()` 取值来源不同：

| 版本 | 位置取自 | 速度取自 |
|---|---|---|
| 0.5.5 | `axis.encoder_.pos_estimate_.any().value_or(0.0f)` | `axis.encoder_.vel_estimate_` |
| 0.5.6 | `axis.controller_.pos_estimate_linear_src_.any().value_or(0.0f)` | `controller_.vel_estimate_src_` |

0.5.6 的这两个 `*_src_` 只在 `Axis::start_closed_loop_control()` 中通过 `connect_to(&axes[load_encoder_axis].encoder_.pos_estimate_ / vel_estimate_)` 接上。

- **首次闭环前**：两个源都没接上，`value_or(0.0f)` 返回 0，所以帧照常发出但内容全零。
- **退出闭环后**：源码里没有对应的断开操作，所以回到 IDLE 后这两个源仍然接着。

ODrive 论坛有两帖报告同样的现象：0.5.6 下 CAN 估计值为零，“进入闭环瞬间才变为非零”，同时 USB 读数正常。官方人员称这是 “a known regression in 0.5.6”，建议降回 0.5.4/0.5.5。

ODrive 官方 0.6.x 文档把 Pos/Vel_Estimate 的来源写作 `pos_vel_mapper.pos_rel/pos_abs` 和 `pos_vel_mapper.vel`，周期项为 `encoder_msg_rate_ms`。如上所述，本机 schema 里没有这些对象，所以 0.6 的描述不能直接套用。

**推论（未经厂家证实）：** 厂家 0.6.5 沿用了 0.5.6 的 controller 输入源路径。理由有两点：
1. IDLE 全零、而同时 USB 的 `pos_estimate` 非零且 `is_ready=1`（开发日志 2026-10-02 第 511–512 行）；
2. 闭环之后 0x029 变为非零（第3节）。

只要 `load_encoder_axis = 0`（手册 p35：仅 BETA 版需要设置），闭环后 0x029 的取值就和 372/378 是同一个 encoder 估计量。

## 3. 开发日志与已归档台架记录

- **S2 时期**（日志第 291、298、312、511–514、538、877 行）：全部是 IDLE 下观察到的全零帧。IDLE 下总线每秒约 110 帧（第 877 行：30.18 s 内 3320 帧），约等于 10 ms 一次的 0x029 加 100 ms 一次的心跳，与出厂默认周期一致。
- **S4 R4‑2B 第七、八轮**（`firmware/Exoskeleton_F446/docs/bench/s4-first-window-20261009/r4-2b-attempt{7,8}-*-events.jsonl`）：每轮都在 24 V 冷上电后开始，开头的 0x029 全零。闭环运行并停止后：

| 时间（UTC） | phase | 0x029 原始数据 | 解码 pos / vel | 同时段 SDO pos_mrev |
|---|---|---|---|---|
| 14:54:50.177 | 3（STOPPED） | `B19D5B400200FA3B` | 3.43150 rev / 0.00763 rev/s | 3431 |
| 14:58:01.070 | 9（STOPPING） | `041958400400FA3B` | 3.37653 rev / 0.00763 rev/s | 3376 |
| 14:58:01.408 | 3（STOPPED） | `E819584004007A3B` | 3.37658 rev / 0.00381 rev/s | 3376 |

  这些数据说明三点：
  1. 闭环后 0x029 非零，位置与 SDO 372 一致；
  2. 回到 STOPPED/IDLE 后仍保持非零，与上游“退出闭环不断开”一致；
  3. 下一次 24 V 冷上电后又变回全零，说明这一状态在断电后不保留。

  缺口：运行中 `shoulder` 命令显示的都是 0x025 SDO 应答帧，因此运动中还没有 0x029 样本；速度也没有和 378 做过比对。

## 4. 最小的一次台架验证（沿用现有镜像和现有命令，不改固件）

**目的：** 补齐运动中的证据。确认闭环期间 0x029 的位置和速度与 SDO 372/378 一致、帧间隔约 10 ms、没有零帧回退。

**前提：** 用当前已读回的 S4 或 S5 隔离镜像；机构承托；24 V 可以立即切断；限值、区间和会话时长都按该镜像已有配置。整轮的输出只有 READY 阶段的零力矩目标，**不执行 `followstart`**。

1. **冷上电，IDLE 阶段**：交替发送 `shoulderfeedback` 和 `followangle`（或 `followstatus`），各约 5 次。预期 `shoulderfeedback` 显示 `est=1` 但 `pos_mrad=0`，而 SDO 的 `pos_mrev` 非零。这一步复现“首次闭环前为零”。
2. **`followprepare`，按现有流程手动进入 READY**：此时驱动处于闭环、目标力矩为零，心跳 `state=8`。按约 10 Hz 交替发送 `shoulderfeedback` 和 `followangle`，共约 3 s；期间操作者在小区间内缓慢推动约 ±2°，然后停稳。
3. **`followstop`，回到 IDLE 后**：再发送 `shoulderfeedback` 和 `followangle` 各 3 次。预期 0x029 仍然非零。之后切断 24 V。

**判定方法：** 换算关系为 `pos_mrad = pos_mrev × 2π`，速度为 rev/s 转 rad/s；两边都按各自的时间戳（`est_ms` 与 `sample_ms`）对齐。

- **通过**：同一时刻（相差 ≤ 20 ms）的位置差 ≤ 2 mrev（约 0.09° 关节角），速度符号一致；闭环期间没有出现 `pos=0` 帧；`est_ms` 年龄始终 ≤ 20 ms。
- **不通过**：任一条不满足就保留 SDO 方案，并把现象报给厂家。

**附加检查（可选）：** 若再做一次只读 SDO，可读端点 183（`encoder_rate_ms`）和 322（`load_encoder_axis`），确认分别为 10 和 0。这需要增加一个只读端点请求。按第 4.4 节的精简原则，如果周期与位置都已一致，可以不做。

## 5. 改用 0x029 后可去掉或简化的失败分支（只列出，本次未修改）

前提是 0x029 只用于闭环期间的连续 pos/vel 反馈。IDLE 零点、七属性启动读取，以及 S5 的零力矩/模式确认（input_torque、control_mode/input_mode）仍要用 SDO，这些是按需发起、一次性的读取，不是每 20 ms 的轮询。

**`App/application/s3_shoulder_service.c`**
- `Poll()` 第 226–246 行：处理挂起读请求的 TX 结果，包括 ALST 仲裁丢失后的单次重试，以及 `BOARD_CAN_READ_TX_FAILED/NONE` 导致的 `fail(S3_READ_TX)`。
- `Poll()` 第 247–257 行：每 20 ms 提交一次位置/速度读请求，以及提交失败或 `CanMonitor_ExpectShoulderProperty` 失败时的 `fail(S3_READ_TX)`。
- `Poll()` 第 203 行：`CanMonitor_NextShoulderProperty` 失败时的 `fail(S3_READ_REPLY)`，即应答顺序或关联出现歧义。
- 第 184–193 行 CANCELLING 状态、`BeginReadOnlyStop()`，以及 `Stop()` 中 `BoardCanTx_CancelShoulderRead` 失败锁存 `S3_READ_TX`：都是为撤销在途 SDO 而设，读请求不再存在后就不需要了。
- `BeforeOutputStop()` 中的 STOPPING“保持在途读请求与应答配对”，以及 `AfterOutputStop()` 中 `!accepted → fail(S3_READ_TX)` 里属于读链路的部分。
- 第 196–198 行 `S3_READ_WINDOW`：读会话总时长上限（S5 为 35 s）。这是为限制 SDO 独占总线而设的；被动接收周期帧后可以去掉，或改为普通的会话时长限制，不再作为读链路故障。

**`App/application/shoulder_property_feedback.c`**（只针对连续 pos/vel）
- `Expect()`：pending、顺序、时间倒退时隔离；`Poll()`：应答超时后隔离；`Receive()`：应答与请求的时间窗口和配对检查。
- S5 快速路径中 POSITION→VELOCITY 两次读取凑成一对再发布（第 87–97 行）。0x029 单帧同时带 pos 和 vel，本身就是一致的一对，不再有“半对”的情况。

**保留并换成新的检查：**
- 0x029 的新鲜度（`est_ms` 年龄 ≤ 20 ms）；
- 心跳 `state=8`；
- 闭环期间把全零帧视为无效：拒绝，并标记“源未接通”；
- 浮点必须为有限值。

`can_monitor.c` 第 58 行对 `BoardCanTx_ShoulderReadResult()` 的依赖也会随之减少。

## 来源

- 本地：`Main_ project/Hardware_information_list/all_hardware_relevant_documents/ABUIABA9GAAgzpi6wQYoh9D9CQ.docx/.pdf`（GIM6010-8 rev2.2）p35、p41–48；`firmware/Exoskeleton_F446/docs/reference/gds_hw3.11.1_fw0.6.5_endpoint0.json`；`Main_ project/docs/development_schedule.md`；上表所列 S4 台架 jsonl。
- [ODrive fw-v0.5.6 can_simple.cpp](https://github.com/odriverobotics/ODrive/blob/fw-v0.5.6/Firmware/communication/can/can_simple.cpp)
- [ODrive fw-v0.5.5 can_simple.cpp](https://github.com/odriverobotics/ODrive/blob/fw-v0.5.5/Firmware/communication/can/can_simple.cpp)
- [ODrive fw-v0.5.6 axis.cpp](https://github.com/odriverobotics/ODrive/blob/fw-v0.5.6/Firmware/MotorControl/axis.cpp)（`start_closed_loop_control`）
- [论坛：V3.6 encoder position/velocity == 0 over CAN until closed loop](https://discourse.odriverobotics.com/t/odrive-v3-6-encoder-position-and-velocity-0-over-can-until-its-put-in-closed-loop-control/11515)
- [论坛：Problem with Encoder Estimates via CAN in 0.5.6](https://discourse.odriverobotics.com/t/problem-with-encoder-estimates-via-can-in-0-5-6/10638)
- [ODrive 0.6 CAN Protocol 文档](https://docs.odriverobotics.com/v/latest/manual/can-protocol.html)
