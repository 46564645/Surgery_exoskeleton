# Exoskeleton_F446 固件入口

## 肘通道幅值 2000 → 10000（2026-10-27，已编译未刷写）

用户按仿真扫描结果决定把 R5 肘通道幅值由 2 A 提到 **10 A**（M3508 额定电流）。仿真（T1 动作，模型假设下）显示总省力由 6.9% 升到 8.8%、肘部由 1.5% 升到 7.1%；肩侧在所有幅值下都稳定在 10%，说明剩余缺口只在肘侧。

- 构建：`cmake --preset WindowsDebug -B build/r5-ma10000 -DS5_R5_DUAL_FOLLOW=ON -DS5_R5_ELBOW_SUPPORT_SIGN=1 -DS5_R5_ELBOW_SUPPORT_MA=10000 -DS5_R5_SHOULDER_AMPLIFY_MNM=0`，其余选项与板上镜像相同。
- 产物：`build/r5-ma10000/Exoskeleton_F446.elf` 与 `.bin`，FLASH 141896 B、RAM 12928 B、0 警告；BIN SHA256 `5D461E3935CBF6FDD81F4ABA8DB7C732990E91AA312553463825137FA7F2C4A4`。
- 验证：主机测试 124 项通过；**未刷写、未上板**，板上仍是下方 2026-10-21 的 `MA=2000` 镜像。刷写时按既有流程做一次有界窗口（备份→写入→独立读回）。
- 扫描工具：`sim/mujoco_arm/scripts/sweep_elbow_ma.py`（只重建仿真用控制库，不改源码）。

## HX711 临时力值与去皮（2026-10-17）

当前板上为 [R5 肘姿态相关重力补偿 + 放宽限值候选](build/s5-r5-limit45-20261021/Exoskeleton_F446.elf)（2026-10-21 刷入），BIN SHA256 `241c006a71d42ecf814e75a3b36188b0f4d129b2aa885d8bbdc368a2ddc80cf2`，已刷写并独立读回一致，上电 `phase=1`。R5 构建选项 `S5_R5_DUAL_FOLLOW=ON`、`S5_R5_ELBOW_SUPPORT_SIGN=1`、`S5_R5_ELBOW_SUPPORT_MA=2000`（sin 曲线峰值）、`S5_R5_SHOULDER_AMPLIFY_MNM=0`。肘电流为 `峰值 × sin(肩角+肘角)`：垂臂约 0 A，前臂抬平方向达峰值。
- 2026-10-21 放宽后的 R5 限值：软限肩 90°/s、肘 150°/s；硬限肩 150°/s、肘 240°/s；`resume_quiet_ms=250`；下限肩 −20°、肘 −30°；会话 45 s。肩读取窗口相应由 55 s 改为 70 s（必须大于 `prepare_timeout + session`）。
- 上一版（30 s 会话、软限 60/90、硬限 120/180）BIN `87afce7f…789934`，2026-10-21 一次 8 s 窗口全程 RUNNING 并正常停止；更早的 HX711 力值镜像 `CDB40497…ED7D006` 见 `build/s5-r5-hx711-force-20261017/`。

- `hx711`：保留 `seen/seq/raw/sat/age_ms`，增加 `valid/force_N/zero_raw/cal=temporary`。`force_N` 单位为牛顿，保留三位小数只是显示分辨率，不代表测量精度；`zero_raw` 显示四舍五入的计数。
- `hxtare`：仅在无外加载荷时调用，立即将最新新鲜且未饱和的样本设为零点，返回 `OK hx711 tared ram_only=1`。只存在RAM，重启恢复标定零点；程序不会自动判断是否空载。
- 无样本、饱和或样本年龄≥500 ms：`valid=0 force_N=na`，去皮返回 `ERR hx711 no_fresh_unsaturated_sample`，原零点保持不变。
- 临时系数集中于 `App/devices/hx711.c`：空载均值144811.46、452 g均值−55221.87，`F_N=(zero_raw-raw)/45127.7057`。用户接受本次一点标定，不追加回零或多点挂载验证；不使用此前的34 g系数。改变固定方式或加载点后，原系数不自动适用。
- 保持10 SPS辅助测量用途。该力值不用于电机控制或故障保护。去皮、超时和符号换算已由主机测试验证；本轮板上只查询力值，未执行去皮、未追加挂载或启动电机。

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

