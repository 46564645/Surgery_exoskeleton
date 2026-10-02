# Exoskeleton_F446 固件入口

当前唯一开发目录：本文件所在目录，即仓库内的 `firmware/Exoskeleton_F446/`。旧 Mac 桌面 STM32Projects 中的原工程为历史备份，不继续并行修改。

## Windows 工具准备

当前 Windows 已验证的版本为 CMake 4.3.1、Ninja 1.13.2、ARM GCC 13.3.1；CubeMX 6.18.1、STM32CubeF4 V1.28.3。GCC 14.3.1 是此前 Mac 的工具记录，不是当前 Windows 的实测编译器。本次迁移沿用现有工具版本。

- 固件构建需要 CMake、Ninja 和 ARM GCC。所需 HAL/CMSIS、LwRB/CLIP 源码已在固件目录内，参考子模块不是固件构建的必要依赖。
- VS Code 使用 CMake Tools 和 C/C++ 扩展。在 **Preferences: Open User Settings (JSON)** 中设置 `cmake.cmakePath` 为本机 CMake 可执行文件的完整路径；合并这一项，保留其他用户设置。不要把个人安装路径写入共享项目设置。
- 软件检查使用 `python`。主机模拟测试还需要 Visual Studio C++ Build Tools（MSVC）及 Windows SDK/UCRT；脚本会查找已安装的 MSVC。ARM GCC 编译开发板程序，MSVC 编译电脑上运行的模拟测试，两者用途不同。

固件文件夹设置 `cmake.useVsDeveloperEnvironment: "never"`，使 CMake Tools 使用 ARM 预设环境。本机实测其默认 `auto` 会自动导入 MSVC 环境，导入后的 PATH 含非法空字符，导致编辑器配置任务无法启动 CMake；原终端环境无此问题。该设置只作用于固件 CMake 工程，主机测试脚本仍自行加载 MSVC。另须确认安装的是 `ms-vscode.cpptools` 主扩展，只有扩展包或主题不能提供 C/C++ 代码提示。

本机 CMake 用户设置示例（换电脑需按实际安装位置调整）：

```json
"cmake.cmakePath": "C:/Users/asus/AppData/Local/stm32cube/bundles/cmake/4.3.1+st.1/bin/cmake.exe"
```

此设置只告诉 VS Code 扩展到哪里找 CMake，不会修改 PowerShell 的 PATH。终端的完整路径和临时 PATH 用法见下文。

## 打开工程

- VS Code：打开总项目根目录的 `Exoskeleton.code-workspace`，代码在 `Firmware — F446` 下，资料在 Project 下。多文件夹工作区中，用 **CMake: Select Active Folder** 选择 `Firmware — F446`，再选择预设。也可单独打开本目录。
- CubeMX：打开本目录的 `Exoskeleton_F446.ioc`。生成前确认 Project Location 指向本目录、工程名为 Exoskeleton_F446、Toolchain 为 CMake、编译器为 GCC，保留用户代码；不要再向桌面备份生成。
- 两个软件共用本目录，无需复制同步。CubeMX负责外设初始化，VS Code负责应用代码及构建。

## 选择本机预设

预设是一组已命名的构建设置。共享 `CMakePresets.json` 保留 `Debug/Release`；Windows 本机的 `CMakeUserPresets.json` 定义 `WindowsDebug/WindowsRelease`，继承共享设置并补充 Ninja、ARM GCC 的 PATH。个人预设已被 Git 忽略，不随克隆分发。

本机已有个人预设，继续使用即可。新电脑可在本目录创建以下 `CMakeUserPresets.json`，先将两个示例 `bin` 路径替换成实际位置；已有文件时只合并所需项，不覆盖其他预设。`$penv{PATH}` 保留原终端环境，Windows 路径列表用分号分隔。

