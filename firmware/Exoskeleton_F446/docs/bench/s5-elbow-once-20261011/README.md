# S5 R2：肘部一次小脉冲串口记录

`elbow_once.py` 用于 **S5_R2_ELBOW_TRIAL 修复版及保持相同命令接口的 CAN2 黑匣子隔离镜像** 的一次肘部 S3 脉冲，不使用 FOLLOW 命令。旧版 R2 的[备份、刷写和独立读回](../../../build/s5-r2-elbow-board-20261011/attempt.json)已完成；现场唯一一次脉冲请求获接受，但收尾出现 `purge_ok=0/stop_delivery_failed=1/FAULT`。修复版已[刷写并独立读回](../../../build/s5-r2-stopfix-board-20261011/attempt.json)；本次[原始串口记录](../../../build/bench-logs/s5-r2-stopfix-20261011/elbow-once-20261011-203712.jsonl)显示一次脉冲与一次取消、零电流帧 `TXOK=1`、`SAFE_OFF`。操作者确认已关闭 24 V、没有看到肘部动作，也没有持续用力。因此仅停止控制链在本次窗口通过，肘部可见运动和方向仍未确认。`--offline-check` 只使用假串口，不连接开发板。

2026-10-11 后续同参数观察：只在主机脚本加 `--midpulse-sample`，于脉冲约 40 ms 请求一次新鲜 C620 反馈，固件、镜像和限值未变。[第一次中段记录](../../../build/bench-logs/s5-r2-midpulse-20261011/elbow-once-20261011-205343.jsonl)的 `current_raw` 从 12 到 519、编码器从 3437 到 3438 tick；操作者当时仍未见动作。用户随后要求最多三次重复；[重复第 1 次](../../../build/bench-logs/s5-r2-repeat-20261011/elbow-once-20261011-205512.jsonl)正常取消并回到 `SAFE_OFF`，第 2 次[原始记录](../../../build/bench-logs/s5-r2-repeat-20261011/elbow-once-20261011-205514.jsonl)取消后报告 `FAULT/reason=2`，脚本立即中止，**第 3 次未发送**。操作者确认已断 24 V、机构承托静止、无异常，并明确从侧面看到这两次均有朝**屈肘**方向的微动。故小幅受控运动及 `+2000` 在此装配姿态下的屈肘方向有现场主观观察；重复试验的正常停止未通过，不能推进肘 FOLLOW。

第 2 次故障后在 24 V 关闭时用 SWD Hot Plug 只读保存[网关 RAM 和解码](../../../build/bench-logs/s5-r2-repeat-20261011/fault-read-trial2-20261011/decode.json)：`state=3/FAULT`、`reason=2/HEALTH`、`safety_reason=2/SAFETY_TRANSPORT`，停止标志 `attempted/accepted/purge_ok=1`、`stop_delivery_failed=0`；串口 `stop_txok=1`。断电后只读[CAN2/控制台计数](../../../build/bench-logs/s5-r2-repeat-20261011/fault-read-trial2-20261011/serial-read-only.jsonl)为 `err/drop/invalid/fov/bus_off=0`、`uart_errors/rx_overflows/tx_timeouts/scheduler_overruns=0`，但不是故障瞬间原子快照。`S3Bench` 的 `inputs()` 在反馈不完整时也会提前返回，让零初始化的 `transport_healthy=false` 映射成同一 `SAFETY_TRANSPORT`；因此当前证据尚不能区分瞬时总线异常、反馈／会话角无效等来源，不因微动成功而忽略这次故障。

后续[CAN2 黑匣子镜像](../../../experiments/elbow_capture/README.md)已上板并独立读回；同参数两次小脉冲均正常取消，操作者报告两次微动、无异常。黑匣子未触发故障，故上次故障原因仍未定位；完整记录在唯一开发日志。

修复版的一次性刷写脚本为 `python -B .\flash_r2_stopfix_once.py`，只接受 BIN SHA256 `297b35ce7293ae9d068c4a9179f0f69529ca1e07a0ae54c31e6de17263e030eb`。它已将板上 512 KiB 备份到 H 盘、核对旧 R2 镜像，并完成刷写、官方校验和独立读回；脚本不应在同一证据目录重复运行。旧 `flash_r2_once.py` 与旧哈希保留为历史记录。

现场使用时明确指定 VCP 与 H 盘日志目录。例如：

```powershell
python -B .\elbow_once.py --port COM10 --output-dir H:\Desktop\Surgery_exoskeleton-git\firmware\Exoskeleton_F446\build\bench-logs\s5-r2
```

需要中途反馈时加 `--midpulse-sample`；它要求中段 `received_ms` 比脉冲前更新、`age_ms≤20`，若读取失败立即走原取消路径。运行前将项目已有 `pyserial` 路径加入 `PYTHONPATH`，并将 `TEMP/TMP/TMPDIR` 指向 H 盘。每次脚本最多发一条脉冲；重复调用必须以每次退出码和收尾状态为界，故障后停止。

脚本固定为 115200 8N1、无流控、DTR/RTS 低电平，不扫描端口。它先读 `ping/status/canstatus/elbowfeedback/stopstatus/outputstatus`；只接受带新 `stop_pending`／`stop_txok` 字段的 R2 修复镜像。CAN2 和肘反馈有效、初始输出关闭后执行 `prepare`，等待 `READY`、本次肘零位与 250 ms 静止观察，再 `arm` 并发送**最多一次** `elbowpulse <token> 2000 150`。约 100 ms 后发 `cancel`，随后短轮询 `stopstatus`；成功返回要求 `s3_state=0`、`stop_pending=0`、`stop_txok=1`、`stop_delivery_failed=0`。原始串口请求与响应写入 H 盘 JSONL；退出码 0 仍只表示命令链和零电流 CAN 帧 TXOK，不表示已独立测得机械停转或力矩。

离线假串口检查（完全不打开 VCP）：

```powershell
python -B .\elbow_once.py --offline-check
```

现场前仍要由人确认机构独立承托、24 V 状态、可立即切断动力、镜像及当前布置。`+2000` 在本次装配和姿态下使肘部产生屈曲微动，但有效活动角域与负载效果尚未确定。出现异常时由现场人员直接断开 24 V；脚本串口 `cancel` 不能替代断电手段。