本机已有个人预设，继续使用即可。新电脑可在本目录创建以下 `CMakeUserPresets.json`，先将两个示例 `bin` 路径替换成实际位置；已有文件时只合并所需项，不覆盖其他预设。仓库内的 `CMakeUserPresets.example.json` 是同一份模板，另含 macOS 预设（`MacDebug`/`MacRelease`），复制改名后按本机路径修改即可。`$penv{PATH}` 保留原终端环境，Windows 路径列表用分号分隔。

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
- 当前状态（2026-10-15）：板上为已独立读回的 R5 双关节隔离镜像，BIN SHA256 `A9EF99B746CDBA0A5EC1E3B898C4BD24A88362C5F11C03A3ED88226D0A586287`，肘命令帽 `20 A`、目标 `2 A`。放宽重复静止门后，一次窗口进入 RUNNING 约 4.5 秒并正常停止，但操作者未抬肩，肩目标始终为零；另两次在启动约 0.17 秒后触发 `SAFETY_SPEED`、正常停止未获确认。最新故障后 RAM 确认肘反馈速度约 `36°/s`，超过 `20°/s` 门槛；这不能证明是真实转动还是反馈瞬态。另有一次准备前肩心跳缺失，以及旧肩 `S3_READ_TX` 故障，原因待分。当前驱动器 24 V 已关闭，双轴助力效果未证明。生产 S4/S5 profile 仍为 `enabled_axes=0`、`output_enabled=false`；细节见[唯一开发日志](<../../Main_ project/docs/development_schedule.md>)。
- S5 配置：R4 肘单轴隔离 site 构建时须显式给出肘支撑方向 ±1／幅值 `1–4000 mA`。R5 双轴隔离 site 的目标电流由方向 ±1 和幅值 `1–20000 mA` 显式选择，独立的肘 CAN 命令帽为 `20 A`。当前板上旧 R5 镜像选 `2000 mA` 目标、最长 `10 s` 会话及最多 `1 s` 渐退；它仍有已确认的肘速度故障。`20 A` 是 C620 协议满量程，不是裸 M3508 的已验证工作电流或关节扶持力。普通生产构建仍禁用输出。
- 待上板的 [R5 判断精简候选](build/s5-r5-fast-gates-20261015/Exoskeleton_F446.bin) 在保持 `2000 mA` 目标和 `20 A` 命令帽下，肩取零用两次不同且新鲜的近零速位置／速度配对，去掉准备期额外的 20／200 ms 静止等待和细位置跨度；停止后的肩侧 200 ms 静止观察保留。忽略肩 CAN1 历史丢帧／无效帧／FIFO 溢出计数及准备入口的队列空检查；双轴网关不再以 250 ms 目标样本年龄单独撤权。原始反馈时效、当前总线与串口故障、CAN 发送结果、停止帧提交、运行速度／角度及固定命令帽仍保留。最长渐退改为 `2 s`，实际仍按 `0.08 Nm/s`／`25 A/s` 下降；脚本 READY 后直接进入启动流程，准备与停止观察上限各 `5 s`。这只是离线软件候选，板上仍是上一条所述镜像，尚无新候选的带电结果。（2026-10-15 已刷入并有两次带电窗口，见开发日志。）
- 2026-10-16 [R5 速度软限候选](build/s5-r5-soft-speed-20261016/Exoskeleton_F446.bin)（BIN SHA256 `40D20843574841EAE294EB77A6EAF1EE3C9C3D46C5579E2E458F4C186ED97C7E`，未刷写）在上一候选基础上改了三处：1）运行期超过软限（肩 `60°/s`、肘 `90°/s`）时两轴目标置零但不故障；所有轴回落到软限一半以下并持续 `100 ms` 后，在 `300 ms` 内渐升回原目标，思路取自 ExoKit `AmplifyReaction::runLoop()`。只有超过硬限（肩 `120°/s`、肘 `180°/s`，原为 `40°/s`／`20°/s`）才锁存 `SAFETY_SPEED`。2）`followstop` 时人仍在动不再因速度／范围直接故障，改为正常渐退。3）肩停机观察在新的 IDLE 心跳加上新鲜读数到来后就结束，手臂下沉只体现为 `stop_observed=0`。肩取零静止判据改为关节约 `0.5°`／`3°/s`／`100 ms`。`followstatus` 末尾新增 `pz=暂停中/暂停次数`、`pk=肩/肘峰值速度(°/s)`。命令帽、目标电流、反馈超时、范围、通信与驱动故障判据都未变。该 −1 符号版刷板后一次窗口因肘被推向伸直止挡而 `SAFETY_RANGE`。
- 2026-10-16 `S5_R5_ELBOW_SUPPORT_SIGN=1` 版（BIN `E9B52B74…85D2D`）：C620 实际命令为 −2 A（屈肘方向）。肩不抬时窗口正常停止；抬肩时前臂相对上臂伸开，越过 −1° 下限。
- 2026-10-16 R5 下限 −10° 版（BIN `FD8717B1…7565E6D`，`SIGN=1`、`MA=2000`）：一次带电窗口全程 RUNNING，肩抬到约 12.8° 时肩目标 16 mNm（转子侧），`followstop` 正常停止。
- 2026-10-16 30 s／4 A 版（BIN `436DA437…5689D506`）：肩增益 0.6、上限 80 mNm、会话 30 s、肘 4 A。一次窗口中肘在软限暂停下往复振荡，约 6.1 s 后 `TRANSPORT` 故障，推断为肩转矩帧在 `NART=1` 下仲裁丢失。
- 2026-10-16 仲裁补发版（BIN `C5E890CA…E3A92A3C`，`MA=2000`）：`board_can_tx.c` 对输出帧在确认 `ALST` 且无 `TERR` 时同帧补发一次，补发次数记在 RAM `output_alst_resends`。30 s 窗口全程 RUNNING 并正常停止，肩补发 5 次。
- 2026-10-16 肩 0.8／120 mNm 版（BIN `2B2B1706…29620384`）：首窗全程 RUNNING 并正常停止；第二窗约 16 s 时肩属性读取 `S3_READ_TX` 故障。`motor_slew_per_s` 改为 100 mNm/s 对 R5 运行期无效（`direct_target` 不经过它）。
- **当前板上（2026-10-16）**：[候选](build/s5-r5-sh10-205-el20-20261016/Exoskeleton_F446.bin)，BIN SHA256 `E8D87B98377699F0D2CBAE5CFD3E0D63035E6813FC3919621855FCE2887F4D26`，已独立读回，`SIGN=1`、`MA=2000`。R5 肩增益 1.0（完整 1.64 N·m 粗估模型），转子侧上限 205 mNm（等于模型峰值，约合关节 1.64 N·m），撤力斜率 80→120 mNm/s；肘下限 −10°→−20°。肩读取的 `S3_READ_TX` 疑似顺序缺陷**未修**（用户本轮未选）。`output_alst_resends` 仍在 `0x20001bf8`。尚未带电运行。
- App/application包含调度和命令；Core中的board_console/board_runtime提供平台接口，应用层不持有HAL句柄。App/devices含C620接收解析和纯命令编码，App/control含坐标转换/安全检查，App/application含默认关闭的输出门控；不改变CAN物理层配置。
- Drivers不手改；Core中的接入代码放USER CODE区。自有源文件加入顶层CMakeLists.txt；不手改生成的cmake/stm32cubemx清单。
- 顶层AGENTS.md及硬件接口登记仍为工作依据。根目录到这里的路径为 firmware/Exoskeleton_F446。
- 电机与 CAN 模块的详细连接见[图纸文字说明](<../../Main_ project/Hardware_information_list/motor_can_diagrams_text.md>)；板卡引脚和接口的结构化资料见[C04 JSON](<../../Main_ project/Hardware_information_list/nucleo_f446re_mb1136_c04_ai.json>)，仅为项目接口摘录。
- AI 读取手册时优先对应型号/版本且正文可读的 Word；入口见文字说明第0.1节。针号、接头方向和线路核对原 PDF/原理图，当前接线及验证状态以接口登记为准。

