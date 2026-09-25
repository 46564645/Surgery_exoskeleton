# 编辑清单

| 文件 | 改动 | 验证标准 | 结果 | commit |
|---|---|---|---|---|
| S1-01：ioc、CubeMX生成的Core/UART Drivers/cmake | 按用户批准配置USART2、DMA与IRQ；官方文件仅由CubeMX生成 | Debug构建；生成顺序；Drivers原文件校验 | 软件通过；实物读回未完成 | 本地S1软件快照（本提交） |
| S1-02：Core/board_console、board_runtime、s1_config | L1静态缓冲、HAL DMA、统计临界区；BSP后最终IRQ核验 | 循环位置、重复通知、队列满、故障、超时、优先级 | 主机模拟通过 | 本地S1软件快照（本提交） |
| S1-03：App/application、third_party、顶层CMake | L4静态CLIP命令、1 ms调度；固定LwRB/CLIP源码与许可证 | 协议边界、调度回绕、链接原子依赖 | 13项测试通过；Debug FLASH 40140 B、RAM 6720 B | 本地S1软件快照（本提交） |
| S1-04：tests、README、hw-lock、接口登记 | 软件回归、实物测试入口、设计/实测状态区分 | CubeMX再生成后自有源码保留；实物10分钟查询 | 再生成检查通过；串口初始查询即超时，未开始10分钟测试 | 本地S1软件快照（本提交） |

## 实施授权与来源

用户明确要求实现修订版S1计划（所有代码review:true），本记录对应该授权；原research/innovate为阶段历史快照。模块3/4/6/30的源码证据见research.md，CLIP内部核查见innovate.md。直接固定复用LwRB与CLIP（commit及许可证见third_party各SOURCE.md），DMA参考MaJerle的位置差算法并适配HAL/主循环发送，调度使用已核实HAL时基。没有引入FreeRTOS或StateSmith。

## 软件验证与独立审查

- `./build.sh Debug`退出0，无新增警告；最终ELF无未解决原子操作依赖。
- `python3 tests/run_host_tests.py`：真实自有模块及第三方库链接模拟HAL，13个独立场景通过；ASan/UBSan及-Wall/-Wextra/-Werror开启。覆盖分段/连续命令、LF/CRLF、空行、额外参数、96/97边界、非法字符、队列满、DMA跨尾及重复/陈旧通知、故障、TX超时及tick回绕。
- Verifier发现TX完成与超时决定之间竞态；先增加能重现失败的timeout_race，再用保存/恢复PRIMASK的短临界区修复决定过程，回归通过。没有为未查明的实物故障修改代码。
- `python3 tests/check_generated.py`通过；CubeMX再生成后Core/App/third_party校验和不变，调用顺序为BSP_PB_Init → BoardRuntime_FinalizeInterrupts → App_Init。原有Drivers文件校验和未变，新增UART文件来自CubeMX。
- 人工分层：应用仅使用平台接口，HAL句柄留L1；ISR有界搬运/计数，解析/格式化/发送启动留主循环；TX完成前不释放排队数据或覆盖暂存区；共享统计短临界区保持原中断状态。CAN/HX711/PWM/运动控制不适用。

## auto-embedded机械检查的三项未通过与解释

`python3 .auto-embedded/scripts/check.py --json`返回1，不能记为全绿：

1. ARCH-2：CubeMX main()顶层8次调用超过通用阈值6。均为既定初始化编排；自有业务在App，不为阈值手改生成器布局。
2. ARCH-5：build/Debug中的CMakeCCompilerId.c为934行，超过800。属于工具生成的编译器识别产物，非本项目手写模块，不修改此产物消警。
3. HW-CONFLICT：USART2、DMA1 Stream5/6均为5/0。它们是不同IRQ；相同抢占级为批准的有意配置，避免两个RX回调上下文相互抢占。硬件允许共享优先级，通用检查器将共享误判为冲突。实际值仍需板上读回验证。

spec完整性检查通过。上述例外按用户计划逐条人工审查，未更改检查器或官方库掩盖结果。

## 实物验证状态

用户已确认只接开发板USB、无电机动力。探针066EFF555748874987182240可枚举，但CubeProgrammer正常/低速复位连接及ST官方GDB服务器均报USB错误；用户重插后仍失败。已尝试将当前ELF转换HEX写入板载虚拟磁盘，未取得SWD读回校验，不声明烧录验收通过。VCP初次、RESET后和USB重插后查询均无数据返回；用户最新明确观察为LD2绿色慢闪、LD1红色常亮。慢闪也符合旧LED固件，不能由此证明新版本已运行。

尚未完成：10分钟6000条查询、板上IRQ读回、三次复位恢复、重插后查询恢复。禁止由主机测试或“灯闪”推断硬件通过；硬件问题根因未定，任务保持active。

补充：git diff --check将CubeMX生成文件的CRLF报告为行尾空白；保留生成格式，未为该提示手改官方文件。独立复查确认TX竞态回归有效，启动IRQ源码链未发现可确证失败原因。

下载链路补查：后台存在VS Code启动的ST-LINK服务；共享模式连接失败。短暂停止该服务后单独连接仍报DEV_USB_COMM_ERR，随后恢复服务。串口无其他进程占用。待核对USB是否经Hub及CN2实物短接帽；不由此断言硬件根因。
