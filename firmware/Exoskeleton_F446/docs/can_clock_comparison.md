# S2 CAN 时钟单变量 A/B 准备（2026-10-02）

当前状态（2026-10-03 01:54 UTC）：板上同源72604B HSE bypass stream候选已于01:32单次刷入/官方校验/独立读回匹配，ELF SHA256=`a6513e2eb4ffe0a6d0101f5382651c9f67c76a400e3f2b8ce3831358634b3238`、BIN SHA256=`acd3910c4cab2a8b959cfedd710cc327c1f95ea9203b9970301a79fbd290a82b`。01:33 USB-only实际HSE/PLL启动及输出0通过；01:54用户上电静止窗口完成30秒/600对/1200请求，主机保存300对有效观测，两CAN无新增错误且输出0。活动源码仍HSI，回退备份保留。此次不使用visible UI，不请求人工动作；只确认这个静止连续窗口，不能确认BR根因、人工变化或S2总验收。当前启动的stream机会已用，未经新明确窗口不得复位重试。

## 基线与对照

活动工程仍是 HSI：`H:\Desktop\Surgery_exoskeleton-git\firmware\Exoskeleton_F446`。本次来源为 b325f3c 后当前未提交 S3 工作区的完整构建输入；A/B 都包含同一份 S3 代码，不能把板上旧 S2 镜像与新 S3 HSE 镜像直接当成时钟单变量比较。该历史快照没有Arm/运动命令；最新应用已有受门控人工命令，生产仍未配置/未绑定发送，prepare/arm被拒绝。

实验快照和全部证据在 `D:\codex_tasks\2026-10-01\task\clock-ab`，它们仅供固定输入比较和归档，不替代活动工程。可交付镜像位于 `deliverables`；manifest.json 给出完整 SHA-256，文件名区分 A/B。原活动工程代码、个人设置及全部已存在未提交成果逐文件校验保留。

| 项目 | A：HSI 基线 | B：HSE bypass 对照 |
|---|---|---|
| 输入 | 内部 HSI 16MHz | ST-LINK MCO 8MHz，经 PH0/OSC_IN |
| PLL | M16 / N336 / P4 | M8 / N336 / P4 |
| SYSCLK/HCLK | 84MHz | 84MHz |
| APB1/APB2 | 42MHz / 84MHz | 42MHz / 84MHz |
| CAN1 肩侧 | 500kbps、Prescaler6 | 相同 |
| CAN2 肘部 | 1Mbps、Prescaler3 | 相同 |
| 两路 bit timing | BS1=11、BS2=2、SJW=1；采样点85.714% | 相同 |
| FLASH/RAM | 52508B / 9200B | 相同 |

两侧 PLLQ/PLLR、FLASH_LATENCY_2、电压等级、总线分频、CAN 模式/过滤器/IRQ/GPIO/错误统计、应用和调度逻辑全部保持原值。CAN 参数仍只是名义速率；本轮没有测量实际时钟或波形。`HSE_VALUE` 在原 .ioc 和 HAL 配置中已是 8000000，本次未额外更改宏。

426 个快照输入文件中，只有 `Exoskeleton_F446.ioc` 与 `Core/Src/main.c` 不同，见 clock-only.diff。IOC 显式给 PH0、PH1 设置 `HSE-External-Clock-Source` 并选择 HSE、PLLM8；main.c 只替换 CubeMX 生成的 SystemClock_Config。不是仅改 HSEState，也没有复制其他项目的 96MHz 设置。使用既有 CubeMX6.18.1/F4 V1.28.3 在隔离目录生成，对照函数与生成结果完全相同；全部 main.c USER CODE 块逐项一致。其余生成代码、Drivers、自有代码、CMake 输入沿用基线。

## 镜像标识与检查

- A BIN SHA-256：`31d91e3c62f8b2472eb669672a8044b95730d789bf14a558f6cceb4163a77d32`。
- A ELF SHA-256：`a64b103d52f5ac2068cd12954dc1a8223f168b6b2f806d89910e0dc527986ce6`。
- B BIN SHA-256：`981218829541131f4c2f676c0ad6c0139a57269e3ad5b6592df6ff4f64a75ac2`。
- B ELF SHA-256：`2dbc9f7e88c993df277981702887c25413f44448ea85ff518730645548b57d92`。
- 两份 WindowsDebug 配置/编译通过，0警告；GCC13.3.1、Ninja1.13.2、CMake4.3.1。日志在 validation/baseline 与 validation/control。
- 两份现有 tests/check_generated.py 均通过，覆盖 USART/DMA、双CAN、IRQ、初始化顺序和 App 头文件边界。
- 现有主机模拟测试 40/40 通过，MSVC W3/WX；主机测试不编译真实 SystemClock_Config，也不能证明 HSE 启动或时钟精度。
- 增补 clock-check.json：源文件差异限定、时钟等值、CubeMX一致和 USER CODE保留均通过。
- 本轮没有 App 结构变动，沿用之前 S3 机械检查已知 ARCH81/HW1 未消除结论，没有重跑或写为通过；ASan/UBSan、实际 UART/CAN、时钟测量、缺钟故障注入均未运行。