## 已有辅助配置

原 `.agents`、`.auto-embedded`、`.codex` 原样保留；迁移不代表硬件资源已锁定或hooks已在新位置启用。根项目打开时不会自动保证加载嵌套工程hooks，本次不更改用户全局信任或工具配置。

## 验证边界

2026-09-25：Mac端Debug构建及13项主机模拟测试通过，CubeMX重新生成后自有代码保留。Mac端SWD连接报USB错误，虚拟磁盘HEX写入尝试不等于读回校验通过。Windows端现已完成S1烧录校验、IRQ优先级读回、三次复位后的串口查询与10分钟通信测试。用户报告物理USB重插后，VCP命令恢复，但SWD报`DEV_USB_COMM_ERR`；Windows系统级重新枚举后SWD恢复，根因未定。用户目视LD2约每秒闪一次，与500 ms翻转、完整亮灭周期约1秒的设定定性相符，未用仪器测量时序。用户已决定S1基本完成，SWD偶发异常作为已知问题按需处理，不阻塞S2；详见唯一开发日志。

## 通信行为（S1 基础；板上为 R5 双关节 FOLLOW 隔离镜像）

- USB VCP 使用 USART2 PA2/PA3，115200、8N1、无流控，命令区分大小写，以 LF 或 CRLF 结束。当前源码保留 S1/S2 查询与 S3 `prepare/arm/cancel/ackfault`、单次 pulse；S4/S5 的选路在冷启动由编译配置和 site 就绪条件决定，FOLLOW 命令为 `followobserve/followprepare/followstart/followstop/followstatus`，公共 `cancel` 路由到选中模式的停止。板上 R5 隔离镜像选择双关节 S5 FOLLOW；早前 R4 肘单轴镜像的单轴 FOLLOW 选路、R2 黑匣子镜像的 `prepare/arm/elbowpulse/cancel` 与 `ec*` 查询均不代表当前板上选路。R4 故障具体触发条件与旧 R2 间歇故障根因均待定位。生产 S4/S5 输出配置仍禁用；完整阶段行为和验收结果见开发日志。
- 当前通用 `status` 仍报告 `stage=S2_CAN_RX`；实际输出许可与故障应查选中模式的 `outputstatus` 或 `followstatus`，不能据通用阶段字样判断 FOLLOW 已启用。S3/S4/S5 均经唯一 OutputGateway 门控，CLI 不能更改现场确认或打开编译期输出开关。正常 LD2 每500 ms 翻转；启动核验失败或通信故障每100 ms 翻转，复位恢复。
- DMA1 Stream5/Channel4循环RX、Stream6/Channel4普通TX；与USART2同抢占优先级5。BSP按键初始化后由`BoardRuntime_FinalizeInterrupts`将EXTI最终设为10并读回核验；SysTick为0，GROUP4。
- 参数集中在`Core/Inc/s1_config.h`。RX DMA为512字节，LwRB RX/TX存储1024/2048字节（各保留1字节区分满空），TX暂存256字节。中断服务延迟须小于半个DMA数组接收时间（115200下约22 ms）；位置差不能识别完全未服务的一整圈。
- 通信每1 ms处理最多128字节及一条完整命令；行最多96字节，非法/超长行整行丢弃。UART/DMA错误、RX溢出或TX超过100 ms锁存故障，停止命令处理。

