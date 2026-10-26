# MuJoCo 臂部仿真（SIM）

用 MuJoCo 搭“桌面固定外骨骼 + 人臂”模型，接入固件真实控制代码，印证并优化省力效果。工作约定见 [AGENTS.md](AGENTS.md)，计划见 [schedule 1.2–1.3节](<../../../../Main_ project/docs/development_schedule.md>)。

## 环境（M0，2026-10-26 建立）

- Python 3.13.9（`H:\app\python.exe`），虚拟环境 `firmware/Exoskeleton_F446/build/sim-venv/`（已忽略，不提交）。
- 依赖全部固定在 [requirements.txt](requirements.txt)，主要为 `mujoco==3.16.0`、`numpy==2.4.6`、`matplotlib==3.10.9`。

在 `firmware/Exoskeleton_F446` 目录的 PowerShell 中执行（临时目录与 pip 缓存放 H 盘）：

```powershell
$env:TEMP="$PWD\build\sim-tmp"; $env:TMP=$env:TEMP
$env:PIP_CACHE_DIR="$PWD\build\sim-tmp\pip-cache"; $env:PIP_USER="0"
H:\app\python.exe -m venv build\sim-venv
build\sim-venv\Scripts\python.exe -m pip install --only-binary=:all: -r sim\mujoco_arm\requirements.txt
```

## 冒烟检查

```powershell
build\sim-venv\Scripts\python.exe sim\mujoco_arm\scripts\smoke_env.py              # 无界面
build\sim-venv\Scripts\python.exe sim\mujoco_arm\scripts\smoke_env.py --viewer 5   # 打开查看器 5 秒
```

输出 MuJoCo 版本、单摆 30° 静止所需力矩（`mj_inverse` 与公式对照）和步进 1 s 后的角度。

**查看器状态（2026-10-26）**：当天早些时候 Intel Iris Xe 与 RTX 3060 都处于设备管理器 Code 43，桌面由 ToDesk 虚拟显示输出、没有 OpenGL，查看器与离屏渲染都失败。同日晚显卡恢复（两块均正常，Iris Xe 驱动 2560×1600），实测 `--viewer` 能开窗、`mujoco.Renderer` 能离屏出图，两条路径都可用。此前"Windows 版只支持 WGL"的结论不变，只是现在 WGL 有可用的驱动。

## 场景可视化（2026-10-26 补）

```powershell
build\sim-venv\Scripts\python.exe sim\mujoco_arm\scripts\plot_scene.py --pose 60,30
build\sim-venv\Scripts\python.exe sim\mujoco_arm\scripts\plot_scene.py --pose 60,30 --hide human
build\sim-venv\Scripts\python.exe sim\mujoco_arm\scripts\plot_scene.py --viewer 10
```

默认输出三个视角的 PNG 到 `build/sim/scene/`，并打印关键body位置。**外骨骼与人臂同轴且更细，正常视角下人臂会完全挡住外骨骼**，看外骨骼用 `--hide human`（仅渲染时把人臂设为透明，不改模型）。`--viewer` 打开真正的交互窗口。

## M1 模型（2026-10-26 建立）

| 文件 | 作用 |
|---|---|
| [model/params.json](model/params.json) | 唯一物理参数源，每项带 `status`（measured/estimated/assumed/derived）与 `source` |
| [model/build_model.py](model/build_model.py) | 由参数生成 MJCF，输出 `build/sim/arm_exo_table.xml`；检查绑带两点在 q=0 重合 |
| [scripts/check_static.py](scripts/check_static.py) | 无界面静态检查：场景尺寸、质量、`mj_inverse` 与二连杆公式、固件 1.64 N·m 对照、被动稳定性、穿戴后更累、绑带随动 |

```powershell
build\sim-venv\Scripts\python.exe sim\mujoco_arm\model\build_model.py      # 只生成模型
build\sim-venv\Scripts\python.exe sim\mujoco_arm\scripts\check_static.py   # 生成 + 全部检查
```

场景：地面、0.75 m 桌面（含立柱与水平支架）、外骨骼两连杆（肩轴在桌面上方 0.30 m）、人体固定躯干 + 上臂/前臂/手、两条绑带。**外骨骼整体侧移 0.10 m 贴着手臂外侧**（用户 2026-10-26 SolidWorks 视图：连杆在臂外侧、绑带环箍住手臂），关节轴仍沿 y，矢状面力学不变。绑带用**关节角 1:1 耦合**（`equality joint`），不是两点重合：外骨骼肘轴比人体肘轴高 5.5 cm，弯肘时两个绑点在几何上无法同时重合（90° 时差约 4 cm），点约束或刚性焊都会产生虚假的巨大阻力（实测肘力矩饱和 40 N·m 只弯到 42°）。控制参数不在模型里：肩 1.64 N·m、肘 mA×kT、限幅与周期都留在固件，M2b 通过 DLL 读同一份。