保留初次受限环境 MSVC 未取得 cl.exe 的失败日志，随后用现有本机工具环境运行40项通过。另保留首次 CubeMX 模式探测日志：仅写 HSEState 未正确激活 pin mode，生成出 HSE_OFF，被断言拒绝、未作为交付物。根据本机 RCC 模式定义改为真实 HSE bypass pin mode 后生成并验证；初次临时输出目录追加工程名的问题也已纠正。CubeMX 日志有原有 schema/IocParser 诊断，其 CLI 最终 OK；以上断言和生成检查用于验证实际产物，未把日志无错误当成依据。没有升级工具、固件包或 ST-LINK。

## UART、USB、tick 与缺钟行为

USART2 保留115200、8N1、DMA及42MHz APB1，SysTick/HAL 1ms及应用静态调度仍基于84MHz HCLK。频率相同意味着名义时序不变，不能代替硬件验证。USB VCP由板载 ST-LINK 提供并转接 USART2，目标 MCU没有启用原生 USB；.ioc 中168MHz USB/SDIO派生字段对应未启用外设，本次不修改 PLLQ 或引入 USB。

启动时先 HAL_Init，再 SystemClock_Config，之后才 GPIO/UART/CAN/App。HAL_RCC_OscConfig 等待 HSERDY 超过原有 HSE_STARTUP_TIMEOUT=100ms 返回 HAL_TIMEOUT；main.c调用原Error_Handler，CAN初始化阶段标志此时无效，因此关中断并停在无限循环。不会继续 App、CAN初始化，也不会静默回退成 HSI运行应用。HSI复位启动状态仍存在，不等于应用层回退。此时无串口状态回复或正常应用心跳是合理的失败表现，但本轮未做缺钟实测。

未启用 CSS，运行中丢失 MCO 的行为、系统停顿和恢复没有验证；本对照不承诺运行时缺钟保护或自动恢复，不能作为助力硬件的安全验收。

## 烧录前待确认的板卡条件

硬件 JSON 中正面照片记录只证实 MB1136 rev C，C04来自用户的“co4”报告，背标尚未核查；不能把默认装配当成实物。现有登记没有时钟焊桥通断证据，本轮没有找到可检查这些焊桥的已归档实物照片。

已优先提取项目归档 UM1724 Word，回查官方 UM1724 Rev17 §7.9.1（原PDF第25页）及§7.10：ST-LINK MCO固定8MHz，经PH0/OSC_IN；要求 SB55 OFF、SB54 ON、SB16/SB50 ON、R35/R37不装。C02及后续版默认MCO连接不能证明当前板没有改过。实际背标、这些桥/电阻状态、ST-LINK供电和MCO到达PH0仍待现场确认。不得盲目焊桥或同时接入另一时钟源。

