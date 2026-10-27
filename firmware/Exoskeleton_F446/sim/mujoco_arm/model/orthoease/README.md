# OrthoEase 上肢外骨骼 MuJoCo 模型

主入口是 [`orthoease.xml`](./orthoease.xml)。模型采用 MuJoCo 原生 MJCF，配套 18 个 STL 可视网格，保持 Fusion `final model` 的当前装配姿态与运动关系。

## 直接打开

安装 MuJoCo Python 包：

```bash
python3 -m pip install mujoco
```

打开模型：

```bash
python3 -m mujoco.viewer --mjcf=/Users/mzc/Desktop/orthoease/MuJoCo_final_model/orthoease.xml
```

也可以把 `orthoease.xml` 直接拖入 MuJoCo `simulate`。移动整个文件夹时，请保持 `orthoease.xml` 和 `assets/` 的相对位置不变。

## 坐标和单位

- MuJoCo 长度单位：米；角度内部单位：弧度。
- MJCF 中的关节范围使用度数书写。
- 坐标转换：`MuJoCo XYZ = (Fusion X, Fusion Z, -Fusion Y) × 0.01`。
- 这样处理后，Fusion 装配中沿手臂向下的 `+Y` 对应 MuJoCo 的 `-Z`，重力方向为 `0 0 -9.81`。
- 当前 Fusion 装配姿态定义为肩、肘电机的 `0°`。

## 运动链和传动

```text
stand_base（01B，固定于世界）
└── stand_passive_yaw
    ├── J01 台架被动转轴：-90° ～ +90°，无电机
    └── upper_arm_drive
        ├── J02 shoulder_motor：0° ～ 110°
        ├── shoulder_position：位置执行器，力矩限幅 ±30 N·m
        └── forearm_drive
            ├── J03 elbow_motor：0° ～ 110°
            └── elbow_position：位置执行器，力矩限幅 ±20 N·m
```

肩电机带动整个上臂组、肘电机机壳和完整前臂组。肘电机只驱动 05B 当前层级中的 05A、06 链节、08B 与 08A。控制量为弧度，例如 55° 应写为 `0.959931`。

| MuJoCo 刚体 | Fusion 部件及作用 |
|---|---|
| `stand_base` | 01B 台架杆夹持座，固定到台架杆/世界 |
| `stand_rotary` | 01A 台架上端转盘、02E 肩电机固定座、GIM6010 肩电机 |
| `upper_arm_drive` | 02C 肩驱动转子、02D 肩输出连杆、02B 上臂杆、04 调节轨、03/03C 上臂接口与绑带、05D/05C 肘电机固定结构、肘电机机壳 |
| `forearm_drive` | 05A 肘输出叉形件（位于 05B 驱动层级）、06 三头链节、08B 前臂接口、08A 前臂臂箍 |

## 人体绑定位置

模型中已经设置两个绿色/橙色 site，作为人体模型的绑定基准：

| Site | 人体位置 | 初始世界坐标，m | 所属刚体内局部坐标，m |
|---|---|---:|---:|
| `upper_arm_human_bind` | 上臂绑带内侧中心；建议绑定在肱骨中下段软组织表面 | `0.003 -0.0415 0.15134` | `0.002700 -0.056728 -0.132381` |
| `forearm_human_bind` | 前臂末端臂箍内侧中心；建议绑定在前臂远端、腕关节近侧 | `0.003 -0.0415 -0.095` | `0.000234 -0.043927 -0.121998` |

这两个位置来自当前 CAD 绑带内侧截面的中心估计。接入具体人体模型时，应把人体上臂和前臂的对应 site 放到皮肤/软组织接触中心，再对齐 site 坐标系。不同人体臂长和臂围需要重新调整人体 site，外骨骼关节轴仍应尽量与人体肩侧辅助轴、肘屈伸轴重合。

可在组合后的 MJCF 中添加 site 绑定：

```xml
<equality>
  <weld name="bind_upper_arm"
        site1="upper_arm_human_bind"
        site2="human_upper_arm_exo_attach"
        torquescale="0.03" solref="0.02 1"/>
  <weld name="bind_forearm"
        site1="forearm_human_bind"
        site2="human_forearm_exo_attach"
        torquescale="0.03" solref="0.02 1"/>
</equality>
```

其中 `human_upper_arm_exo_attach` 应属于人体上臂刚体，`human_forearm_exo_attach` 应属于人体前臂刚体。两个 weld 会形成“人体手臂—外骨骼”闭环；在正式仿真中需要依据绑带柔顺性调节 `solref`、`solimp` 和 `torquescale`。只需要位置跟随时，可改用两个 `connect` 约束。

## 仿真建模说明

- STL 用作可视几何，关闭了 STL 自碰撞；运动碰撞采用上臂、前臂胶囊体，运行更稳定。
- MuJoCo 对普通网格碰撞默认使用凸包，因此没有直接把凹形绑带网格当作接触体。
- 质量、转动惯量、执行器增益及 ±30/±20 N·m 力矩限幅是可运行的初始估计，不是电机标定数据。做控制器性能、人体载荷或安全性分析前，应替换为实测质量、质心、惯量和电机输出曲线。
- `home`、`mid_flexion`、`max_flexion` 三个 keyframe 分别对应 0°、55°、110°。
- `metadata/component_manifest.json` 保存每个 STL 与 Fusion 原部件的对应关系及关节、人体绑定点坐标。

## 文件结构

```text
MuJoCo_final_model/
├── orthoease.xml                 # 直接加载入口
├── README.md                     # 本说明
├── assets/                       # 18 个 STL 可视网格
├── metadata/
│   ├── component_manifest.json   # Fusion—MuJoCo 映射和坐标
│   ├── fusion_snapshot.json.gz   # 从 final model 导出的只读几何快照
│   └── validation.json           # 已完成的加载与运动核查记录
└── scripts/
    ├── export_fusion_snapshot.py # 从 Fusion 重新导出快照
    └── build_mujoco_package.py   # 从快照重建 STL 与 MJCF
```

格式选择依据 MuJoCo 官方说明：MJCF 是原生模型格式，刚体以嵌套 body 组成运动树，没有 joint 的子 body 与父 body 刚性连接；MuJoCo 可直接加载二进制 STL。人体绑定示例使用 site-to-site weld，便于随人体尺寸修改绑定点。参见 [MuJoCo Modeling](https://mujoco.readthedocs.io/en/stable/modeling.html)、[MJCF XML Reference](https://mujoco.readthedocs.io/en/stable/XMLreference.html) 和 [Python viewer](https://mujoco.readthedocs.io/en/stable/python.html)。
