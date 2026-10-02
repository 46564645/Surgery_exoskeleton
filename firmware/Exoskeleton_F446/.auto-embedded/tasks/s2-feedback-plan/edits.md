# S2第一轮编辑与验证清单

trace_id：T-s2-feedback-plan。全部固件项review:true，用户明确批准实施本轮计划；未自动提交或推送。唯一开发日志在项目docs/development_schedule.md，本文件只记录改动与验证依据。

| 文件／范围 | 改动／核查 | 实际结果 |
|---|---|---|
| .ioc、main/MSP/IRQ/hal_conf、生成CMake、官方CAN HAL | CubeMX6.18.1+F4V1.28.3生成双CAN；USER CODE失败钩子 | 500k/1M，AF9，时钟依赖与中断5/0；check_generated通过；重新生成全部USER CODE/自有文件一致；原有Drivers逻辑未变 |
| board_can、s2_config（L1） | 64帧静态队列、标准数据帧、部分失败、错误计数、限量ISR | 两路隔离、满丢新、回绕、非法DLC、启动失败、FIFO累计/清除、bus-off模拟通过；无应用TX接口 |
| can_monitor、app_main、console_commands（L4） | 每1ms限量消费、最近帧、三个命令、S2_CAN_RX | 无HAL头进入应用；未收帧seen=0；年龄跨UINT32回绕通过；最大字段长度计算218B，小于256B |
| tests/host、run_host_tests.py | MSVC模拟HAL扩展；保留POSIX ASan/UBSan入口 | 25项通过：原13项+新增12项；MSVC实验性C11原子开关及两个旧单元告警处理记录于research；本机未跑消毒器 |
| tests/bench_can_serial.py | pyserial跨平台USB查询，失败也保存报告 | 原有POSIX bench_serial.py未改；新旧7条命令初始检查及60秒600次查询通过；输出0、未见帧、错误0 |
| 顶层CMakeLists、README、hw-lock、接口登记、AGENTS摘要、唯一日志 | 构建清单与当前状态同步 | WindowsDebug成功、0警告，FLASH 47564B / RAM 9008B；原预设与构建入口保留 |

## 板测证据

- CubeProgrammer2.23.0，探针066EFF555748874987182240，SWD4MHz，原ST-LINK固件V2J28M18未改。枚举时复现旧USB错误，按已有方法重启该USB复合设备后恢复；没有猜改固件。
- 烧录前读备份512KB，下载ELF后Download verified successfully并软件复位；日志build/s2-validation/preflash.log、flash.log，SHA-256见hashes.json。
- COM10 115200 8N1，stage=S2_CAN_RX，can1_started=1/500000、can2_started=1/1000000；两路seen=0、age_ms=na；完整查询及持续测试见usb-soak.json。尚无真实驱动器报文，不声明物理CAN链路通过。

## 机械检查与人工复核

先运行check.py --json，报告保存在auto-review.json，未全绿。默认PowerShell5存在UTF-8/.NET兼容错误；使用已有Git Bash直接补跑arch-check.sh，同版本检查器完成扫描，报告architecture-bash.log。42项中41项是build内下载依赖/编译探测文件；固件唯一项为生成main的10个初始化调用超过模板6个，保持CubeMX编排不为过门改写。hw-lock的通信IRQ共用5/0是批准的同优先级串行执行设计，无针脚/流/DMA重复占用；spec完整性通过。没有隐藏原报告、降低阈值或宣称全绿。

项目审查：改动范围与reference、外设时钟/引脚/过滤器、向量入口、静态队列/临界区恢复、构建与文档逐项核对。PWM/控制律/运动输出/HX711不适用本轮。临界区仅有界拷贝/计数/寄存器读，不打印或等待；ISR回调最多3帧，应用消费最多16帧每路。初始化阶段是单次启动；错误仅记录，不自动重启总线。协议语义/在线超时留待下轮查实驱动器。

年龄显示边界修复：新帧时间戳晚于本轮调度取时的模拟用例先失败、修改后通过；证据age-race-before.log/host-tests.log。最终烧录与usb-soak.json对应修复后的ELF；第一次板测另保留before-age-fix文件，未以旧测试替代新版本结果。
