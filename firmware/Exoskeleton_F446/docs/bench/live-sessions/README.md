# 实时窗口会话原始记录（live-sessions）

这些目录原位于 `firmware/Exoskeleton_F446/build/live-*/`。`build/` 被 Git 忽略，而 M5「台架回放」需要这些实测角度序列，所以复制到 `docs/bench/` 保存（2026-10-26）。原目录仍留在 `build/` 下，内容相同。

## 来源与内容

由 `tests/live_monitor/s5_live_window.py` 采集的 S5/R5 台架窗口，每个目录：

| 文件 | 内容 |
|---|---|
| `samples.csv` | 每帧记录，列为 `t_s, phase, gate, reason, safety, q_sh_deg, q_el_deg, sh_rotor_mNm, sh_joint_Nm, el_mA, el_Nm, paused, allowed, remaining_ms` |
| `force.csv` | HX711 力样本（10 SPS，含原始码与换算值） |
| `session.jsonl` | 事件流水（状态切换、暂停、故障、命令） |
| `summary.json` | 会话汇总（时长、状态计数、故障/暂停次数） |
| `figure/` | 该会话的出图（PDF/PNG/SVG） |

`q_sh_deg`/`q_el_deg` 是关节角（垂臂会话零点，前屈为正），`sh_rotor_mNm` 为肩转子命令、`sh_joint_Nm` 已按 8:1 折算到关节，`el_mA`/`el_Nm` 为肘电流与按 kt=0.02 N·m/A 折算的力矩。

## 用途与边界

- **M5 台架回放**：把 `samples.csv` 的角度序列喂给仿真 DLL，比较仿真算出的目标与台架记录的目标，验证控制代码接入正确。
- 这些是台架实测记录，但**不是**省力效果的验收数据：它们只说明当时的命令与姿态，没有人体用力测量。
- 目录名即采集时间（本地时间 `YYYYMMDD-HHMMSS`）。哪一个窗口对应哪次参数改动见 `Main_ project/docs/development_schedule.md` 开发日志。
