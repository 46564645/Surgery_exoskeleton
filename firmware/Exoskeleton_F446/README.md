# Exoskeleton_F446 固件入口

当前唯一开发目录：本文件所在目录。桌面 STM32Projects 中的原工程为迁移备份，不继续并行修改。

## 如何打开

- VS Code：打开总项目根目录的 `Exoskeleton.code-workspace`，代码在 Firmware — F446 下，资料在 Project 下。也可单独打开本目录。
- CubeMX：打开本目录的 `Exoskeleton_F446.ioc`。生成前确认 Project Location 指向本目录、工程名为 Exoskeleton_F446、Toolchain 为 CMake、编译器为 GCC，保留用户代码；不要再向桌面备份生成。
- 两个软件共用本目录，无需复制同步。CubeMX负责外设初始化，VS Code负责应用代码及构建。

## 构建

在本目录执行 `./build.sh Debug` 或 `./build.sh Release`。
VS Code选择任务 `Firmware: Build Debug`（默认构建任务）或 Release。
脚本先配置再编译；`build/` 为可重新生成的产物，不提交。

本机 `.local-tools.sh` 已配置 ST 工具路径；此文件不提交。换电脑应安装工具并将 cmake、ninja、arm-none-eabi-gcc 加入 PATH，或重建该本地文件。
CMake Tools通过PATH寻找cmake；需要指定安装位置时，在各电脑的VS Code用户设置配置`cmake.cmakePath`，不向共享设置写入绝对路径。本机LocalDebug/LocalRelease预设仅存在于被忽略的CMakeUserPresets.json中，换电脑需自行配置。

Windows接续开发时，先把cmake、ninja和arm-none-eabi-gcc加入当前终端PATH，然后在本目录运行：

```powershell
cmake --preset Debug
cmake --build --preset Debug
```

现有VS Code构建任务调用`/bin/sh`，不能直接在原生Windows使用；先用上述终端命令。不要复制Mac的build缓存或本机工具配置。建议先匹配工程记录的工具版本；Windows构建尚待实测，不用旧版CubeMX重新生成。构建所需HAL/CMSIS及LwRB/CLIP源码已在固件目录内，参考子模块不是本轮固件构建的必要依赖。主机模拟测试还需支持ASan/UBSan的宿主C编译器；ARM GCC不替代宿主编译器。

## 当前基线及编辑边界

- NUCLEO-F446RE / STM32F446RET6；ioc记录 CubeMX 6.18.1、STM32CubeF4 V1.28.3。
- 迁移时工具路径对应 CMake 4.3.1+st.1、GCC 14.3.1+st.2、Ninja 1.13.2+st.1。
- S1通信代码已实现：USART2循环DMA接收、DMA发送、静态命令与非阻塞调度；双CAN、HX711、StateSmith与电机控制尚未接入。
- App/application包含调度和命令；Core中的board_console/board_runtime提供平台接口，应用层不持有HAL句柄。App/devices、App/control仍预留。
- Drivers不手改；Core中的接入代码放USER CODE区。自有源文件加入顶层CMakeLists.txt；不手改生成的cmake/stm32cubemx清单。
- 顶层AGENTS.md及硬件接口登记仍为工作依据。根目录到这里的路径为 firmware/Exoskeleton_F446。

## 已有辅助配置

原 `.agents`、`.auto-embedded`、`.codex` 原样保留；迁移不代表硬件资源已锁定或hooks已在新位置启用。根项目打开时不会自动保证加载嵌套工程hooks，本次不更改用户全局信任或工具配置。

## 验证边界

2026-09-25：Mac端Debug构建及13项主机模拟测试通过，CubeMX重新生成后自有代码保留。Mac端SWD连接报USB错误，虚拟磁盘HEX写入尝试不等于读回校验通过。用户转述Windows端已恢复SWD连接并读出STM32F446xx（Device ID 0x421、512 KB），但尚未烧录S1或验证串口。S1实物验证未完成，详见项目唯一开发日志。

## S1通信与检查

- USB VCP使用USART2 PA2/PA3，115200、8N1、无流控；只支持无参数的`ping`、`status`、`help`、`?`，区分大小写，以LF或CRLF结束。
- `status`固定报告`stage=S1_COMM output_allowed=0`；无电机控制命令。正常LD2每500 ms翻转；启动核验失败或通信故障每100 ms翻转，复位恢复。
- DMA1 Stream5/Channel4循环RX、Stream6/Channel4普通TX；与USART2同抢占优先级5。BSP按键初始化后由`BoardRuntime_FinalizeInterrupts`将EXTI最终设为10并读回核验；SysTick为0，GROUP4。
- 参数集中在`Core/Inc/s1_config.h`。RX DMA为512字节，LwRB RX/TX存储1024/2048字节（各保留1字节区分满空），TX暂存256字节。中断服务延迟须小于半个DMA数组接收时间（115200下约22 ms）；位置差不能识别完全未服务的一整圈。
- 通信每1 ms处理最多128字节及一条完整命令；行最多96字节，非法/超长行整行丢弃。UART/DMA错误、RX溢出或TX超过100 ms锁存故障，停止命令处理。

在本目录运行：

```sh
./build.sh Debug
python3 tests/run_host_tests.py
python3 tests/check_generated.py
python3 .auto-embedded/scripts/check.py --json
```

主机测试使用模拟HAL，不代表板上DMA或IRQ已验证。机械检查当前有三项已人工说明的告警：CubeMX主函数初始化调用数、build内编译器探测文件行数，以及刻意保持相同的通信IRQ优先级；详见任务`edits.md`。未为消除告警修改官方代码。

开发板仅接USB且电机动力断开时，可使用下列只读命令测试；端口以当次枚举结果为准：

```sh
python3 tests/bench_serial.py --port /dev/cu.usbmodem1103 --seconds 600 --report build/bench/soak.json
```

该脚本不烧录、不复位；初始状态无回复即失败，不会把未执行的10分钟测试记为通过。完整验收还需SWD校验、实际IRQ读回、三次复位及USB重插后恢复查询。

Windows端需安装Python及pyserial；用`python tests/bench_serial.py --port COM10 --seconds 600 --report build/bench/soak.json`测试。COM10是用户本次报告的端口，运行前重新枚举确认。先完成S1固件烧录与校验，再执行串口验收。