当前停止源码另见[受监督台架策略](docs/s3_output_preparation.md)：保留默认禁用，接收静止观察与软件停止投递分开报告；停止投递失败不允许仅凭静止反馈清故障。13:46带电窗口两路持续收帧且错误为0，肩侧可信连续反馈及一次小幅手动变化仍待验收；用户已取消本轮S2实物掉线/恢复试验，未执行、不写通过。前轮4组关键停止测试及WindowsDebug编译通过；16:09新镜像已刷入并确认默认禁用，实际停止仍未验收。

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

## S3每轴源码准备与既有USB验证（实际输出关闭）

当前源码默认 `enabled_axes=0`，SAFE_OFF、configured=0、transport_bound=0、output_allowed=0。位0肩、位1肘：1仅肩、2仅肘、3双轴；集合只在冷启动初始化时复制，CLI不能修改。逐轴反馈/协议、映射与限值取代整机S2布尔门槛；只检查启用轴，停用总线不TX或调用HAL。公共保护/停止、UART和服务时序仍参与门控，任一启用关键链异常撤销全部启用轴许可。人工prepare/arm/cancel/ackfault和带令牌、明确单位/寿命的单轴pulse沿唯一出口执行；当前确认项false/0，默认Prepare/Arm被拒绝。详见[实现说明](docs/s3_output_preparation.md)。

