# S5 R2 肘侧 CAN2 故障黑匣子（隔离诊断）

本实验借用相邻 `can_blackbox/recorder.c/.h` 的固定环形缓冲与首次故障冻结方式。它记录主循环实际交给 C620 解析器的 CAN2 `0x201` 原始帧，在肘部会话角首次进入 `FAULT` 时保存判定前后的角度计数、转速、序号和时间；若网关先进入 `FAULT`，则以网关状态兜底冻结。记录和串口查询不发送 CAN，也不更改 R2 的电流、角域、速度、时长或停止链。

## 构建与边界

实验开关 `EXPERIMENT_ELBOW_CAPTURE` 默认关闭，且只允许与 `S5_R2_ELBOW_TRIAL=ON` 一起使用。它与旧 CAN1 黑匣子不能同时打开。固件目录使用独立的 H 盘构建目录；运行前将 `TEMP`、`TMP`、`TMPDIR` 指向 H 盘临时目录。示例（PowerShell）：

```powershell
$tmp = 'H:\Desktop\Surgery_exoskeleton-git\firmware\Exoskeleton_F446\build\s5-r2-capture-tmp'
New-Item -ItemType Directory -Force -Path $tmp | Out-Null
$env:TEMP = $tmp; $env:TMP = $tmp; $env:TMPDIR = $tmp
$cmake = 'C:\Users\asus\AppData\Local\stm32cube\bundles\cmake\4.3.1+st.1\bin\cmake.exe'
& $cmake --preset WindowsDebug -B build/s5-r2-can2-capture-20261011 -DS5_R2_ELBOW_TRIAL=ON -DEXPERIMENT_ELBOW_CAPTURE=ON
& $cmake --build build/s5-r2-can2-capture-20261011
```

当前板上诊断 BIN 为 `build/s5-r2-can2-capture-20261011/Exoskeleton_F446.bin`，SHA256 `4113182d09ed5ef9102916fdce08b87c4bd717ea0a00a57b0e0f98414f32732c`，已按[一次性记录](../../build/s5-r2-can2-capture-board-20261011/attempt.json)备份旧板、刷写并独立读回匹配。关闭实验开关的同源 R2 重建 BIN 与刷前旧修复版 SHA256 `297b35ce7293ae9d068c4a9179f0f69529ca1e07a0ae54c31e6de17263e030eb` 完全一致。旧故障帧不可补回。

四个 `--wrap` 入口仅包装 `C620_Receive`、`ElbowSessionAngle_Observe`、`OutputGateway_Step` 和 `clip_cmd_parse_line`，每个真实函数执行一次。捕捉只在主循环写静态 RAM，不在中断里打印。链接后应在 ELF 反汇编确认调用点确实转向 `__wrap_*`；只看到编译成功不算接入成功。

## 导出

故障后保留开发板 USB 供电，先关闭 24 V，再从原 VCP 端口导出。RAM 记录会随 MCU 复位或 USB 断电消失。`dump.py` 只发送 `ecstatus`、`ecfault`、`ecgate`、`ecfreeze`、`ecrewind`、`ecnext`；输出目录必须是**全新且位于 H 盘**的绝对路径。示例：

```powershell
$env:PYTHONPATH = (Resolve-Path 'build/s2-tools/python').Path
python -B experiments/elbow_capture/dump.py --port COM10 --out 'H:\Desktop\Surgery_exoskeleton-git\firmware\Exoskeleton_F446\build\bench-logs\s5-r2-can2-capture-一次性目录名'
```

COM10 只是先前使用的端口，运行前以当次枚举为准。`raw.jsonl` 保存每次请求和原始回复；`capture.json` 保存结构化记录及导出是否完成，失败时也保留已收到的原始内容。`kind=1` 是 C620 接收帧，`kind=2` 是会话角故障触发，`kind=3` 是网关故障触发。`ecfault` 的 `last_*` 是会话上一次接受的值，`ticks/rpm/seq/rx_ms` 是触发当次样本；`ecgate` 留网关实际故障理由。导出器给出的 `speed_exceeded/jump_exceeded` 只是按记录重算的提示，需结合原始帧、调用顺序和固件分支判断。

这不是独立总线分析仪：时间戳是 MCU 毫秒级接收与主循环处理时刻，不能证明物理线路上所有帧或实际关节力矩。只有新的现场窗口触发并成功导出后，才能判定上次健康故障对应的具体输入条件。

## 2026-10-11 首次上板结果

沿用原 R2 参数做了两次短脉冲，均正常取消并收到停止零帧 `TXOK=1`；操作者报告两次微动、无异常，已关闭 24 V 且机构承托静止。[黑匣子导出](../../build/bench-logs/s5-r2-can2-capture-20261011/no-fault-export-2150/capture.json)完整保存 112 条 CAN2 `0x201` 记录，`fault=0/gate=0/triggered=0`。本轮没有旧故障的触发帧；记录在无故障时持续滚动，导出内容晚于第二次脉冲，不能归因上次故障。导出已调用 `ecfreeze`，当前 RAM 记录冻结；下一次捕捉前须复位 MCU 重新武装，且现场测试需另行确认当前状态。原始试验命令、反馈和参数见[唯一开发日志](<../../../../Main_ project/docs/development_schedule.md>)。
