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

## M4 实验运行器（2026-10-27）

一条命令跑同一动作的三种条件，输出人的用力对照：

| 条件 | 含义 |
|---|---|
| C0 | 裸臂：不绑外骨骼（约束关掉），人只承担自己的手臂 |
| C1 | 穿戴但外骨骼不通电：人还要承担外骨骼重量 |
| C2 | 穿戴且由固件控制库驱动（每 50 ms 调用一次，参数取自`s5_site.c`） |

```powershell
build\sim-venv\Scripts\python.exe sim\mujoco_arm\run_experiment.py --task T2slow
build\sim-venv\Scripts\python.exe sim\mujoco_arm\run_experiment.py --task T1,T2slow,T2fast,T3 --duration 20
```

| 文件 | 作用 |
|---|---|
| [sim/tasks.py](sim/tasks.py) | 参考动作：T1 静态保持、T2slow/T2fast 抬放、T3 自由变姿态；段间用最小加加速度曲线。默认肘 90°（直臂会碰桌） |
| [sim/runner.py](sim/runner.py) | 仿真循环：人体 PD 跟踪参考（力矩即“人的用力”），C2 时按固件周期调用 DLL |
| [sim/metrics.py](sim/metrics.py) | 分肩/肘的 RMS、峰值、绝对值积分、跟踪误差、绑带力、桌面接触步数；抬/放分段；相对 C1 的省力率 |
| [sim/plots.py](sim/plots.py) | 四联图：肩力矩、肘力矩、绑带力、各条件 RMS 柱状图 |
| [run_experiment.py](run_experiment.py) | 命令行入口，写`build/sim-runs/<时间>/`的`samples_*.csv`、`summary_*.json`、`figure_*.png` |
| [scripts/report_figures.py](scripts/report_figures.py) | 从一次运行的目录汇总出报告图（时序、柱状、场景、可选 GIF） |

### 结果汇总（2026-10-27，最终参数 MA = 10 A，模型假设下）

数据：`build/sim-runs/final2-20261027/`；出图脚本`scripts/report_figures.py`。

| 动作 | 时长 | C0 裸臂 | C1 穿戴不通电 | C2 穿戴+助力 | 穿戴代价 | 助力收益 |
|---|---:|---:|---:|---:|---:|---:|
| T1 静态保持 5/25/45° | 24 s | 8.2 N·m | 9.2 | 8.4 | +12.1% | −8.8% |
| T2slow 抬放 4 s 周期 | 12 s | 8.2 | 9.2 | 8.4 | +12.5% | −9.0% |
| T2fast 抬放 2 s 周期 | 10 s | 8.1 | 9.1 | 8.3 | +12.5% | −9.1% |
| T3 自由变姿态 | 25 s | 9.4 | 10.5 | 9.5 | +11.9% | −9.5% |

（总用力 = 肩、肘力矩绝对值积分 / 时间；穿戴代价 = C1 比 C0 高多少，助力收益 = C2 比 C1 低多少。）

- 穿戴上不通电时，外骨骼让人的用力增加约 12%；现有固件（MA=10 A）能收回约 9%，剩下约 3% 是外骨骼自重没被补偿的部分。
- 分关节看：肩侧省力约 10%、肘侧约 7%。肩侧在所有幅值下都稳定，说明肩通道已把外骨骼肩侧自重抵消；剩余缺口在肘侧与前臂连杆。
- 任务从 5° 起而不是 0°：MyoArm 的肩关节下限就是 0°，从 0° 起步时手臂靠在关节止挡上，止挡而不是肌肉在承力，会让对比失真。

出图：

| 文件 | 内容 |
|---|---|
| `report/report_timeseries.png` | T2slow 时序：肩力矩、肘力矩、上臂绑带力，C0/C1/C2 三条曲线加参考轨迹 |
| `report/report_bars.png` | 四个动作的总用力柱状图 + “穿戴代价 / 助力收益” |
| `report/report_scenes.png` | 4 个代表姿态 × 右视/斜视 的场景图 |
| `report/lift_anim.gif` | 抬放循环动画 |

绑带软度按 0.05 s 时间常数（Fusion 包建议的 0.02 s 会让绑带与 3–4° 轴倾角对抗，90° 屈肘时产生 164/70 N；0.05 s 下为 25/11 N）。

### 肘通道幅值扫描（2026-10-27）

