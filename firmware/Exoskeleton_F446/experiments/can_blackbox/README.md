# CAN1 黑匣子（临时实验，默认关闭）

用途：保留肩侧 CAN 错误发生前后的软件观测，尤其是 HAL 清除前的 ESR/TSR。它是取证工具，不是已确认的通信修复，也不提供电机命令。

**当前现场状态（2026-10-06）：** 本文后面的“未烧录”是离线准备阶段记录。用户关闭24V后，已通过Normal模式刷入HSE实验版并独立读回匹配；USB查询output_allowed=0、肩rx/err0、黑匣子未冻结。首次HOTPLUG擦除失败及恢复经过、备份见`captures/flash-20261006T005618131Z/`和项目唯一开发日志。当前等待24V重新上电确认，尚无故障捕获；不因刷入成功宣布通信问题修复。

**后续现场更新：** 用户确认上电后已捕获并导出首错，见`captures/observe-20261006T005859528440Z/`。第0事件为ESR01000020（接收侧Form、TEC0/REC1），发送邮箱全空；17条内无应用TX，随后收到029好帧。后续缓存errors74→79，证明除首次突发外还有新增。黑匣子现已冻结，UART关闭；没有发送SDO/运动或复位。详细边界以唯一开发日志和analysis.json为准，未定位物理根因。

**最新结论（2026-10-06 23:55 CST）：** 当前板上为只读HSE黑匣子（`readonly-hse-build-verification.json`）。更换/绞合CANH/L、电机USB保留条件下，主动读取30秒/1200请求/600组MCU配对，主机保存300组新鲜样本，CAN错误为0；读取会话已结束，输出仍禁用。前述首错捕获为较早窗口，不代表当前冻结状态。肩侧S3按用户缩减范围收尾，详见唯一开发日志；未定位物理根因或验收带动力S4。

**只读候选复现：** 先用`prepare_hse.py --output <固件build内一个不存在的新目录>`核验并生成输入，再仅将生成目录的`App/application/s3_site.c`替换为活动工程同名默认禁用文件，保持其余输入不变。以Debug、既有ARM GCC工具链及`EXPERIMENT_CAN_BLACKBOX=ON`构建，再用arm-none-eabi-objcopy生成BIN。相对当前可动作黑匣子构建输入仅site不同；确切哈希和site字节检查见只读验证JSON。不修改活动site或历史覆盖档案。

**证据归档：** 本次选择将`captures/`中既有JSON/JSONL/LOG文本证据显式提交，目录对后续临时采集仍默认忽略；BIN备份和构建产物只保留本机。删除实验前须保存需要的证据。

## 目录与接入

| 文件 | 职责 |
|---|---|
| `recorder.c/.h` | 固定容量环形记录、首次触发、冻结；无 HAL/串口依赖 |
| `stm32_capture.c` | STM32 寄存器采样、HAL 调用前后记录、串口查询 |
| `experiment.cmake` | 默认 OFF 的构建开关与 GNU ld `--wrap` 接入 |
| `dump.py` | 从串口分条导出，保存原始回复和 JSON；不发送 CAN |
| `prepare_hse.py` | 核验历史168个输入，生成仅含实验增量的HSE构建副本到`build/` |
| `tests/` | 离线环形缓冲及 HAL/寄存器模拟测试 |
| `captures/` | 本实验的现场采集，Git 忽略，删除实验前选择归档 |

目录外仅顶层 `CMakeLists.txt` 的一行 `include(... OPTIONAL)` 接入。`main.c`、中断文件、CAN 适配器、控制代码及 `Drivers/` 均不修改。

链接器 `--wrap` 将工程原有外部调用转到本目录的薄封装，封装记录后调用原函数一次。目标为 `HAL_CAN_IRQHandler/GetRxMessage/AddTxMessage/AbortTxRequest/ResetError` 及 `clip_cmd_parse_line`。仅记录 CAN1；CAN2 调用仍直接执行原函数。未打开新的中断、改变过滤器、自动重发、时钟、优先级、输出许可或错误计数规则。不使用动态内存、Flash 写入、阻塞等待或 ISR 打印。

HAL 的 IRQ 会同时处理多个中断原因，所以封装所有既有 `HAL_CAN_IRQHandler` 调用，而非只在 SCE 向量采样。GNU ld 包装需要真实链接验证；启用 LTO、改变工具链或 HAL 入口后，重新检查反汇编，不能只凭编译通过判断接入有效。

## 保存规则和字段