`s3_feedback`从CanMonitor取得逐帧GDS档案观测与C620真实反馈，并分别保留时间戳和driver[2]。新只读`shoulderfeedback`固定标记`verified=0`，显示mrad/mrad_s、位置/心跳两个时间戳及心跳状态、错误；WireValid不授予许可，也不证明肩侧HAL Form根因已修复。受监督裸C620台架已增加独立命名的反馈静止观察，不能代表断电或硬件急停；外部停止输入及GDS校准/零目标/模式读回仍未提供。

GDS启用肩侧仍需IDLE/校准及新的零目标、模式、闭环读回。C620官方反馈没有目标电流echo、轴状态或校准状态；启用肘侧准备为零电流提交后取得更新的真实反馈，并要求现场 `operational_confirmed[1]` 和Prepare/Arm前真实 `independently_stopped`，不填零目标echo或GDS状态。发送接受和新反馈不能证明零电流已经执行。

裸C620可显式使用 `S3_BENCH_ROTOR` 的真实单圈编码器扇区：映射1:1、编码器零位0、方向已确认，范围按方向反算后严格在 `(0, 2π)`，且 `bench_sector_confirmed`、逐轴确认及 `trusted_feedback_axes` 满足，C620 ID须与实际接收配置一致。它不称连续转子量或关节角，不设置 `rotor_reference_confirmed` 或补造圈数；超时/越界锁故障，新帧不恢复旧Arm。装配关节模式仍缺可信连续参考。当前生产上述确认均未落实。

2026-10-02本轮 `host-04` 一次执行受影响35项全部PASS：32项既有场景加 `s3_axes`、`s3_observations`、`s3_shoulder_console` 三组。补入裸C620坐标ID匹配检查及反例后仍35/35 PASS、无编译警告；host-03和前版镜像证据保留，当前最终结果以host-04为准。11项未受影响UART场景复用前轮最终一次43/43整体验证的结果，不称一次46项全绿。首次编译发现旧测试整机字段尚未适配，定位后一次修正；随后按新合同完成验证。Windows ASan/UBSan未运行；本轮最终同源HSI/HSE WindowsDebug编译/链接0警告，FLASH64488B/RAM9312B；复用缓存再配置各有1条 `CMAKE_TOOLCHAIN_FILE` 未使用提示。source-manifest.json登记434个项目输入；加上本机CMakeUserPresets.json，冻结比较共435项，A/B仅IOC/main时钟段不同，两份生成检查通过，静态ARCH81/HW1历史告警未变、新增/移除0、SPEC通过。本轮证据位于 `D:/codex_tasks/2026-10-01/task/s3-axis/`，交付hash以本轮最终审计为准，不以旧镜像记录代表当前源码。