固件肘通道 = `MA × 0.02 N·m × max(0, sin(q_sh+q_el))`，`MA` 是编译选项`S5_R5_ELBOW_SUPPORT_MA`。用 T1 动作、只重建控制库（不改源码、不刷写）：

```powershell
build\sim-venv\Scripts\python.exe sim\mujoco_arm\scripts\sweep_elbow_ma.py
```

| MA | 肘幅值 | 总省力（相对 C1） | 肩 | 肘 | 电流含义 |
|---:|---:|---:|---:|---:|---|
| 2000（当前） | 0.04 N·m | +6.9% | +10.0% | +1.5% | 2 A |
| 6000 | 0.12 N·m | +7.9% | +10.0% | +4.3% | 6 A |
| 10000 | 0.20 N·m | +8.8% | +10.1% | +7.1% | 10 A（M3508 额定） |
| 14000 | 0.28 N·m | +9.7% | +10.1% | +9.8% | 14 A |
| 20000 | 0.40 N·m | +11.1% | +10.1% | +14.0% | 20 A（C620 命令上限） |

MA=20000 时穿戴加助力的总用力 7.07 N·m，与裸臂 7.09 N·m 基本相同，即外骨骼自重被完全抵消；但 20 A 是命令上限，连续运行会过热。MA=10000（额定 10 A）能拿到约 2/3 的收益。

## 阶段 A / B / G 结果（2026-10-27，模型假设下，MA = 10 A）

### A 工作空间扫描

```powershell
build\sim-venv\Scripts\python.exe sim\mujoco_arm\scripts\workspace_scan.py
```

肩 5–60° × 肘 30–110°（12×9）网格，每点用 PD 保持取稳态人体力矩（**不能用静态反动力学**：绑带闭合后，接地的外骨骼在反解里会"替"人承力，得到不合理的结果）。

| 指标 | 范围 | 均值 |
|---|---:|---:|
| 穿戴不通电的代价（C1 vs C0） | −1.8 … +19.0% | **+13.2%** |
| 固件助力的收益（C2 vs C1） | −0.9 … +15.8% | **+9.4%** |
| 肌肉激活的收益 | −62 … +34% | +5.0% |

规律：抬臂越高、肘越弯，收益越大（肩 60°/肘 70–110° 处 12–16%）；**低抬臂（肩 5–20°）+ 小屈肘（30–60°）区域助力反而增加肌肉激活**（图中红色块），因为外骨骼自重补偿在该姿态下"帮过头"。

### B 三个手术姿势（各 60 s）

```powershell
build\sim-venv\Scripts\python.exe sim\mujoco_arm\run_experiment.py --task B_P1,B_P4,B_P5
```

| 姿势 | C0 | C1 | C2 | 穿戴代价 | 助力收益 |
|---|---:|---:|---:|---:|---:|
| P1 器械平行桌面（肩 20°/肘 100°） | 7.62 | 8.54 | 7.83 N·m | +12.1% | **+8.2%** |
| P4 近身（30°/110°） | 7.48 | 8.60 | 7.69 N·m | +15.0% | **+10.6%** |
| P5 远端（60°/70°） | 10.93 | 12.55 | 11.02 N·m | +14.8% | **+12.2%** |

60 s 保持期间姿势漂移很小（肩力矩峰值与均方根差 ≤0.02 N·m），说明模型里没有出现缓慢下垂；疲劳代理需要更长的保持或加入肌肉疲劳模型才有意义。

### G 爆发动作

```powershell
build\sim-venv\Scripts\python.exe sim\mujoco_arm\scripts\burst_report.py
```

人用"PD + 重力前馈 + 45 ms 力矩上升滞后"模拟爆发发力；每周期为"冲程→保持 2 s→返回"，1 ms 采样。

| 指标 | G1 锤击（肘 100→45°，180 ms） | G2 快速抬举（肩 5→45°，250 ms） |
|---|---|---|
| 峰值力矩 C0 / C1 / C2 | 6.51 / 6.86 / 6.72 N·m | 44.4 / 44.9 / 44.5 N·m |
| RTD 0–50 ms | 6.20 / 6.56 / 6.39 N·m | 15.1 / 14.4 / 15.3 N·m |
| 达峰时间 | 9 ms | ~217 ms |
| 助力收益（C2 vs C1） | **+2.0%** | **+0.9%** |
| 穿戴代价（C1 vs C0） | +5.4% | +1.2% |