```json
{
  "version": 3,
  "configurePresets": [
    {
      "name": "WindowsDebug",
      "inherits": "Debug",
      "environment": {
        "PATH": "C:/Tools/ninja/bin;C:/Tools/arm-gnu-toolchain/bin;$penv{PATH}"
      }
    },
    {
      "name": "WindowsRelease",
      "inherits": "Release",
      "environment": {
        "PATH": "C:/Tools/ninja/bin;C:/Tools/arm-gnu-toolchain/bin;$penv{PATH}"
      }
    }
  ],
  "buildPresets": [
    { "name": "WindowsDebug", "configurePreset": "WindowsDebug" },
    { "name": "WindowsRelease", "configurePreset": "WindowsRelease" }
  ]
}
```

VS Code 中先运行 **CMake: Select Configure Preset**，选择 `WindowsDebug`；再运行 **CMake: Select Build Preset**，选择对应的 `WindowsDebug`。Release 同理，配置与构建预设应成对切换。`Debug/Release` 也可使用，但要求终端或扩展环境本身已能找到 Ninja、ARM GCC。

## 构建与代码提示

**PowerShell：** 从项目根目录进入固件目录，然后运行以下命令。CMake 路径是本机已核实的位置，换电脑需调整；每步退出码非 0 时停止，先检查该步输出。

```powershell
Set-Location 'firmware/Exoskeleton_F446'
$cmakeExe = Join-Path $env:LOCALAPPDATA 'stm32cube/bundles/cmake/4.3.1+st.1/bin/cmake.exe'
& $cmakeExe --list-presets=all
if ($LASTEXITCODE -ne 0) { throw 'CMake preset lookup failed' }
& $cmakeExe --preset WindowsDebug
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
& $cmakeExe --build --preset WindowsDebug
if ($LASTEXITCODE -ne 0) { throw 'CMake build failed' }
```

如希望在同一个终端直接输入 `cmake`，可临时加入 CMake 目录；关闭终端后不保留，Ninja/ARM GCC 仍由个人预设提供：

```powershell
$env:PATH = (Split-Path -Parent $cmakeExe) + ';' + $env:PATH
cmake --version
```

**VS Code：** 选择好预设后按 `Ctrl+Shift+B`，运行默认任务 `Firmware: Build`；它先执行 `Firmware: Configure`，再编译。两个任务使用 CMake Tools 原生任务和当前预设，不依赖 shell 脚本。头文件、宏和编译器信息由 CMake Tools 提供给 C/C++ 扩展；切换预设后重新配置，使代码提示更新。

`WindowsDebug` 产物位于 `build/WindowsDebug/`，主文件为 `Exoskeleton_F446.elf`；`WindowsRelease` 对应 `build/WindowsRelease/`。`build/` 为可重新生成的产物，不提交；换电脑不要沿用其他电脑的构建缓存。

