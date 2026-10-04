S2 编码器可见观察窗口（仅主机；固件未改）

本窗口复用同目录capture.py/capture.ps1的固定读取、校验与清理流程。
构建与验证证据在 D:/codex_tasks/2026-10-01/task/s2-visible-capture/，
原始文件哈希、离线测试结果、窗口可见性记录与截图均保存在该处。
本次没有运行Live、串口采集、复位、烧录或供电操作。

默认先看等待窗口（无设备枚举、无端口访问、无采集子进程）：
  powershell -NoProfile -File .\visible_capture.ps1
大字显示“未开始 / 不做动作”，开始按钮禁用。默认启动不等于采集准备完成。

未来的一次现场窗口：
1. 主线程先和用户协调新的明确供电窗口。不要因旧会话 READY 或旧日志开始动作。
2. 得到本次明确确认后运行：
   powershell -NoProfile -File .\visible_capture.ps1 -Live -PoweredWindowConfirmed
3. 先看到灰色“未开始 / 不做动作”。用户点击“开始一次读取”一次，
   窗口才启动自己拥有的现有 capture.ps1 子进程；本窗口不能重试或再次开始。
4. 只有本次子进程真实 READY_FOR_MOVEMENT，且已观察至少两组匹配的有效
   完整配对、时间戳和计数持续前进，才显示绿色“可做一次小幅手动变化”。
   用户现场直接看窗口，一次轻微往返；无需等待远程聊天转发 READY。
5. 红色“中止 / 不做动作”或“读取结束 / 不做动作”出现即停止手动变化。
   COMPLETE、浮点值变化或成功退出都不自动证明物理变化验收通过。

状态判定：
- 实时模式只读自己启动的进程 stdout（现有 -u + flush），不读日志重放。
  会话名由本次新建 evidence-visible-* 目录确定；固定 ST-LINK 父标识、
  唯一健康 COM 绑定、30秒/1200请求/10Hz 会话约束继续沿用现有脚本。
- 复用 capture.feedback 与 Tracker；固件报告 age_ms 必须 <=100ms，
  两个时间戳前进、pairs/tx/elapsed_ms 不倒退、无 rejected、output_allowed=0。
- 新事件的 UTC、同机源 monotonic 和本地接收时间均要求年龄 <200ms；
  允许至多50ms的未来偏差。窗口每25ms检查一次，留出250ms预算余量。
  固件 age_ms 与主机消息新鲜度是分别检查的两项，不能相互替代。
- READY 的 first/latest pair 必须与窗口实际观察到的样本完全一致，
  READY 与其最新样本间隔 <=100ms。不会因进程启动或查询成功点绿。
- 已确认 encstart 后最初 <=200ms 内、尚无有效配对时，active=1/reason=0/
  valid=0 仅显示等待。取得首个有效配对后 valid=0 立即红色。
- 失效样本、active=0、reason!=0、ABORT、FIRMWARE_BOUNDED_END、COMPLETE、
  进程退出、消息停更、超时、会话/配对不匹配或主机时钟跳变都会撤下提示。
  红色是本次窗口的终态；延迟 READY 或后续样本不能重新点绿。
- 读取时间也按主机 monotonic 限定：从 SESSION 起最多30秒显示预算，
  比固件实际启动略早撤下属于保守拒绝，绝不延长动作提示。

“停止动作提示，等待读取结束”与关闭窗口：
立即撤下绿色并锁定红色，不宣称已立即发送 encstop。现有 capture 没有
协作停止输入接口；窗口保持拥有并排空子进程输出，不杀进程、不另开串口。
原有最多30秒读取结束后，由原有 finally 尝试 encstop（链路仍可用时）、
最终查询及关端口。点击关闭期间保留红色窗口，待子进程退出后才关闭。
本窗口不新增驱动命令、运动命令、属性写入、重启或恢复。

离线演示：
  powershell -NoProfile -File .\visible_capture.ps1 -Demo
点击“开始演示”显示蓝色模拟状态，约1.5秒后红色。始终大字“演示，不做动作”，
没有采集进程或硬件访问，不能用于动作许可。Demo 与 Live 互斥。

离线验证：
  H:\app\python.exe -B -X utf8 .\test_visible_offline.py
11项重点测试通过：完整实际228事件/50样本记录、初始预热、延迟/积压READY、
错误会话/计数/配对/时间戳、样本停更、失效/固件年龄、ABORT/结束/进程退出、
时钟跳变及截止时间不可复活、默认/Live双重选项。见 offline-validation.json。
fixtures/actual-events.jsonl 是只读离线夹具，绝不在 Live 中被加载。
首次预热夹具错误地把合成事件放在上一条 REPLY 之前，触发时钟顺序拒绝；
已修正夹具的合成事件时间顺序，未放宽生产状态检查。

Windows 真实可见性验证：verify_visible_wait.py 创建前台默认等待窗口约3.5秒，
Win32 IsWindowVisible=true、HWND/PID 见 wait-window-proof.json；Popen调用0、
serial模块未导入。wait-window.png 仅截图本窗口 HWND。验证后已关闭。
随后只调整用户可见文案，未重新执行硬件操作或可见性验证。

限制：
这是观察和提示界面，不是电机输出或硬件停止保护。Windows调度、窗口主线程
阻塞或系统冻结时不能保证及时重绘；200ms/25ms是正常运行下的保守检查预算。
解除阻塞后会先检查真实时间，旧READY不能恢复。没有声称硬件在线绿灯或
现场延迟验证通过；本次只有离线状态验证和真实灰色等待窗口验证。

参考：索引第31行；实际读取 ExoKit Processing GUI/processing_gui.pde 的
setup()/draw() 与运行/取消布局，只借鉴分组和可见状态，未复用其自动串口打开
或运动命令。现有 capture.py 的反馈验证、Tracker、固定绑定及 finally 清理
直接复用。Python/Tk 标准库避免另装上位机和修改现有串口所有权。
https://docs.python.org/3/library/tkinter.html