**结论（与预期一致）**：静态保持时助力能收回 8–12%，但在 180–250 ms 的爆发动作里只有 1–2%——因为固件输出每 50 ms 才更新、肩通道上升斜率仅 0.1 N·m/s，助力在爆发期间几乎"冻结"。同时外骨骼自重仍让峰值力矩增加 1–5%。

**边界**：人体爆发用的是简化 PD+前馈，不是肌肉模型；G2 仍有约 28% 超调，指标适合做三条件对比，不适合当作真实手术动作的绝对数值。

## 加大助力幅值的扫描（2026-10-27）

固件新增两个编译选项（只改上限，不改控制律）：

| 选项 | 范围 | 含义 |
|---|---|---|
| `S5_R5_SHOULDER_GAIN` | 0.001..**3.0**（原 1.0） | 肩通道增益；>1 表示连人臂的一部分也一起补 |
| `S5_R5_SHOULDER_CAP_MNM` | 1..**575**（原固定 205） | 肩转子力矩上限；575 mNm = 4.6 N·m 关节侧 = GDS68 旧 24 V 额定 / 8 |

注意：只加增益没用，因为原来的 205 mNm 上限会把增益>1 的部分截掉，必须同时放开上限。

| 设置 | 静态 P1/P4/P5 | G1 锤击峰值力矩 | G2 快速抬举峰值力矩 |
|---|---:|---:|---:|
| 205 mNm / gain 1.0（旧） | 8.2 / 10.6 / 12.2% | −3.5% | −0.3% |
| **300 mNm / gain 1.5（2026-10-27 选定）** | **11.3 / 15.1 / 17.6%** | −3.1% | −0.6% |
| 575 mNm / gain 3.0 | 约 +35%（仅 P5 实测） | −1.4% | −0.9% |

### 爆发测试用的是固件的 Amplify（速度触发增量）功能

固件本来就有助力功能：`S5_R5_SHOULDER_AMPLIFY_MNM`（抬臂速度超过 20°/s 时，在重力基线上叠加一个增量，换向暂停 400 ms、进入 200 ms 斜坡）。爆发测试就是验证这个功能。2026-10-27 把它的上限由 100 mNm 放开到 575 mNm（= 4.6 N·m 关节侧），以便扫描到硬件极限。

肌肉驱动模型下的结果（增益 1.5、肩上限 300 或 575）：

| 设置 | G2 峰值力矩 | G2 人体正功 | G1 峰值力矩 | G1 人体正功 |
|---|---:|---:|---:|---:|
| C1 穿戴不通电 | 46.45 N·m | 123.01 J | 4.87 N·m | 19.50 J |
| C2，Amplify 0 | 46.17（−0.6%） | 125.79 J | 4.72（−3.1%） | 19.03 J（−2.4%） |
| C2，Amplify 100 mNm | 46.17 | 125.58 J | 4.72 | 19.03 J |
| C2，Amplify 300 mNm | 46.17 | 125.30 J | 4.72 | 19.03 J |
| C2，Amplify 575 mNm（上限 575） | 46.17 | 124.79 J | 4.72 | 19.03 J |

**为什么 Amplify 也几乎没用**：它的最大可能做功 = 4.6 N·m × 0.7 rad ≈ 3.2 J，而一次爆发的人体正功约 120 J——上限就是 2.5%。扫到最大值时实测只回收约 1 J，符合这个上限。肘部爆发（G1）不受影响，因为固件只有肩通道有 Amplify。

结论：**爆发测试确实用了助力功能，但受执行器能力限制，250 ms 级爆发的收益上限只有 2–3%**。这不是控制逻辑没实现，而是"4.6 N·m 对 80 N·m 惯性需求"的量级差。

### 为什么爆发没有增益（2026-10-27 定量）

把肩部爆发（5→45°，肘 90°）的力矩拆成惯性项 `M·q̈` 和重力项 `qfrc_bias`：

| 动作时长 | 峰值惯性项 | 峰值重力项 | 重力占峰值 |
|---|---:|---:|---:|
| 250 ms | 80.75 N·m | 7.61 N·m | **8.6%** |
| 430 ms | 27.30 | 7.61 | 21.8% |
| 600 ms | 14.02 | 7.61 | 35.2% |
| 800 ms | 7.89 | 7.61 | 49.1% |
| 1200 ms | 3.50 | 7.61 | 68.5% |

