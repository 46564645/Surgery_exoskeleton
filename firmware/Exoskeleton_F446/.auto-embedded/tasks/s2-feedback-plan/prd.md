# S2 第一轮：端口配置与双 CAN 接收基础

用户于2026-09-25明确要求 IMPLEMENT THIS PLAN，本轮全部固件项review:true，实施授权已取得。此范围替代原完整反馈阶段计划。阶段以task.json为准，验证证据见edits.md，项目进度只记development_schedule.md。

## 目标与边界

保留S1 USB串口、LED与裸机调度，新增双CAN初始化、有界原始接收与串口观察；stage=S2_CAN_RX，始终output_allowed=0。用户已确认低压接线并连接USB；本轮24V断开、H/L不接驱动器，不实现设备协议解析、在线判断、运动发送或自动总线恢复。

## 批准的实施清单

| 文件／层级 | 接口与实现 | 验证标准 | 审查 |
|---|---|---|---|
| .ioc、CubeMX生成Core/CAN HAL | 6.18.1 + F4 V1.28.3；CAN1 PB9/PB8、CAN2 PB13/PB12，AF9 | APB1 42MHz，BS1=11/BS2=2/SJW=1，分频6/3；正常模式；CAN1时钟依赖；接收与SCE中断5/0；保留S1 | review:true |
| Core/Inc/board_can.h、Core/Src/board_can.c、Core/Inc/s2_config.h（L1） | bool BoardCan_Start(void)、bool BoardCan_Read(BoardCanBus, BoardCanFrame*)、bool BoardCan_GetStats(BoardCanBus, BoardCanStats*)；无公开HAL类型 | 两路各64帧；分界14，bank0/FIFO0与bank14/FIFO1；标准数据帧；ISR最多3帧；队列满丢新帧；临界区恢复PRIMASK；初始化失败可查询 | review:true |
| App/application/can_monitor.h/.c、app_main.c、console_commands.c（L4） | 每1ms每路最多消费16帧；最近帧；canstatus/shoulder/elbow；复用CLIP | 每条回复小于256字节；seen=0/age_ms=na；ID/DLC/data/年龄为原始观测；不报告电机在线 | review:true |
| 顶层CMakeLists、README、hw-lock、接口登记、唯一开发日志 | 自有源文件显式加入顶层清单；生成清单由CubeMX维护 | 区分软件结果、USB板测与未执行的驱动器联调 | review:true |
| tests/host、run_host_tests.py、check_generated.py、bench_can_serial.py | MSVC模拟HAL、再生成检查、USB查询；保留原POSIX测试入口 | 隔离、队列满/回绕、非法DLC、接收预算、启动失败、错误统计、时间戳回绕、新旧串口命令 | review:true |

CAN1 500kbps仅无驱动器测试准备值，接GDS68前核实；CAN2 1Mbps。USART2 PA2/PA3保持115200 8N1无流控，端口按枚举识别。现场线色统一维护在硬件接口登记第10.2节。

## 验证与后续

顺序：CubeMX生成/保留检查 → MSVC模拟测试 → WindowsDebug零新增警告 → CubeProgrammer烧录校验 → USB查询约60秒，output_allowed始终0。异常先保留证据、查明根因，不猜改代码。

本轮只完成接收基础；后续借万用表核实供电和基本保护，再按GDS68→C620→双路联调。先读取实物版本、ID、速率、启动设置，再解析反馈和判断超时；不猜默认值、不升级驱动器固件、不执行运动或校准。