- 自动从零初始化开始记录。128 条、每条 36 B。故障前滚动保留最多 112 条（包含触发事件），首次触发后追加最多 16 条，然后冻结。首个硬件错误另外保存三个发送邮箱，计入这 16 条。
- `overwritten` 是触发前被覆盖的事件数；`suppressed` 是冻结后未保存的事件数；并非 CAN 丢帧数。冻结只停止记录，原 CAN/停止逻辑继续运行。
- ESR 的 LEC=1..6、错误警告/被动/离线状态或 MSR.ERRI 可触发；HAL 报错、提交/撤销返回非 OK 也触发。`trigger_seq` 是首次触发序号，须结合 kind/aux 判断来源，不把 HAL API 失败直接当作线上错误。
- 若触发后总线停止、未凑足后续条数，可用 `bbfreeze` 保存已有记录。无清除/重新开始命令；复位前先导出，RAM 不保证跨复位保留。
- 每条含序号、`ms`（HAL 毫秒时基）、原始 ESR/TSR、事件类型、aux、ID、DLC、flags、status 和 8 字节数据。时间是软件读取时刻，多个事件可同毫秒；序号表示保存顺序。ISR 可穿插在发送 begin/result 之间；不宣称微秒时序或硬件原子快照。
- ESR 解码：LEC=`(esr>>4)&7`，TEC=`(esr>>16)&255`，REC=`(esr>>24)&255`，BOFF=`esr&4`。TEC/REC 是控制器错误计数，不是应用累计回调数。
- 发送结果的 `HAL_OK` 仅表示请求装入邮箱，不代表对端确认。TSR 状态可能是先前请求的锁存，必须和 begin/result、邮箱及原始帧一起比较。
- `bbconfig` 是首次 CAN1 IRQ 的 MCR/BTR/IER/RCC 快照；尚无 IRQ 时 valid=0。名义时钟配置不能证明实际晶振频率。
- 记录关键区恢复进入前的 PRIMASK。首错批量保存最多四条，其他保存一条；实际板上延迟与观察扰动尚未测量。

| kind | 含义 | aux / id / status |
|---|---|---|
| 1 IRQ | HAL 处理前 | aux=MSR |
| 2 RX | HAL 成功读取的完整帧 | aux=FIFO；id=标准/扩展 ID；flags=HAL IDE\|RTR |
| 3 TX_BEGIN | 调用 AddTxMessage 前 | id/data=待提交帧 |
| 4 TX_RESULT | AddTxMessage 返回 | aux=成功时邮箱掩码，否则0；status=HAL返回值 |
| 5 HAL_ERROR | 原有 ResetError 前 | aux=HAL错误码；此时LEC可能已被HAL清除 |
| 6/7 ABORT_BEGIN/RESULT | 撤销调用前后 | aux=邮箱掩码；RESULT status=HAL返回值 |
| 8 MAILBOX | 首次硬件错误时的邮箱内容 | id=原始TIR，aux=TDTR，status=邮箱索引0..2；data=TDLR/TDHR |

不会把坏帧送入正常 RX 队列；这里也拿不到硬件丢弃的完整坏帧。邮箱空闲时内容可残留，不代表正在发送。心跳原始帧可在窗口内观察，第一版未增加控制层状态快照；如需输出许可/反馈年龄，用现有查询记录并标注查询时刻，不能冒充故障瞬间值。无法测量 CANH/CANL 波形或确定具体元件损坏。

## 构建与删除

在固件目录使用 README 已核实的 CMake/WindowsDebug 工具链，单独目录构建：

```powershell
$cmakeExe = Join-Path $env:LOCALAPPDATA 'stm32cube/bundles/cmake/4.3.1+st.1/bin/cmake.exe'
& $cmakeExe --preset WindowsDebug -B build/can-blackbox -DEXPERIMENT_CAN_BLACKBOX=ON
if ($LASTEXITCODE -ne 0) { throw 'configure failed' }
& $cmakeExe --build build/can-blackbox
```

这使用当前仓库配置。当前仓库默认 HSI/禁用 site，和此前台架 HSE 镜像不同；此产物仅完成编译验证，不能直接当作保持台架条件不变的烧录镜像。现场取证前应按已有 `docs/bench/relative-hse-20261004/` 可复现流程核对对应覆盖配置，再单独启用实验；本实验不自行切换时钟或授权运动。

2026-10-06 已完成上述HSE准备：`python experiments/can_blackbox/prepare_hse.py --output build/can-blackbox-hse-source`。脚本核验历史168个输入（仅允许顶层CMake准确增加可选实验入口），使用归档main/ioc/site覆盖，并加4个实验构建输入；不修改活动源文件，也不绕过校验去接受其他改动。输出目录必须是固件`build/`下尚不存在的新目录。设置固件README中的工具链PATH后，按历史归档的`cmake -S/-B`方法分别配置OFF/ON即可，工具不包含烧录或设备访问。