**根因**：现有控制只做重力补偿（静态项）。爆发动作 90% 以上是惯性项，重力补偿理论上最多只能削掉 8.6% 的峰值力矩；而我们的上限 2.4 N·m 只是惯性项的 **3%**。所以不是"参数没调对"，而是**控制类型不匹配**——爆发需要注入能量（与速度/加速度成正比的助力），不是托住重量。

用功（能量）口径复核（肌肉驱动模型，G2）：人体正功 C0 119.6 J / C1 123.0 J / C2 125.8 J，外骨骼正功 9.08 J；把 Amplify 从 0 加到 100 mNm（0.8 N·m）只让外骨骼功从 9.08 变 9.58 J，人体功几乎不变。**C2 的人体功反而略高于 C1，原因未查明**（怀疑绑带约束在做负功或助力改变了加速度分布），列为待查项。

**文献里这类问题怎么解**：Positive Power control（Robotics and Autonomous Systems 2023，按人的任务目标注入正功）、速度场助力（ICRA 2020）、反馈力/速度控制、ExoKit `AmplifyReaction`（速度触发增量）——共同点是**按速度给扭矩**，而不是按姿态做重力补偿。

**结论**
- 加大肩幅值对**静态/准静态**工作非常有效（省力 12%→35%），对**爆发**几乎无效（≤1%）——爆发峰值力矩主要是惯性项，重力补偿占比很小。
- 以本机能力（≤4.6 N·m）而言，250 ms 级爆发不可能被明显助力；到 600–800 ms 重力占 35–49%，助力才有意义。
- 肘幅值 10 A→20 A 在肘主导的锤击里把收益从 3.5% 提到 6.8%（肌肉驱动模型），但同样不影响肩主导动作。
- 风险：4.6 N·m 已到 GDS68 旧 24 V 额定值，没有热裕量；低抬臂姿态可能"帮过头"（A 阶段热力图已显示该区域激活反而上升），需要小角度实测确认。

## 验证报告（2026-10-28）

整体结果、文献对比、图表索引见 [docs/validation_report.md](docs/validation_report.md)。按论文口径（相对穿戴不通电 + 前三角肌）报告时，助力使三角肌激活下降 **11.7–20.3%**，与肩部外骨骼文献同一区间。

## 一键出全套肌肉驱动结果（2026-10-27）

```powershell
build\sim-venv\Scripts\python.exe sim\mujoco_arm\scripts\report_muscle_all.py
```

依次跑静态保持、爆发动作、三个姿势的场景图，汇总到`build/sim-runs/report-muscle-<时间>/`：

| 文件 | 内容 |
|---|---|
| `report.png` | 静态与爆发两张结果图上下拼合 |
| `static/muscle_static.png` / `.json` | 三个手术姿势的激活量与人体力矩（C0/C1/C2） |
| `burst/muscle_burst.png` / `.json` | 首次爆发的激活量与角速度曲线、含正功指标 |
| `scenes/` | P1（20/100）、P4（30/110）、P5（60/70）三个姿势的场景图 |
| `README.md` | 各文件说明 + 从控制库读回的固件设置 |

## 肌肉驱动的爆发仿真（2026-10-27）

```powershell
build\sim-venv\Scripts\python.exe sim\mujoco_arm\scripts\muscle_burst.py                    # 无窗口
build\sim-venv\Scripts\python.exe sim\mujoco_arm\scripts\muscle_burst.py --burst G1 --viewer 0  # C2 那一轮打开 MuJoCo 窗口
```

人体改为由 MyoArm 的 63 块肌肉驱动；外骨骼仍由固件控制库驱动。窗口模式实测可用（C2 那轮同步显示，整轮按实时速度播放）。

**控制方法（2026-10-27 按文献改成计算力矩 + 静态优化）**：每 10 ms 用期望加速度 `a_des = Kp(q_ref−q) − Kd(q̇)`，按 MuJoCo 的运动方程算所需广义力

```
τ_req = M(q)·a_des + qfrc_bias − qfrc_passive − qfrc_constraint
```

再解静态优化 `min Σa² s.t. R·(F_active·a) = τ_req − R·F_passive`，把 `a` 作为肌肉激励写入 `data.ctrl`（MuJoCo 自己的激活动力学再滤波成 `act`）。

