---
title: 确定运行状态与安全控制权
type: grilling
status: closed
parent: ../map.md
blocked_by:
  - define_architecture_viewpoints
claimed_by: codex
---

## Question

系统状态、ExoKit 动作生命周期和安全故障状态分别管理什么；发生取消、通信丢失、反馈异常或紧急停止时，哪个模块拥有最终输出控制权？

## ExoKit findings

- `IReaction`保留 `prepare -> startup -> runLoop -> shutdown`生命周期；正常完成调用 `shutdown(false)`，取消调用 `shutdown(true)`，两条路径都必须清理动作状态。
- `CmdReactionRunner::launchReaction()`在启动新 Reaction 前先取消并销毁旧 Reaction，因此同一命令入口只拥有一个当前动作。
- `JointLockReaction`进入时记录原扭矩和模式、使能关节并锁定当前位置；退出时恢复进入前状态。
- `ExoskeletonJointActiveHandle`构造时关闭扭矩；`ExoskeletonCalibrator`也确保所有主动关节在校准和加载配置期间关闭扭矩。ExoKit 没有“校准完成即自动使能”的行为。
- ExoKit 的关节范围检查发现任意主动关节越界后关闭所有主动关节；急停按钮同样遍历所有主动关节并关闭扭矩。
- ExoKit 的安全实现直接依赖 Dynamixel，并以 `exit(1)`结束程序。它没有独立 `FAULT`状态、通信超时治理、故障锁存、恢复条件或最终输出仲裁，因此只能保留语义，不能直接移植实现。

已核对位置：`IReaction.cpp/.h`、`CmdReactionRunner.cpp`、`JointLockReaction.cpp`、`ExoskeletonJointActiveHandle.cpp`、`ExoskeletonCalibrator.cpp`和`ExoskeletonHandle.cpp`。

## Direct decisions from the open-source index

- 采用索引模块 26：系统状态机与 ExoKit Reaction 生命周期分离。Reaction 只能管理动作开始、运行、取消和清理，不能修改安全状态。
- 采用索引模块 27：保留“新动作取消旧动作、取消后清理、完成后恢复”的 ExoKit 语义，改为静态对象和裸机调度。
- 采用索引模块 28：L4 设置独立 `SafetySupervisor`；它可以取消当前 Reaction、撤销候选输出并进入 `FAULT`，优先级高于重力补偿、Amplify、Lock、串口命令和 JointController。
- 采用索引模块 29：设备在线、反馈超时、任务周期健康和看门狗独立于 Reaction。只有系统关键任务和关键反馈均健康时才允许喂看门狗。
- 采用索引模块 25：关节角度、速度和输出限制位于 Reaction 之外，并在最终输出前形成不可绕过的约束；具体阈值保持 `TBD`。

## State ownership model

系统状态、运行模式和 Reaction 生命周期分为三个层次，不用一个枚举同时承担所有职责：

| 层次 | 管理内容 | 不负责的内容 |
|---|---|---|
| 系统生命周期状态 | 上电检查、校准、允许运行、故障锁存与恢复资格 | 不计算重力补偿或动作输出 |
| 运行模式 | 基础重力补偿、动作放大、通电保持 | 不清除故障，不绕过安全门 |
| Reaction 生命周期 | 某个 ExoKit 风格动作的准备、启动、周期运行、取消和清理 | 不直接写 CAN，不决定系统安全状态 |

当前状态集合采用：

- `STARTUP_CHECK`：读取配置并检查两个 Joint 的反馈、零位和关键服务健康；主动输出关闭。
- `CALIBRATION`：执行 ExoKit 风格逐关节 `tare`流程；主动助力和保持输出关闭。
- `SAFE_OFF`：系统已运行但主动输出未许可；用于上电后的安全等待、人工停止和故障恢复前检查。
- `FOLLOW`：持续重力补偿基础状态。
- `ASSIST`：`FOLLOW`基础上叠加 Amplify 增量助力。
- `LOCK`：取消 Amplify，组合重力补偿和当前位置保持。
- `FAULT`：锁存安全故障，取消当前 Reaction、撤销两个 Joint 的候选输出并请求两个执行器关闭主动输出。

不再单独设置 `HOVER`：本项目已经用 `FOLLOW`表示持续重力补偿，用 ExoKit `LOCK`表示通电位置保持；再增加 HOVER 会与两者重复。

## Control authority

最终输出控制权顺序固定为：

`SafetySupervisor > 系统状态许可 > 关节角度/速度/输出约束 > 命令合成器 > Reaction > 外部命令`。