本次`build/can-blackbox-hse-source/build/OFF/Exoskeleton_F446.bin`与历史验收BIN的SHA256完全一致（`7f844000b67d9880106a54b2f3fcd3f4487e10666ad2756036fbbdfc0bff6740`）；ON版本94916字节，SHA256=`97bcd52898191c1bafb8f246de88a66881cc47b1d38b480566ead287a1769416`。结果见`hse-build-verification.json`，输入清单在生成目录`experiment-inputs.json`。未烧录，现场前仍需核实当前板上镜像/备份及24V断电状态；归档site原有运动命令存在，静态取证只使用既有查询和bb命令，不据此次构建获得新运动许可。

关闭：重新配置时指定 `-DEXPERIMENT_CAN_BLACKBOX=OFF` 后构建。普通默认构建不包含记录器或包装代码。

删除：先导出/归档需要保留的现场证据，删除整个 `experiments/can_blackbox/`，再运行 CMake 配置和构建；`OPTIONAL` 入口允许目录不存在。最后可删除顶层那条可选 include 和上级 README 链接以清理文字，无须修改主程序或业务模块。旧 ELF 不会因删除源码自动改变，应重新构建；旧实验构建产物可按原有 `build/` 规则清理。

## 导出

已有 UART 115200、逐条命令/回复，沿用原控制台整条回复空间预留，不在 ISR 输出。普通 `help` 表不改，实验命令以此处为准：

1. `bbstatus`：版本、数量、首次触发、冻结、覆盖等。
2. `bbfreeze`：冻结记录；不停止 CAN、不清除错误、不驱动电机。
3. `bbrewind`：导出游标回到0，不改变记录。
4. `bbconfig`：输出首次IRQ配置快照。
5. 反复 `bbnext`，每次一条，直至 `OK bb end=1`；未冻结返回 `ERR bb freeze_first`。

也可用 pyserial 工具（明确选择当次端口，不自动猜测）：

```powershell
python experiments/can_blackbox/dump.py --port COM10
```

工具立即冻结并导出，不负责等待/诱发故障。不要在开始观察前运行导出工具。默认目录为本实验 `captures/<UTC>/`，或使用 `--out` 指定新目录。回复不完整/格式错误立即停止，已收到的十六进制原文保留在 `raw.jsonl`；不自动重连、重试或复位。`capture.json` 保存解码事件；冻结后 UART 失败可排障后重新 rewind 导出。

## 参考与验证边界

- 对应开源索引第5/6/30行。实际读过 BetaRobot 固定版本 `c83dd84d6c7717105c948d8d12f2fbb5f4d6b48c` 的 [`bsp_bxcan.c`](https://github.com/itgz123/BetaRobot/blob/c83dd84d6c7717105c948d8d12f2fbb5f4d6b48c/bsp/bsp_can/bsp_bxcan.c) 收发及邮箱归属组织；沿用项目已有静态 CAN 适配接口，不搬入动态注册/ISR 日志。
- 实际读过本工程 ST HAL 的 `HAL_CAN_IRQHandler`（清LEC早于ErrorCallback）、`GetRxMessage/AddTxMessage` 和当前 `board_can.c/board_can_tx.c`。官方[HAL接口源码](https://github.com/STMicroelectronics/stm32f4xx-hal-driver/blob/master/Src/stm32f4xx_hal_can.c)用于交叉核查，以本地版本为实现依据。
- 实际读过现有 CLIP `clip_cmd_parse_line` 和 LwRB `lwrb_write_ex`；复用原 UART 整条回复/背压路径。参考 Zephyr v3.7.0 [`can_stm32_bxcan.c`](https://github.com/zephyrproject-rtos/zephyr/blob/v3.7.0/drivers/can/can_stm32_bxcan.c) 的错误寄存器读取思路，不引入 RTOS 驱动。
- 这些参考不是现成的本项目故障黑匣子；固定容量首次错误冻结及包装接入是用户本轮批准的诊断实现。风险是少量 RAM/执行时间和链接接入依赖，不能宣称不扰动现场时序。
- 离线测试：`python experiments/can_blackbox/tests/run.py`，使用真实记录器/封装代码及模拟寄存器，检查绕回、首错保留、冻结、HAL清除前捕获、返回值/调用次数、CAN2隔离、PRIMASK恢复、UART失败后同条重读。
- 2026-10-06：MSVC `/W4 /WX` 测试通过；ARM GCC13.3.1 Debug ON/OFF均编译成功，输出无警告。ON FLASH94916 B/RAM15264 B，OFF FLASH91148 B/RAM10600 B，增加FLASH3768 B/RAM4664 B。反汇编确认六类外部调用进入包装，OFF符号表无记录器/包装符号。
- 同日将整个实验目录临时移出源码路径（验证后恢复），独立重新配置/构建通过，大小与OFF一致；实际验证目录缺失不破坏普通构建。导出工具帮助入口和离线ESR解码检查通过；串口导出未实测。
- 未烧录、未接设备、未验证现场UART导出、ISR时延或错误复现；没有CAN根因或修复结论。进度仅追加项目唯一开发日志。