**已知边界**

- 桌面碰撞默认关闭（`table.collision_enabled=false`）：肩轴只在桌面上方 0.30 m，而整臂 0.77 m，直臂前屈超过约 25° 就会穿过桌面。S5 那种自由空间扫动与"桌子在正前方"不能同时成立，T5 桌面动作需要先定坐姿再打开。
- 绑带是角度耦合，**不直接给出绑带力**；`check_static.py` 打印的是约束传递的力矩（N·m）。HX711 力触发要用到真实绑带力时，需要更细的绑带模型（或从台架标定反推），这属于 M5 的范畴。
- 侧向偏置 0.10 m 是占位假设（大于臂半径 0.045 + 连杆半径 0.035，保证不穿模），实物值待量。
- 人体尺寸/质量、桌子几何、外骨骼质心均为估计或假设，见 `params.json` 的 `status`。

## M2 接入固件控制代码（2026-10-26）

固件侧先做了一步纯搬移（M2a）：`s5_follow.c` 中 `compute_target()` 的合成部分移到 [App/control/s5_assist_compose.c](../../App/control/s5_assist_compose.c)，顺序与数值不变；`s5_follow.c` 只负责取站点数值和力样本。主机用例 124 个全过，`WindowsDebug` 与 R5 双轴配置都能编译（未刷写）。

仿真侧（M2b）用同一份 C 代码编成共享库：

| 文件 | 作用 |
|---|---|
| [bridge/sim_bridge.h](bridge/sim_bridge.h) / [.c](bridge/sim_bridge.c) | 导出 `SimBridge_Reset` / `Step` / `GetSite` / `Sizes`；字段全为 32 位，`Sizes` 供 ctypes 校验 |
| [bridge/site_stub.c](bridge/site_stub.c) | 仅为链接满足 `S3ShoulderService_ReadProfileReady`（桥接不调用就绪检查） |
| [scripts/build_bridge.py](scripts/build_bridge.py) | MSVC 编 `build/sim/sim_bridge.dll`；macOS 走 clang 编 `.dylib`；`--gain/--ma/--sign/--amplify-mnm/--force-assist` 对应固件 CMake 选项 |
| [sim/controller.py](sim/controller.py) | ctypes 封装；8:1、kt、限幅、周期等数值由 `Controller.site` 从库里读回，不在 Python 重写 |
| [scripts/test_bridge.py](scripts/test_bridge.py) | 与固件公式对照；`--sweep` 额外用 gain 0.6 重建并检查扫描通路 |

```powershell
build\sim-venv\Scripts\python.exe sim\mujoco_arm\scripts\build_bridge.py
build\sim-venv\Scripts\python.exe sim\mujoco_arm\scripts\test_bridge.py --sweep
```

实测：30/60/90° 姿态下肩转子 0.1025/0.1775/0.2050 N·m、肘 1.0/1.73/2.0 A，与手算一致；assist 关为 0；肩反馈失效时返回 0；力触发默认关闭；gain 0.6 时肩命令正好 0.6 倍。

## M3 传感器 / 执行器 / 时序（2026-10-26）

| 文件 | 作用 |
|---|---|
| [sim/timing.json](sim/timing.json) | **只放固件没通过桥接暴露的模型假设**：肘 8192 tick/rev、肘滞后 49 ms、肩滞后未知(0)、HX711 10 SPS/100 ms/σ0.001 N |
| [sim/io_model.py](sim/io_model.py) | `SensorModel`（肩 50 ms 保持 + 肘量化）、`CommandHold`（50 ms 零阶保持）、`ActuatorModel`（×8 / ×0.02 + 一阶滞后）、`ForceModel`（10 SPS + 延迟 + 噪声） |
| [scripts/check_timing.py](scripts/check_timing.py) | 六组数值检查，含 DLL 闭环 2 s |

```powershell
build\sim-venv\Scripts\python.exe sim\mujoco_arm\scripts\check_timing.py
```

实测：肩角每秒 20 次更新且两次之间保持不变；命令每秒 20 次更新；肘角按 0.0439° 台阶；肘力矩 49 ms 到 63.2%；力 5 s 采 50 个样本、阶跃延迟 100 ms、噪声 σ0.84 mN；闭环 2 s 内肩力矩按 20 Hz 阶梯跟随，稳态 1.420 N·m 与固件模型一致。

**时序参数不重复**：send interval、肩属性读取周期、反馈超时、会话时长、8:1、kt、限幅都从 DLL 的 `SimBridgeSite` 读回，`timing.json` 只存仿真假设。