依据：
- MyoSuite 官方教程 `3.3_Inverse_Dynamics`、`3.4_Computed_Muscle_Control`（计算肌肉控制：静态优化分配肌肉激励以跟踪关节轨迹）；
- MyoSuite issue #337 中维护者的建议：对肌肉模型不能直接反解 `ctrl`，要按"期望加速度"反推；
- 计算力矩控制的标准形式 `τ = M·a_des + bias − passive`（FORC 讲义、MuJoCo 社区博客）；
- OpenSim CMC：10 ms 窗口 + 静态优化 + 残差执行器。

注意：本版本 MuJoCo 的`mj_inverse`在未开`mjENBL_INVDISCRETE`时`qfrc_inverse`恒为 0，所以不用它，直接按上式算。

改成计算力矩前馈后（旧版本的重力前馈会让肩部下垂 26°，现已 0.2°）：

| 指标 | G1 锤击 C0/C1/C2 | G2 快速抬举 C0/C1/C2 |
|---|---|---|
| 峰值人体力矩 | 4.69 / 4.87 / 4.70 N·m | 44.7 / 46.5 / 46.3 N·m |
| 峰值角速度 | 2.15 / 2.11 / 2.14 rad/s | 3.05 / 3.05 / 3.06 rad/s |
| 激活量峰值 | 0.50 / 0.51 / 0.48 | 1.84 / 1.85 / 1.91 |
| 动作时间 | 430 ms | 225 ms（参考 250 ms） |
| 稳态误差 | 1.3° | **0.2°** |

**观察**
- 肌肉驱动下肘部爆发只能做到约 430 ms（参考 180 ms）：肌肉的激活动力学与力-速度关系限制了下砸速度，这比理想力矩模型更真实。
- 助力收益在爆发里很小（G1 −3.5%、G2 −0.3%）：助力幅值（≤1.6 N·m）远小于爆发峰值力矩（5–46 N·m），更新周期的影响也被淹没。
- 输出更新周期 50/20/10 ms 的对照仍不稳定（14.06 / 7.17 / 15.94 N·m，非单调），**"提高更新率改善爆发"未被仿真证实**。

## M6 肌肉激活量（第二个省力指标，2026-10-27）

动态仿真仍用肩、肘两个理想力矩驱动；M6 把 MyoArm 的 63 块肌肉**接回来只做后处理**：对每个记录样本，用静态优化（min Σa²，约束 R·(F_active·a) = τ − R·F_passive，0≤a≤1）求出"要产生当时那个人体力矩，肌肉需要多大激活"。

| 文件 | 作用 |
|---|---|
| [sim/muscles.py](sim/muscles.py) | `MuscleEffort`：读肌肉长度/张力、数值差分求力臂、活动集法解静态优化 |
| [scripts/activation_report.py](scripts/activation_report.py) | 从`run_experiment.py`的输出算激活量，写`activation.json`与`report_activation.png` |

```powershell
build\sim-venv\Scripts\python.exe sim\mujoco_arm\scripts\activation_report.py --run build\sim-runs\final2-20261027
```

结果（所有肌肉激活量之和，越低越省力；括号内为相对裸臂）：

| 动作 | C0 裸臂 | C1 穿戴不通电 | C2 穿戴+助力 |
|---|---:|---:|---:|
| T1 | 0.500 | 0.523（+4.7%） | 0.501（+0.3%） |
| T2slow | 0.454 | 0.477（+5.1%） | 0.455（+0.2%） |
| T2fast | 0.421 | 0.448（+6.6%） | 0.424（+0.9%） |
| T3 | 0.509 | 0.540（+6.0%） | 0.511（+0.4%） |

与力矩指标一致：穿上不通电更费肌肉（+5–7%），10 A 助力后肌肉激活基本回到裸臂水平（+0.2–0.9%）。力矩指标显示还剩约 3% 的差距，激活指标显示几乎为零——因为剩余负载集中在肘部，那里的力臂较有利。

实现要点：MuJoCo 报的肌肉执行器力是"沿肌肉长度方向的负值"，取绝对值才是张力；第一版没取绝对值，导致优化把伸肌当成屈肌用（`LAT2` 被选来产生屈曲力矩），已修正。力臂用肌肉长度对关节角的中心差分（±1e-4 rad）。

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