配置方式依据：[CMake 共享/个人预设](https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html)、[CMake Tools 原生任务](https://github.com/microsoft/vscode-cmake-tools/blob/main/docs/tasks.md)、[C/C++ 代码提示配置](https://code.visualstudio.com/docs/cpp/configure-intellisense)。

## Windows 软件检查

以下命令均在固件目录运行，不连接硬件：

```powershell
python tests/run_host_tests.py
if ($LASTEXITCODE -ne 0) { throw 'Host tests failed' }
python tests/check_generated.py
if ($LASTEXITCODE -ne 0) { throw 'Generated configuration check failed' }
```

主机测试使用模拟 HAL，不代表板上 DMA 或 IRQ 已验证。Windows 使用 MSVC C11；此前 Mac/POSIX 的 ASan/UBSan 内存错误检查结果不能算作 Windows 上的执行结果。

辅助检查入口为 `python .auto-embedded/scripts/check.py --json`。已知其默认 Windows PowerShell 5 扫描存在 UTF-8 无 BOM、GetRelativePath 兼容问题；S2 当时用 Git Bash 执行包内 `arch-check.sh` 补查，并人工说明生成代码/构建目录及同优先级 IRQ 告警。该入口未全绿，不能据此宣称所有检查通过，详见[该轮记录](.auto-embedded/tasks/s2-feedback-plan/edits.md)。

## Windows 串口检查（当前 S2 无驱动器场景）

`tests/bench_can_serial.py` 使用 pyserial，只发送查询命令。适用条件为当前 S2_CAN_RX 固件、仅 USB 供电、24V 断开、H/L 未接驱动器，且两路从启动起尚未收到 CAN 帧。它要求 `started=1`、`seen=0`、接收/错误计数为 0；接入驱动器或已经收过帧时，不使用此脚本判断通信是否正常。

本机 pyserial 已隔离安装在被忽略的 `build/s2-tools/python`。新电脑缺少该依赖时，可先运行 `python -m pip install --target build/s2-tools/python pyserial`。在固件目录临时设置模块路径并列出端口：

```powershell
$env:PYTHONPATH = (Resolve-Path 'build/s2-tools/python').Path
python -m serial.tools.list_ports -v
```

确认上述现场条件后，替换下例端口为当次枚举的 ST-LINK VCP 端口，再执行：

```powershell
python tests/bench_can_serial.py --port COM10 --seconds 60 --report build/s2-validation/usb-soak.json
```

COM10 只是此前实测端口，不能假定永久不变。串口参数为 115200、8N1、无流控。脚本不烧录、不复位、不使能；失败也会保存报告，未执行的检查不得记为通过。此前 S1 的 10 分钟测试、SWD 校验和重插观察见开发日志，不由此脚本替代。

## 当前基线及编辑边界

- NUCLEO-F446RE / STM32F446RET6；ioc记录 CubeMX 6.18.1、STM32CubeF4 V1.28.3。
- 当前 Windows 工具版本见上方工具准备；此前 Mac 使用 GCC 14.3.1+st.2 的记录作为历史结果保留。
- S1已基本完成；S2原始双CAN接收基础已接入并完成USB板测，状态S2_CAN_RX。2026-10-01按用户批准先实现C620肘侧只读解析；GDS68解析、HX711、StateSmith与电机控制尚未接入。新解析软件与当前板上已烧录镜像分别验证，不将离线构建当作已烧录。
- App/application包含调度和命令；Core中的board_console/board_runtime提供平台接口，应用层不持有HAL句柄。App/devices含C620接收解析，App/control仍预留；不改变CAN物理层配置。
- Drivers不手改；Core中的接入代码放USER CODE区。自有源文件加入顶层CMakeLists.txt；不手改生成的cmake/stm32cubemx清单。
- 顶层AGENTS.md及硬件接口登记仍为工作依据。根目录到这里的路径为 firmware/Exoskeleton_F446。
- 电机与 CAN 模块的详细连接见[图纸文字说明](<../../Main_ project/Hardware_information_list/motor_can_diagrams_text.md>)；板卡引脚和接口的结构化资料见[C04 JSON](<../../Main_ project/Hardware_information_list/nucleo_f446re_mb1136_c04_ai.json>)，仅为项目接口摘录。
- AI 读取手册时优先对应型号/版本且正文可读的 Word；入口见文字说明第0.1节。针号、接头方向和线路核对原 PDF/原理图，当前接线及验证状态以接口登记为准。

## 已有辅助配置

原 `.agents`、`.auto-embedded`、`.codex` 原样保留；迁移不代表硬件资源已锁定或hooks已在新位置启用。根项目打开时不会自动保证加载嵌套工程hooks，本次不更改用户全局信任或工具配置。

## 验证边界

2026-09-25：Mac端Debug构建及13项主机模拟测试通过，CubeMX重新生成后自有代码保留。Mac端SWD连接报USB错误，虚拟磁盘HEX写入尝试不等于读回校验通过。Windows端现已完成S1烧录校验、IRQ优先级读回、三次复位后的串口查询与10分钟通信测试。用户报告物理USB重插后，VCP命令恢复，但SWD报`DEV_USB_COMM_ERR`；Windows系统级重新枚举后SWD恢复，根因未定。用户目视LD2约每秒闪一次，与500 ms翻转、完整亮灭周期约1秒的设定定性相符，未用仪器测量时序。用户已决定S1基本完成，SWD偶发异常作为已知问题按需处理，不阻塞S2；详见唯一开发日志。

## 通信行为（沿用 S1，当前状态为 S2）

- USB VCP使用USART2 PA2/PA3，115200、8N1、无流控；支持无参数的`ping`、`status`、`help`、`?`、`canstatus`、`shoulder`、`elbow`、`elbowfeedback`，区分大小写，以LF或CRLF结束。新命令需要包含C620解析的新固件，当前未安排烧录。
- `status`固定报告`stage=S2_CAN_RX output_allowed=0`；无电机控制命令。正常LD2每500 ms翻转；启动核验失败或通信故障每100 ms翻转，复位恢复。
- DMA1 Stream5/Channel4循环RX、Stream6/Channel4普通TX；与USART2同抢占优先级5。BSP按键初始化后由`BoardRuntime_FinalizeInterrupts`将EXTI最终设为10并读回核验；SysTick为0，GROUP4。
- 参数集中在`Core/Inc/s1_config.h`。RX DMA为512字节，LwRB RX/TX存储1024/2048字节（各保留1字节区分满空），TX暂存256字节。中断服务延迟须小于半个DMA数组接收时间（115200下约22 ms）；位置差不能识别完全未服务的一整圈。
- 通信每1 ms处理最多128字节及一条完整命令；行最多96字节，非法/超长行整行丢弃。UART/DMA错误、RX溢出或TX超过100 ms锁存故障，停止命令处理。

## S2原始CAN观察（2026-09-25）

端口/现场线色维护在[硬件接口登记](<../../Main_ project/Hardware_information_list/hardware_interface_registry.md#102-逐线连接表>)。CAN1为500kbps准备值，接GDS68前须核实；CAN2为1Mbps。两路正常模式，仅标准数据帧；无应用发送、无设备协议解析、无电机在线判断。每路64帧队列，满时丢新帧；每次ISR最多3帧，主循环每1ms每路最多16帧。预算在Core/Inc/s2_config.h，位时序和引脚在.ioc。

| 命令 | 返回内容 |
|---|---|
| canstatus | 两路启动状态、速率、start_error及output_allowed=0 |
| shoulder / elbow | 对应原始总线的rx/drop/invalid/fov/err/last_err/bus_off/queued；最近帧seen/id/dlc/data/age_ms |

start_error：0成功，1初始化失败，2过滤器失败，3启动失败，4通知启用失败。started表示启动成功，不等于电机在线；总线故障另查bus_off和错误。rx计通过帧格式检查的接收帧（含随后因软件队列满而丢弃的帧），drop为软件丢新帧数，fov为硬件FIFO溢出事件次数（不等于精确丢帧数），err为错误事件次数，last_err为最近HAL错误位图。统计饱和于UINT32_MAX，复位清零。从未收帧时seen=0、id/age_ms=na、data=-；收到后年龄用uint32毫秒差值计算，跨tick回绕有效，超过一个完整tick周期无法区分。

初始化错误通过USER CODE钩子保留串口查询能力；其他时钟/UART错误保持原Error_Handler行为。LED仍用于S1心跳/串口故障，不用LED代替CAN状态查询。

Windows本机另装CubeMX6.18.1于`%LOCALAPPDATA%/ST/STM32CubeMX-6.18.1`，旧版本保留；F4包位于`%USERPROFILE%/STM32Cube/Repository/STM32Cube_FW_F4_V1.28.0`，package.xml的Patch=1.28.3（官方基础包+补丁目录约定）。CLI生成须用Windows原生反斜杠的project path，正斜杠曾导致重复盘符。生成保留CoupleFile=false，CAN初始化在main.c；不要手动搬到新can.c或改生成CMake。

测试：`python tests/run_host_tests.py`，本轮MSVC 25项通过，未在本机运行ASan/UBSan；`python tests/check_generated.py`通过，6.18.1重新生成后所有自有代码和USER CODE块一致。WindowsDebug：FLASH 47564 B、RAM 9008 B、零警告；已用原CubeProgrammer2.23流程烧录校验，未升级ST-LINK。

本轮60秒600次持续查询通过，串口/CAN错误和调度超时均0，output_allowed始终0；这是接收基础验证，未验证真实CAN报文、终端、电压、驱动器反馈或运动。下一步借万用表核实供电和基本保护，查清GDS68版本/ID/速率/启动设置，再接入驱动器。

## 2026-10-01 肩侧原始帧观察（S2 仍进行中）

用户报告 GIM 已接通 24 V 后，COM10 只读查询显示肩侧 CAN1 的 `rx` 持续增长，最近帧为 `0x029`、DLC 8、全零载荷、年龄 2–9 ms；肘部 CAN2 当时无帧。`output_allowed=0`，未发送使能或运动命令。肩侧 `err` 从 1 增至 11，最后一次 HAL 错误位图为 `0x00000010`（帧格式错误）；这些计数没有给出每次错误的时间或原因。`0x029` 仅在实物确认为 CAN Simple 后才能按节点 1 的编码器估计帧解释，全零载荷尚不能作为有效位置/速度反馈。完整实测边界见[唯一开发日志](<../../Main_ project/docs/development_schedule.md>)。

下一步先核对 GDS68 实际版本与 CAN 配置、断电终端/共地，并做针对性的静态错误复测。GDS68 手册指出 Type-C 不单独供电；USB 读取实际配置仍需受控 24 V 静态供电，且不得发送电机控制命令。旧版无驱动器场景的 `bench_can_serial.py` 不适用于判断已接收过帧的链路。

## C620肘侧只读反馈（2026-10-01软件实现）

用户批准先完成肘侧解析和离线测试，再单独排查肩侧0x10；S2仍以两路完整验收为准，详见唯一开发日志。原始`elbow`和`shoulder`查询保留；`elbowfeedback`仅显示C620转子反馈及新鲜度，无电机控制接口。接入主循环后逐帧解析，每路原有预算不变，不只解析每批最后帧。

配置集中在`Core/Inc/s2_config.h`：电调ID1（实物最新帧0x201），超时100ms（台架暂定值，约100个已观察到的1kHz周期，仍需确认实际周期和真机边界）。位置单位rad、速度rad/s是电机转子量；未知传动比、方向和零位时不换成肘关节量。电流保留有符号`current_raw`，不声明A。

命令输出含`seen/valid/online`、`rotor_pos_rad/rotor_vel_rad_s`、`angle_ticks/rpm/current_raw/temp_c`、`received_ms/age_ms`和`output_allowed=0`。从未有样本时数据和时间显示`na`；离线后缓存保留，但`valid=0 online=0`，收到新的及时有效帧才恢复。无效帧不刷新时间戳；角度8192及以上拒绝，标准DATA由L1核验。历史HAL错误保持在原始`elbow`统计里，不被成功解码清零。

串口通过整数定点文本输出rad（6位小数）和rad/s（3位小数），不增加newlib-nano浮点printf；内部值仍为float32 SI单位。主机测试验证符号、单位、真实快照、非法/陈旧/乱序帧、100/101ms边界、超时恢复、回绕、双路隔离、接收预算及最大长度查询。软件通过不代表新代码已烧录、链路无错或独立真值/断连恢复已验收；现场仍有两次未归因CAN错误。

## Mac / POSIX 兼容入口

以下入口保留，Windows 使用前文流程。Mac 上将 CMake、Ninja、ARM GCC 加入 PATH；也可自行创建被忽略的 `.local-tools.sh` 供 `build.sh` 加载。该文件为可选本机配置，不能假定新电脑已有。

在固件目录执行：

```sh
./build.sh Debug
python3 tests/run_host_tests.py
python3 tests/check_generated.py
```

Release 使用 `./build.sh Release`。VS Code 同样可选择共享 `Debug/Release` 或自己的个人预设，使用原生构建任务；本轮未在 Mac 上重新验证。

Mac 串口通常使用 `/dev/cu.*`，Linux 通常使用 `/dev/ttyACM*`；均按当次枚举结果选择。当前 S2 的跨平台脚本及使用条件见前文，将端口参数替换成对应设备路径即可。旧 `tests/bench_serial.py` 依赖 POSIX `termios`，保留为历史 S1 脚本，不作为当前 Windows/S2 操作入口。
