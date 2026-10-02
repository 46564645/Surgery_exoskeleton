# S2 研究依据（2026-09-25）

本文件记录设计依据；项目进度只记根目录 development_schedule.md。

## 计划前基线（保留调查背景）

- 活动工程为 H:/Desktop/Surgery_exoskeleton-git/firmware/Exoskeleton_F446；旧快照不是开发入口。
- S1按用户决定基本完成，SWD换口后恢复，偶发问题不阻塞S2。当前仍为S1_COMM、output_allowed=0，双CAN尚未配置。
- 已检查固件README、CMake/预设、ioc、app_main.c、console_commands.c、平台接口及auto-embedded架构/硬件约定。SYSCLK84 MHz，APB1 42 MHz；沿用四层架构、现有串口与裸机调度。
- 用户明确24V电源尚未测量；回复未明确两块CAN模块与所有线材是否齐全，不能标记齐备。
- 当前稀疏检出不含硬件目录和参考索引，已用git show读取HEAD中的登记、硬件参考和开源借鉴索引。读取旧快照中的原始PDF前已核对其Git blob与当前HEAD一致；不以旧快照状态覆盖当前工程。

## 对应表与实际源码

对应开源索引的CAN、GIM6010/C620驱动、反馈与单位转换、在线检测行；先核对ExoKit。

| 来源与实际读取文件/函数 | 采用方式与差异 |
|---|---|
| ExoKit `Arduino API/lib/exokit_api/src/exo/ExoskeletonJointActiveHandle.cpp`，位置读取/零位与符号处理 | 仅参考关节接口边界；其Dynamixel接口不能直接承担双CAN驱动 |
| BetaRobot `bsp/bsp_can/bsp_bxcan.c`，CANConfig及接收回调 | 适配CAN1/CAN2与过滤器分区思路；静态有界队列搬运帧，解析移至主循环，不照搬动态注册/回调中的设备处理 |
| BetaRobot `drv/drv_motor/drv_djimotor/drv_djimotor_broadcast.c`，接收回调和GetData | 适配C620大端字段解析；不照搬中断共享快照、多圈推断和减速器参数 |
| BetaRobot `drv/drv_daemon/drv_daemon.c`，DaemonReload/DaemonIsOnline/DaemonTask | 仅借鉴新数据刷新在线状态；改为裸机时间戳差值，不引入RTOS任务 |
| UICRM-Embedded `boards/drivers/src/Motor/DjiMotorBase.cpp`，Motor3508::UpdateData | 交叉核对角度、RPM、电流、温度字段；不采用P19减速比或未经验证的力矩换算 |
| Rhino-Pi-X1-ROS2HomeRobots `Code/STM32/ros2-wheel-foot-car/User/Devices/SW_Motor/gim6010_drv.c`，GIM6010_fbdata/SWGetEncoderCount | 参考CAN Simple ID拆分与反馈分派；FDCAN/RTOS/MIT发送/系数不复用，以对应实物版本手册为准 |

版本固定：BetaRobot `c83dd84d6c7717105c948d8d12f2fbb5f4d6b48c`；UICRM `225427b004568e75069f0ed7a92180a548082295`；Rhino `a4b24b80c00aa1cbf58d2a8a20167d8710d97bec`。
原始入口分别为 https://github.com/itgz123/BetaRobot 、https://github.com/UIC-RoboMaster/UICRM-Embedded 、https://github.com/Coderyne/Rhino-Pi-X1-ROS2HomeRobots 。所读源文件暂存被忽略的build/s2-references，不修改参考仓库。

## 手册证据与限制

