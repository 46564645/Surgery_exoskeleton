S5 FOLLOW 实时监控窗口（tests/live_monitor/s5_live_window.py）

用途
- 独占 ST-LINK VCP，跑一次有界 S5 双轴 FOLLOW 会话。
- 实时三联图：肩/肘角度、肩/肘助力力矩、HX711 力，共享时间轴（滚动最近 60 s）。
- 助力生效/软限暂停用底色区分，起停画竖线，可直接截图作研究主证明图。
- 同时把原始命令、20 Hz 采样和 10 Hz 力值写入 build/live-<时间戳>/。

前置条件
- 固件为 R5 双轴镜像（followstatus 返回 s5=1 axes=3），板上输出默认未许可。
- 24 V 已打开、机构已承托、可立即断电；界面上的确认勾选框对应这条。
- 串口不能被其他程序占用（dual_follow_once.py、串口助手等需先关闭）。
- 本机 pyserial 位于 build/s2-tools/python，运行前设置 PYTHONPATH。

启动
    set PYTHONPATH=%CD%\build\s2-tools\python
    python tests/live_monitor/s5_live_window.py --port COM10 --duration-ms 40000
参数
    --port             显式 VCP，例如 COM10；不自动扫描
    --duration-ms      观察时长，默认 40000（固件会话 45 s，脚本留 1.5 s 停止余量）
    --output-dir       记录目录，默认 firmware/build/live-<时间戳>
    --board-bin-sha256 可选，写入 summary.json 便于追溯板上镜像
    --offline-check    内置假串口，验证状态机与落盘，不接触硬件
    --replay DIR       回放已保存目录，不打开串口，用于无硬件截图

操作顺序
1. 勾选“24V 已开、机构已承托、可立即断电”（不勾选时按钮不可用）。
2. 点“检查并开始”：预检 → followprepare → 两轴取零 → READY → 立即 followstart。
3. 到设定时长后自动 followstop。
4. 等横幅显示“已正常停止（phase=7，输出许可 0）”。
5. 若显示“停止未确认”，保持承托并立即切断 24 V，不要重试。

为什么没有单独的“开始”按钮
- 固件 `OutputGateway_Arm()` 规定：READY 只在 `prepare_timeout_ms`（R5 为 20 s）内有效，
  超时再发 followstart 会被判 EXPIRED 并锁存故障。
- 因此“人工确认”放在勾选框与按钮点击上，点下后立即准备并启动，避免 READY 过期。
- 一次启动只能跑一个会话：故障锁存或正常停止（phase=7）后都需要复位 MCU 才能再跑。

界面底色
- 浅绿：RUNNING 且未暂停（助力生效）
- 灰：RUNNING 但软限暂停（该轴目标已清零）
- 深色：非运行阶段

记录文件（目录在窗口底部显示）
- session.jsonl   每条命令/回复与标记，带 UTC 与 monotonic 时间
- samples.csv     20 Hz：t_s, phase, gate, reason, safety, q_sh_deg, q_el_deg,
                  sh_rotor_mNm, sh_joint_Nm, el_mA, el_Nm, paused, allowed, remaining_ms
- force.csv       10 Hz：t_s, raw, force_N, valid, sat, age_ms
- summary.json    样本数、峰值速度、暂停次数、停止是否确认、镜像哈希

单位换算（只在落盘/显示边界做一次）
- 角度：q_*_mrad / 1000 × 180/π
- 肩力矩（关节侧）：cmd_sh_mNm / 1000 × 8（rotor_per_joint）
- 肘力矩：cmd_el_mA / 1000 × 0.020（S5_ELBOW_MOTOR_KT_NM_PER_A）

导出实验图（论文/报告用）
- 会话结束后点窗口上的“导出实验图”，或直接运行：
      python tests/live_monitor/figure_session.py build/live-<时间戳>
- 输出到 <会话目录>/figure/：figure.pdf（矢量）、figure.svg（矢量）、figure.png（300 dpi）。
- 默认只画 RUNNING 会话段；加 --full 画整段记录（含停机后保持值）。
- 常用参数：
      --lang en            英文标注（投稿用）
      --width 3.5 --height 6.2   单栏尺寸（inch）
      --window 5 15        只画 5–15 s
      --title "..."        加图标题
- 图内含：助力生效（浅绿）/软限暂停（浅灰）底色、安全停机虚线、三面板共享时间轴。
- 依赖 matplotlib，已安装在 build/s2-tools/python（3.10.9）；运行时需设置同一 PYTHONPATH。

已知限制
- 一次启动只跑一个会话：故障锁存需要 MCU 复位，本工具不复位。
- 力值只作辅助记录，不进入控制或安全判据。
- 20 Hz 采样与固件 50 ms 指令周期一致；串口超时会丢样本，界面样本数可作判据。
- 本工具不烧录、不改参数、不自动重试；停止未确认时必须人工断电。
