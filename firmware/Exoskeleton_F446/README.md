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
CMake Tools已配置本机cmake路径；需要使用扩展配置工程时选择LocalDebug或LocalRelease预设（其环境包含编译工具路径）。换电脑需调整.vscode/settings.json和本地预设。日常直接使用上述构建任务。

## 当前基线及编辑边界

- NUCLEO-F446RE / STM32F446RET6；ioc记录 CubeMX 6.18.1、STM32CubeF4 V1.28.3。
- 迁移时工具路径对应 CMake 4.3.1+st.1、GCC 14.3.1+st.2、Ninja 1.13.2+st.1。
- Core及Drivers保持原样，仍为LED闪烁示例；双CAN、HX711与控制算法尚未接入。
- App/devices、App/control、App/application按需要逐步实现；当前仅有目录说明。
- Drivers不手改；Core中的接入代码放USER CODE区。自有源文件加入顶层CMakeLists.txt；不手改生成的cmake/stm32cubemx清单。
- 顶层AGENTS.md及硬件接口登记仍为工作依据。根目录到这里的路径为 firmware/Exoskeleton_F446。

## 已有辅助配置

原 `.agents`、`.auto-embedded`、`.codex` 原样保留；迁移不代表硬件资源已锁定或hooks已在新位置启用。根项目打开时不会自动保证加载嵌套工程hooks，本次不更改用户全局信任或工具配置。

## 验证边界

迁移后须在新目录重新编译，不复用旧build缓存。本轮不烧录、不运行电机；ST-LINK下载及断点调试配置留待连接开发板后核实，不提供未经验证的启动配置。