- UM1724 PDF第59页Table29核对Morpho针位；CAN模块原理图第1页显示3.3V供电及固定120Ω；已查看原图，实物排针必须按丝印对应。
- GDS rev2.2手册PDF第10页查看接口图；第37页说明旧硬件USB/CAN限制；第41–43页说明CAN Simple、11bit ID、默认500kbps与按固件版本区分的心跳。编码器估计字段为小端float32，位置rev、速度rev/s；这些是手册事实，不证明当前旧24V/Y-48实物版本和配置相同。
- C620 V1.01中文部分PDF第25页查看接线图，第33页核对反馈：标准数据帧、DLC8、ID=0x200+电调ID，位置0–8191、RPM、电流原始字段及温度；默认反馈1kHz。电调ID须查实，不能默认1。
- 两CAN独立；协议与节点ID可不同。正常CAN接收需要硬件ACK，不能将静默模式当作只有两节点的正常接收方案；禁止应用发送运动报文与硬件ACK是两回事。
- 终端按模块及远端实际设置核对；约60Ω仅适用于断电且两端120Ω实际接通，不能据此武断否定断电后失效的软件终端。

## 工具结果

- embeddedskills固定提交 `536c1f929e359a5c02ef8ea9f1a20691e8d764e3` 的12项skill已安装到用户skills目录。
- workflow_plan.py成功发现唯一GCC工程；gcc_project.py presets成功枚举Debug/Release/WindowsDebug/WindowsRelease。仅为工程识别，不是构建、烧录或探针发现结果。
- 继续使用已有auto-embedded本地任务与四层spec；未启用嵌套hooks、未修改全局信任、未替换已验证的CubeProgrammer下载流程。
- 此轮没有修改或构建固件，没有连接驱动器或测量电源。

## 本轮范围与实施依据

- 后续照片和用户确认已补齐两块CAN模块的低压接线信息，USB串口也已实测。仍未测模块供电或24V、未接驱动器；不把照片当电气测量。
- 本轮对应索引第5行双CAN平台、既有第30行CLIP命令扩展；上表设备协议资料留作下轮研究，未移植设备解析。BetaRobot CANConfig的分区和Rx回调经适配，去除动态注册与ISR设备处理，使用ST HAL过滤器/接收接口。
- CubeF4 V1.28.3的HAL子模块为b6f0ed3829f3829eb358a2e7417d80bba1a42db7；实际读取HAL_CAN_Init/Start/GetRxMessage/IRQHandler/ResetError。HAL累积ErrorCode，回调记账后清除，避免旧FIFO溢出被重复累计。
- CubeMX保持CoupleFile=false，初始化留在main.c。初始化失败钩子只放USER CODE区，仅CAN失败允许继续串口观察，其他Error_Handler路径保持原逻辑。
- CubeMX CLI的project path使用正斜杠时曾拼出双盘符路径；改用原生Windows反斜杠后成功生成相对CMake路径，没有手改生成清单。自有文件与全部Core USER CODE块在再生成前后相同；原有Drivers只有换行变化，已恢复原字节。
- 本机MSVC编译器原缺Windows SDK/UCRT，已从微软官方安装10.0.26100.9169。MSVC测试开启官方/experimental:c11atomics；仅CLIP解析单元关闭C4244/C4267、既有S1 board_console单元关闭x64 size_t到32位LwRB的C4267，其他项目代码保持/WX。固件编译设置未放宽，第三方源码未改。
- check.py默认调用Windows PowerShell 5，因UTF-8无BOM及GetRelativePath不兼容而无法完成架构扫描。直接用已有Git Bash运行同一包arch-check.sh补查，实际固件范围仅main初始化调用数量告警；其余41项位于build下载依赖/探测文件，不作为产品源码。IRQ同优先级是已批准配置，不是资源重复占用。机械门禁未全绿，保留原报告及人工解释。

- 最终复核补充：App_Poll先取调度时间，再消费CAN队列；ISR可能在这两步之间写入下一毫秒时间戳。旧时间减新帧时间会按uint32回绕为极大年龄。模拟用例固定调度50ms、帧51ms，修改前can_commands断言失败（age-race-before.log），修改为读取最近帧后经BoardRuntime_NowMs重新取时后25项回归通过；最终ELF重新烧录校验并再跑60秒600次查询。未修改调度、时基或CAN协议。
