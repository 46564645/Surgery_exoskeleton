# 编辑清单

| 文件 | 改动 | 验证标准 | 结果 | commit |
|---|---|---|---|---|
| `App/application/elbow_session_angle.*`、`s3_feedback.*`、`s3_bench.*`、`c620_standstill.*` | R1 肘 RAM 会话零位、跨零与 S3 准备链 | 相关主机测试、WindowsDebug 构建 | 执行者记录：23 个相关 S3 用例 PASS，构建 0 warnings；未刷写、未实物验证 | 未提交 |
| `App/control/s5_elbow_support.*`、`App/application/s5_follow.*`、`s5_site.*`、`output_gateway.*`、`console_commands.c`、`CMakeLists.txt` 及相关主机测试 | R3 肘单轴与 R5 双轴 FOLLOW 软件链；生产 site 保持 `enabled_axes=0/output_enabled=false` | 完整主机套件和默认 WindowsDebug 构建 | 唯一开发日志记录 108/108 PASS、构建／链接退出 0 且无新增编译警告；本次文档收尾未重复运行 | 未提交 |
| `App/application/s3_site.c`、`App/application/app_main.c`、R2 隔离构建产物及 `docs/bench/s5-elbow-once-20261011/` | R2 专用肘 S3 一次动作候选，普通构建选项 OFF | 隔离构建与脚本离线假串口 | 隔离 BIN SHA256 `3adf9f9945d51c061b3669caa92fff9a9c16e95ac338208931660ce386a366a9`；构建／链接退出 0、无新增编译警告；脚本三路径离线检查通过。候选未刷写、未接 24V、未实物运动 | 未提交；构建产物位于被忽略的 `build/` |
| `README.md`、本 `plan.md`、`edits.md` | 同步 R1/R2/R3/R5 的软件与现场边界 | 内容对照 site、候选记录、唯一开发日志及 Git 状态；`git diff --check` | 2026-10-11 文档收尾；固件代码和开发排期日志本轮未修改，现场结果未新增 | 未提交 |
| `App/application/s5_site.c`、`CMakeLists.txt`、`tests/host/test_s5_follow.c` 及主机测试入口 | R4 肘单轴、R5 双轴隔离 site 要求显式肘支撑符号与 mA；R4 5 s／250 ms，R5 10 s／1 s 并沿用 S4 肩低姿态局部值；生产输出仍 OFF。修复 S5 site 用全波峰值误拒单侧肩角的问题 | 先以失败用例复现误拒，再核对局部角域的通过／超帽拒绝、相关主机用例及 WindowsDebug 构建 | 执行者记录相关主机用例与 WindowsDebug 构建通过；R4/R5 只有示例参数离线 ELF，无刷写或方向／支撑／双轴效果实测 | 未提交 |
| `docs/bench/s5-elbow-once-20261011/follow_once.py` 及同目录说明 | R4 一次性串口流程与手动小幅变姿态观察窗口；失败不重复动力命令 | 假串口成功、准备拒绝、启动拒绝、运行故障四路径 | 四路径离线通过；每路径最多一次启动并尝试正常停止／清理；窗口内样本仅记录，不作主观演示成功门槛。未连接设备 | 未提交 |
| `README.md`、本 `plan.md`、`edits.md`、唯一 `Main_ project/docs/development_schedule.md` 末尾 S5 日志 | 同步 R4/R5 隔离软件进度及 R2 冻结镜像边界 | 对照源码、构建产物和日志；检查文档改动空白与范围 | R2 固定 BIN 的 SHA256 已复核匹配，但后续 site／CMake 修改后未重编 R2；当前轮次不将离线 ELF 写为实物验收 | 未提交 |
| `tests/host/test_s5_follow.c`、`tests/run_host_tests.py`、`docs/bench/s5-elbow-once-20261011/dual_follow_once.py`、R5 独立构建与上板证据、本任务文档及唯一开发日志 | 增加真实 R5 角域双轴链和肘反馈过期共同撤权检查；建立一次性串口观察；记录首次双轴准备失败 | 相关主机检查、WindowsDebug 构建、刷后独立读回、现场状态与原始记录逐项核对 | 主机相关用例和构建通过；R5 BIN SHA256 `ECC96C845F76B13FBE259C6412781A56841494D3D565B4B4B960E674040D044E` 与读回一致。两次现场尝试均未启动 FOLLOW：一次入口拒绝，一次两轴取零后在网关 Prepare 前故障。后续不启动助力的准备抓取正常撤销、未复现故障。无双轴助力或正常停止实物通过结论 | 未提交；证据在 H 盘 `build/` |

当前为 `main` 工作树；S5 task 目录及新增源码仍有未跟踪文件，相关既有源码与 README 有未提交改动。本轮不创建 commit、不推送；后续精确暂存前先核对共同工作树中的其他改动。
