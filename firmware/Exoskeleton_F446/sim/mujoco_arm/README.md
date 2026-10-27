# MuJoCo 臂部仿真（SIM）

用 MuJoCo 搭“桌面固定外骨骼 + 人臂”模型，接入固件真实控制代码，印证并优化省力效果。工作约定见 [AGENTS.md](AGENTS.md)，计划见 [schedule 1.2–1.3节](<../../../../Main_ project/docs/development_schedule.md>)。

## 环境（M0，2026-10-26 建立）

- Python 3.13.9（`H:\app\python.exe`），虚拟环境 `firmware/Exoskeleton_F446/build/sim-venv/`（已忽略，不提交）。
- 依赖全部固定在 [requirements.txt](requirements.txt)，主要为 `mujoco==3.16.0`、`myo_sim==0.2.3`（人体模型，2026-10-27 加入）、`numpy==2.4.6`、`matplotlib==3.10.9`。

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

## M1b 模型：MyoArm + Fusion OrthoEase + 桌子（2026-10-27 返工）

旧 M1（自绘胶囊人体、外骨骼在手臂内侧、关节角耦合绑带）已按用户指出的实物关系废弃，改为：

| 来源 | 内容 |
|---|---|
| [model/orthoease/](model/orthoease/) | 用户 Fusion 导出包（只读副本：入口 `orthoease.xml`、18 个 STL、`metadata/`）。外骨骼外形、运动链和两个绑定点以它为准 |
| MyoSim 0.2.3 `myoarm_r` | 站立人体（固定躯干 + 右臂骨骼）；去掉肌肉，锁住肩带和远端关节，只留 `shoulder_elv_r`（前屈）与 `elbow_flexion_r`（屈肘），抬臂平面 90° 直接写进刚体姿态 |
| [model/params.json](model/params.json) | 唯一物理参数源（每项带 status/source）：桌高、支架、密度、阻尼、绑带软度等 |
| [model/build_model.py](model/build_model.py) | 用 MjSpec 组装并写出 `build/sim/arm_exo_table.xml`；对 Fusion 模型的修改全部在这里完成 |
| [scripts/check_static.py](scripts/check_static.py) | 场景关系、质量、重力矩、穿戴不通电更累、绑带随动、桌面接触 |
| [scripts/plot_scene.py](scripts/plot_scene.py) | front/right/back/top/oblique 五个视角 PNG 到 `build/sim/scene/`，可 `--hide human/exo`、`--viewer` |

组装规则：
- 外骨骼绕 z 转 +90° 后平移：肩轴穿过 MyoArm 肩关节中心；侧向位置使上臂绑带内侧离肱骨轴 56 mm（截面图内弧半径）。
- 沿 04 调节轨把肘部以下加长 34.8 mm，使外骨骼肘轴对准人肘（0.257→0.2905 m，调节轨实际行程未核实）。
- 质量按 STL 体积×ABS 1.1 g/cm³，M3508 0.120 kg，GIM6010-8 0.388 kg；关节阻尼改为 0.02 N·m·s/rad（假设）；杆顶转台 J01 被动可转，阻尼 0.1（假设）。
- 两条绑带为 site-to-site 软 `weld`（solref 0.02、torquescale 0.03，取自 Fusion 包 README）。
- 人站立；桌高 0.90 m（用户 2026-10-27 最终决定）；竖杆直径 14 mm，**轴线穿过 01B 夹持座的孔**（`build_model.clamp_bore()`从 STL 截面算出孔径中心，Fusion 坐标 (0.10784, 0.01981) m、孔约 15.0 mm），从方块向上接到夹持座，方块伸出桌沿；手臂下垂时在桌沿外侧。人体只与桌面碰撞。

```powershell
build\sim-venv\Scripts\python.exe sim\mujoco_arm\model\build_model.py       # 生成模型并打印几何
build\sim-venv\Scripts\python.exe sim\mujoco_arm\scripts\check_static.py    # 全部检查
build\sim-venv\Scripts\python.exe sim\mujoco_arm\scripts\plot_scene.py --pose 60,60
build\sim-venv\Scripts\python.exe sim\mujoco_arm\scripts\plot_scene.py --pose 60,60 --hide human
build\sim-venv\Scripts\python.exe sim\mujoco_arm\scripts\plot_scene.py --viewer 10
build\sim-venv\Scripts\python.exe sim\mujoco_arm\scripts\lift_contact.py      # 抬臂到碰桌，出拼图
```

`plot_scene.py`除五个视角外还有`pole`视角：近距离看竖杆穿过 01B 夹持座孔。`lift_contact.py`用人肩 PD 抬起手臂，打印首次接触的时间/角度/接触部件，并输出整景+手部特写两行拼图。

**当前数值（2026-10-27，模型假设下）**：外骨骼上臂组 0.656 kg、前臂组 0.177 kg、转台 0.499 kg；外骨骼水平自重力矩 1.728 N·m（固件模型 1.64）；人臂水平 10.5 N·m；穿戴不通电 60° 保持时人肩力矩 9.37→10.65 N·m；前臂绑带内侧离尺骨轴 41.5 mm（截面 37 mm）。

**已知边界**

- 桌高 0.90 m 时，直臂前屈约 **6.6°** 拇指尖就碰到桌面靠人一侧的前下棱（接触点 z=0.85 m、y=−0.068 m）；肘 45° 时下垂姿态即接触，肘 90° 时前屈到 110° 都不碰。用`lift_contact.py`可复现并出图。**用户 2026-10-27 决定不再为这个干涉调整布局**：保留碰撞、接受直臂会碰桌；M4 动作按能做的范围设计，若确实需要自由空间扫动，把`params.json`的`table.collision_enabled`临时设为 false。
- 绑带 solref/torquescale、关节阻尼、调节轨行程、侧向距离都是假设；绑带力只看量级，M5 之前不当作真实力。
- 外骨骼两根轴是"方正"的（沿 x），人体解剖轴与之差 3–4°，由软绑带吸收。
- 控制参数不在模型里：肩 1.64 N·m、肘 mA×kT、限幅与周期都留在固件，M2b 通过 DLL 读同一份。

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