官方来源：[UM1724](https://www.st.com/resource/en/user_manual/um1724-stm32-nucleo64-boards-mb1136-stmicroelectronics.pdf)。本机Word提取证据为 um1724-word.txt；板卡证据入口仍是项目 `nucleo_f446re_mb1136_c04_ai.json` 和 hardware_interface_registry.md。

## 后续一次简短的现场比较（本轮未执行）

1. 先确认上述板卡条件，并取得现场烧录/静态接收授权；保持机械状态、收发器接线/终端、驱动器FW/ID/速率及IDLE状态一致，全程不发送使能、校准或电流/力矩命令。
2. 使用本包 A 镜像（校验manifest和下载校验结果），记录输出未许可；同一启动等待后取 canstatus/shoulder/elbow 的起止快照，观察30秒原始反馈。记录 Δrx、Δerr、Form/LEC记录、FIFO/queue错误、bus-off/online与原始字节；last_err 单独重复不计作新增事件，err增量才反映新增记录。
3. 换本包 B，使用相同启动等待和30秒观察；同时核对实际 RCC PLLSRC=HSE、HSEBYP=1及PLL M8/N336/P4（只读观测安排需与现场授权一致），不要用软件计算的42MHz当作实测时钟。比较两份日志的时间与rx/err增量。B不启动或无串口时停止比较、按授权恢复A，不把空日志当成零错误。
4. 观察结束恢复 A HSI 基线并保留两份日志。若B错误消失，仅说明外部时钟假设获得支持；若两侧都无错误或错误持续，结果不够判定根因，应记录为未定，不顺带修改CAN采样点或协议。

本实验不是长时间稳定性、真实反馈语义、双总线完整验收、故障保护或 S2完成证明。

## 最新应用重新构建与HSI USB-only结果（2026-10-02）

新增准备流程/CLI/隔离实验解码后，旧52508B镜像不代表当前应用。新包为`D:/codex_tasks/2026-10-01/task/s3-next/clock-ab`，deliverables/manifest.json列出A-HSI-S3-next与B-HSE-bypass-S3-next的ELF/BIN/HEX及完整hash。保留旧目录和日志，不与旧A/B混用。

- 新A ELF：`ac5ea60571f18b754114a82ac303912bc5cc09babce3dd64593ba698d0ef6fc4`；BIN：`b76312baedbeadb8e0f420fded0d9c852e017ef9594e1d464ae549687ed2e333`。
- 新B ELF：`38f2dd1f6b1138c55c8ac90686cabea5628b07d1cacdcc32cb07af128a4a5ecc`；BIN：`483494ff06c2d6bcd569b604d2c885aaded4c5d74cc005366dd934d925a5dafa`。
- 两份配置/编译0警告，FLASH59724B/RAM9264B，生成检查通过。433输入仍只有IOC/main.c不同，其余应用完全相同。时钟设置与本文件原方案一致；未重新运行CubeMX，复用两时钟文件字节相同的既有生成一致性证据，见clock-check.json。
- 主机累计43个不同场景通过（42项首次通过+测试夹具定位修正后CLI单项通过），不是一次整体43/43执行。静态ARCH81/HW1未变、SPEC通过。
- 06:35:36–38 UTC按用户USB-only授权备份后烧录新A、读回校验和复位；06:35:59 UTC COM10七条查询/拒绝检查通过，configured=0/transport_bound=0/output_allowed=0，prepare/arm均ERR blocked。证据在新包上级usb-only。24V断开依用户报告；未发送电机命令。

新B没有烧录，实物MCO路由仍待确认；没有测量实际时钟或进行带驱动A/B，不能把此次默认安全状态检查算作HSI精度、Form根因或S2双路反馈验收。后续比较须使用本节同源新A/B，先满足上述MCO/板卡及受控静态供电条件。


## 每轴观测与门控版最新同源镜像（2026-10-02，本轮未烧录）

当前源码新增每轴启用集合、真实反馈观测及C620裸轴扇区合同，前节59724B镜像已不是当前应用。最新包为 `D:/codex_tasks/2026-10-01/task/s3-axis/clock-ab/deliverables/manifest.json`，保存A-HSI-S3-axes与B-HSE-bypass-S3-axes的ELF/BIN/HEX及完整SHA-256。旧包保留历史，不混用。最新两份FLASH64488B、RAM9312B，编译/链接0警告；初次配置0警告，身份检查补丁后的缓存再配置各有1条CMAKE_TOOLCHAIN_FILE未使用提示，日志如实保留，不称所有配置均零警告。

435个冻结输入（434个项目文件加本机忽略的CMakeUserPresets.json）只有IOC和main.c时钟段不同；两份生成检查通过。活动工程的代码/测试/配置与HSI冻结输入一致，文档更新在最终审计单独注明。CubeMX未重复运行，时钟文件字节相同的既有6.18.1生成证据继续适用。受影响主机35项在host-04一次全部PASS，包含C620 ID不匹配的真实提供者反例；另11项未变化UART/DMA场景复用前轮final-full-host的单次43/43记录，不声称本轮运行了完整46项。

本轮未访问USB、串口或SWD，未重烧HSI/HSE，未接通24V或发送执行器命令。本任务最近一次核验的板上HSI仍为前节ac5ea605镜像，当前新源与新包尚未上板；没有通过本轮读取排除外部改动。肩侧HAL Form根因、FW0.6.5档案解释兼容性、实物MCO路由和带驱动A/B仍未验证。若后续按本文件方案比较，必须先使用本节同源最新A/B，不能直接把旧板上镜像与新B作单变量对照。


## 后续现场照片与USB-only更新阻碍（2026-10-02 10:55–10:58 UTC）

已实际查看用户提供的板背JPEG，Library ID为libfile_04e4395175488191996afea83e5660ed，当前Windows证据在s3-axis/photo-review/result.json。可见MB1136 rev C以及SB16/SB50/SB54的0欧器件、SB55空焊盘；精确C04、R35/R37、电气连通及实际MCO频率仍未证实。该照片补充了此前仅正面的记录，不能替代通断/时钟证据，不能据此切换HSE。

用户明确24V关闭后尝试更新最新A-HSI。正确ST-LINK和COM10父序列号已核对，但Hot Plug读取芯片身份报Unable to get core ID，Normal同速率诊断报DEV_USB_COMM_ERR；应用仍能通过三条串口查询、输出禁用。**该10:55–10:58窗口未执行Flash备份或写入**，已被同日13:27 HSI成功烧录及独立读回结果取代。失败原文及查询见s3-axis/usb-only-20261002/session-result.json。本轮没有验证HSE启动或Form根因；先恢复SWD，再按原约束更新HSI，禁止用当前旧板与新B直接比较。


## 当前连续编码器观察应用的同源候选（2026-10-03 01:19–01:24 UTC，仅离线）

为避免旧应用与新应用混比，重新固定当前活动工程的462个输入文件，其中144个固件构建输入逐项匹配已验证板上连续观察镜像。A/B只在`Core/Src/main.c`的SystemClock_Config与`Exoskeleton_F446.ioc`不同；28个USER CODE块、CAN参数、stream请求与时限、NART=1、错误即停、S3默认配置/绑定/输出0及全部保护均相同。只复制前次CubeMX6.18.1实际验证的时钟文件；当前HSI main.c/IOC与那次基线字节相同，未重复生成。原活动工程、个人设置和已有改动保留，候选仅在`D:/codex_tasks/2026-10-01/task/s2-stream-clock-ab/`。

| 当前候选 | A：HSI stream | B：HSE bypass stream |
|---|---|---|
| 输入/PLL | HSI16MHz/M16/N336/P4 | ST-LINK MCO8MHz/M8/N336/P4，经PH0 |
| HCLK/APB1/APB2 | 84/42/84MHz（名义） | 相同 |
| CAN1/CAN2 | 500kbps/1Mbps，原分频6/3及11/2/1时间段 | 相同 |
| FLASH/RAM | 72604B/9640B | 相同 |
| BIN SHA256 | `993161f700860e0c4178e7eb767698eb863714763a01a88c89302cd360613e6d` | `acd3910c4cab2a8b959cfedd710cc327c1f95ea9203b9970301a79fbd290a82b` |
| 重建ELF SHA256 | `7f7cba68034421c21dda7af7041a5d96f4ab814f2fe9d90d13653555a5cba787` | `a6513e2eb4ffe0a6d0101f5382651c9f67c76a400e3f2b8ce3831358634b3238` |

两版WindowsDebug配置、编译、BIN/HEX转换和tests/check_generated.py均退出0，编译未报告warning；独立差异审查通过，生产S3Site及EncoderStream符号在两版中，无TestS3符号。新A ELF的调试路径不同，使用相同BIN确认应用相同；不以ELF哈希相同作为要求。应用逻辑未变，未重复已通过的主机功能测试；本轮未运行全静态扫描或ASan/UBSan。完整ELF/BIN/HEX/MAP、hash与检查证据见`deliverables/manifest.json`，原已验证HSI ELF/BIN另以`ROLLBACK-verified-HSI-stream.*`保留。

已通过当前Library传输工具取得用户两张照片并实际看像素。背面图与此前照片字节相同，可见SB16/SB50/SB54有0Ω器件、SB55空；新正面图中U5左侧C29下方四行器件组的丝印顺序为R34/R36/R35/R37，上两行有0Ω、下两行空焊盘，故R35/R37外观未装；X3晶体位空、ST-LINK X1晶体标8.000且在位。与Hardware_information_list、C04原理图第3页及UM1724 Rev17第7.9.1节条件相符。这只确认装配外观，不确认导通、实际MCO波形或精确C04背面子版本。照片与独立审查见`photo-review/result.json`、`independent-review.json`；也不能由照片推定当前24V关闭或USB已拔。

本轮没有连接硬件、烧录、复位、测钟或运行连续采集。HSE启动缺钟仍按原HAL100ms超时进入Error_Handler，发生在CAN/App初始化前，无应用HSI自动回退；CSS未启用，运行中丢MCO的保护/恢复未验证。候选仅供输出门控0的静态诊断。以后只有在明确的动力断电与烧录窗口下才能切换；B启动/身份/时钟检查失败、UART消失或出现新CAN错误/ABORT即结束比较，不自动重试，按已授权的断电恢复步骤还原归档HSI并独立读回。短窗口无错只支持时钟假设，不能宣布根因或S2完成。用户取消的掉线恢复仍未执行；GitHub未提交/推送。


## HSE stream USB-only单次刷入与实际启动（2026-10-03 01:31–01:33 UTC）

用户在明确“24V关闭、USB接回”请求后回复“可以刷入”，主线程授权一次刷入/校验/独立读回/启动时钟与输出禁用核查；未独立测量24V。固定SN066EFF555748874987182240/F446 ID421/512KB/V2J48M35核对成功。512KB刷前备份SHA256=`a21d2623f6a4f5d4ed0df6dfd1255447a617b2e7892e34b65531afc3873a68e2`，前72604B与已验证HSI BIN993161f7完全相同；144固件输入与462候选输入复核通过。

01:32:46.315438–01:32:49.029262 UTC官方-w/-v/-rst退出0，Download verified successfully及软件复位记录存在；仅一次刷写，无重试/回退。01:32:56.874049独立读回72604B，SHA256=`acd3910c4cab2a8b959cfedd710cc327c1f95ea9203b9970301a79fbd290a82b`与指定BIN匹配。01:33:04单次HOTPLUG只读RCC CR/PLLCFGR/CFGR=`03077B83/22415408/0000100A`：HSEON/HSERDY/HSEBYP、PLLON/PLLRDY均1，PLLSRC_HSE1、M8/N336/P4/Q2/R2、SWS2，HCLK不分频、APB1÷2、APB2不分频。证实外部旁路时钟与PLL实际启动并被选中；84/42/84MHz仍为根据8MHz名义输入计算，不是示波器测频/抖动结果，CSS仍0。

01:33:24固定父SN健康COM10重新绑定；01:33:34六条ping/status/canstatus/outputstatus/shoulderreadstatus/encfeedback缓存查询通过。两CAN started1/start_error0，configured/transport_bound/output_allowed/request_valid/arm_token均0，UART/调度错误0；SDO idle/seq0，stream active0/reason0/pairs0/tx0。没有encstart、CAN属性请求、使能、参数/模式写或运动。本次启动检查不能证明有电机反馈，也不证明BR根因已解决。原始命令、备份、读回、RCC、串口和结果在`D:/codex_tasks/2026-10-01/task/s2-stream-clock-ab/usb-only-20261003T013012Z/`。

保持24V关闭。本次候选没有混入其他审查的源码修改；主线程已报告当前visible capture对异常JSON事件存在未捕获异常，可使提示停更，故现场动作指导暂缓，待另一对话修复并验证后再在有人现场的一次有界窗口检查连续肩反馈与手动变化。没有执行现场连续采样、缺钟试验或掉线恢复，GitHub未提交/推送；S2继续未通过。


## HSE上电静止连续窗口（2026-10-03 01:54 UTC，仅一次）

用户在主线程要求“开24V、机构保持不动、只读反馈、不驱动且不用绿色提示”后回复“打开了”。原capture.py/ps1不依赖visible UI；通过独立审查的静止包装入口执行一次，旧READY_FOR_MOVEMENT仅保留为协议就绪记录，未向用户发出动作许可。没有修改固件/保护、复位或重试，尚未独立测量24V。

终态为正常有界结束：active0/reason2/elapsed30000ms，固件完成600对、提交1200个请求，rejected0；主机以约10Hz保存300对不同有效观测，不能把600写成保存样本数，也不能把tx计数等同已发成功帧数。有效观测配对时间戳推进、有限量值与新鲜度检查通过；末尾valid0属于会话结束状态，不是中途失效。肩RX56401→60916（+4515），肘RX516780→547227（+30447）；两路err/drop/invalid/fov均0增量，last_err00000000、bus_off0。全部已记录输出许可0，encstop收尾确认成功。

静止位置2.955029–2.955193rev，范围0.000164rev，是观察到的微小静止波动，不能作为手动变化、方向/单位或S2整体通过证据。相较先前HSI的4952ms/BR中止，本次HSE30秒无新增错误支持进一步检查时钟来源假设；不同时间的一次窗口并非物理根因证明或长期稳定性验收。原始1172事件和summary在`tests/encoder_capture/evidence-static-hse-20261003T0153385075330Z/`；整理结果及独立审查在`D:/codex_tasks/2026-10-01/task/s2-stream-clock-ab/static-window-20261003T0153384983536Z/`。用户取消的掉线恢复未做，人工变化未做，GitHub未提交/推送。

此启动的stream机会已用，当前静止完成即停止，不自动复位/重试或驱动。S2后续人工变化仍需另一个明确现场窗口，动作指导不得使用当前存在异常事件缺陷的visible UI；本次用户允许绕过另一个对话只做静止，不等于修复或取消该缺陷。