2026-10-02前轮43场景记录保留：原40项加prepare/gds/cli三组；首次整体42项通过而CLI夹具失败，定位HAL启动状态后一次修正测试初始化顺序，先重跑CLI通过；随后于2026-10-02 06:44 UTC最终完整执行一次43/43 PASS，退出0、无warning，证据为 `D:/codex_tasks/2026-10-01/task/s3-next/final-full-host/run.json`。前轮生成检查通过，同源HSI/HSE WindowsDebug配置/编译0警告，FLASH59724B/RAM9264B；ARCH81/HW1历史告警未变、SPEC通过，Windows ASan/UBSan未运行。

前轮用户授权USB-only并报告24V断开后，原Flash512KB已备份，HSI镜像 `ac5ea60571f18b754114a82ac303912bc5cc09babce3dd64593ba698d0ef6fc4` 烧录校验及COM10七条默认拒绝检查通过；HSE未烧录，没有实物pulse/校准/模式命令。证据在 `D:/codex_tasks/2026-10-01/task/s3-next/`，时钟包见[比较说明](docs/can_clock_comparison.md)。此段为早期镜像记录，之后13:27和16:09 HSI均已更新；停止策略已随16:09上板，实物停止效果未验收。按用户最新要求，完成S2实物验收后再提交/推送GitHub；唯一进度日志仍为[开发排期](<../../Main_ project/docs/development_schedule.md>)。


后续现场返回的USB-only更新尝试（2026-10-02 10:55–10:58 UTC）：24V关闭已由用户明确确认，最新A-HSI hash与默认禁用配置已核对；固定ST-LINK/COM10对应正确，但SWD Hot Plug身份读取失败、Normal诊断报DEV_USB_COMM_ERR。因此没有执行新Flash备份或烧录。三条ping/status/outputstatus查询通过，应用运行且输出禁用；未发送prepare/arm或其他控制命令。最新源码仍未上板，结果见s3-axis/usb-only-20261002/session-result.json；代码与前述软件验证未改。

## 肩侧限定属性读取（16:09已烧录校验，属性应答待验收）

实机HW3.11.1/FW0.6.5的USB schema已确认只读float端点372/378。新增`shoulderreadpos/shoulderreadvel`各端点每次启动仅一次，`shoulderreadstatus`保存匹配025应答；要求默认门控关闭及新鲜IDLE心跳，无自动重试或任意参数。原生encoder估计与周期029分别标记，不改变S3许可。协议、来源、超时和最小后续窗口见[限定SDO诊断](docs/s2_encoder_sdo_diagnostic.md)。16:21已完成限定CAN属性/USB紧邻对照；当前S2只剩可信连续肩反馈及一次小幅手动变化。用户已取消实物掉线/恢复试验，状态为取消、未执行；不是通过。S3运动/停止/断连处理要求仍保留；GitHub在S2验收后上传。

## S2 有界连续编码器观察（待现场验收）

`encstart/encstop/encfeedback`无参数，固定CAN1/node1/端点372与378，按位置后速度顺序发布完整同周期样本。每次启动最多一个30秒会话、20Hz配对、1200次请求；应答50ms、配对新鲜度100ms，IDLE心跳另用250ms。与一次性诊断互斥，不自动启动/重启/重试；SDO无线上事务token，要求隔离唯一CAN主机及有序应答。默认enabled_axes=0，连续观察不授予S3许可。

来源依据、限制及明早最小步骤见[连续观察说明](docs/s2_encoder_stream.md)和[Windows采集工具](tests/encoder_capture/README.txt)。采集进程给出`READY_FOR_MOVEMENT`后，主线程才通知用户做一次小幅手动变化；`COMPLETE`和非零小波动都不能自动判为物理变化通过。今晚17:00更新、校验/复位及独立读回通过，17:02仅被动查询、未启动流；明早仍需现场窗口，本轮不安排实物掉线/恢复。