- 外部命令只能请求状态或动作变化。
- Reaction 只能形成候选增量或保持请求。
- 命令合成器形成重力、Amplify 和 Lock 的候选合成输出。
- Joint 限制层执行正常范围内的限制与拒绝。
- `SafetySupervisor`拥有最后许可权，可把任意候选输出替换为安全撤销输出。
- 驱动器自身保护继续有效，但只作为底层保护，不替代系统级监督。

## Event classification

| 事件 | 系统处理 | 是否进入 `FAULT` |
|---|---|---|
| `cancel`或新动作替换旧动作 | 按 ExoKit 调用当前 Reaction 的取消/清理；平滑返回 `FOLLOW`或进入新动作 | 否 |
| 无效或不支持的文本命令 | 拒绝命令并报告错误，不改变当前有效状态 | 否 |
| 调试串口暂时无新命令 | 当前动作由本地状态机继续管理；串口不是持续控制心跳 | 否 |
| FORCE-01/固定10 SPS的HX711数据无效或超时 | 标记低速辅助监测不可用并报告，不改变编码器主控制 | 否；当前明确不作为安全输入 |
| 任一执行器关键反馈超时或失效 | 取消当前 Reaction，撤销两个 Joint 输出并锁存故障 | 是 |
| 关节状态无效、跳变到不可解释值或超过硬边界 | 拒绝继续控制，撤销两个 Joint 输出并锁存故障 | 是 |
| 速度、输出或驱动器故障状态超过已验证安全条件 | 撤销两个 Joint 输出并锁存故障 | 是 |
| 控制/安全任务失期或系统无法证明关键任务健康 | 禁止继续喂看门狗；能执行时先由安全门撤销输出 | 是或由看门狗复位 |
| 紧急停止 | 立即取得最高优先级，取消动作并请求两个执行器关闭主动输出 | 是，且不得由普通 `cancel`清除 |

故障阈值、输出撤销时序、物理急停是否切断 24 V 动力以及恢复需要人工复位还是重新上电，均不在没有硬件依据时补写。物理急停和具体接口受“具体接线门禁”约束。

## Confirmed startup permission

- 当前版本不要求操作者另行执行“启用”操作。启动检查、零位加载或校准以及两个 Joint 的关键反馈检查通过后，系统可以从 `SAFE_OFF`进入 `FOLLOW`。
- 状态转换不得把上述条件直接写死，而是通过独立逻辑许可 `assist_enable_permission`判断。当前该许可由系统内部启动条件产生，不增加新的硬件输入。
- 为后续电磁吸附机构预留许可来源。未来接入时，只有“吸附已确认”才能使 `assist_enable_permission`成立，系统才允许进入持续重力补偿；吸附许可撤销时，安全监督必须阻止继续进入或保持主动助力状态。
- 当前硬件清单没有确定电磁铁、驱动器、吸附检测方式或接口，因此本票据不添加具体器件、针脚、端子或接线。进入该阶段时必须触发“具体接线门禁”，先完成硬件接口编码。

## Resolution

本项目保留 ExoKit 的 Reaction 生命周期、新动作取消旧动作、Lock 进入/退出恢复和异常时关闭全部主动关节的行为语义；在其外围增加独立系统状态机、健康监测与 `SafetySupervisor`，解决 ExoKit 没有独立故障状态和最终输出仲裁的问题。

系统采用 `STARTUP_CHECK`、`CALIBRATION`、`SAFE_OFF`、`FOLLOW`、`ASSIST`、`LOCK`和`FAULT`。`FOLLOW`是持续重力补偿基础状态，`ASSIST`在其上叠加动作放大，`LOCK`组合重力补偿和位置保持，不设置与其重复的 `HOVER`。

最终控制权为 `SafetySupervisor > 系统状态许可 > 关节限制 > 命令合成器 > Reaction > 外部命令`。普通 `cancel`按 ExoKit 语义清理动作并返回 `FOLLOW`；关键执行器反馈异常、不可解释的关节状态、超出安全条件、关键任务失期和紧急停止进入锁存 `FAULT`，取消当前 Reaction，并撤销两个 Joint 的主动输出。辅助力传感器失效只降低监测能力，不改变编码器主控制。

当前在启动与校准检查全部通过后，不增加人工启用步骤，但统一通过 `assist_enable_permission`进入 `FOLLOW`。该逻辑接口为未来电磁吸附确认预留；具体电磁硬件与恢复、断电行为仍须在器件确定及接口编码后设计。
