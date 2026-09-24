---
title: 划分机械与人体连接模块
type: grilling
status: closed
parent: ../map.md
blocked_by:
  - define_architecture_viewpoints
claimed_by: codex
---

## Question

机械系统应如何表达本项目替换 ExoKit 肩部上游固定方式、保留下游右臂结构以及仍待决定的 `ELBOW` 传动承力结构？

## Confirmed so far

- 系统始终只开发 ExoKit 右臂，不包含左臂或双臂扩展。
- ExoKit 原右臂的 `SHOULDER_BACK` 通过柔性链节连接 `SHOULDER_SIDE`；本项目不配置 `SHOULDER_BACK`，改为由台架固定基座/支撑杆通过固定座直接连接 `SHOULDER_SIDE` 固定侧。
- 从 `SHOULDER_SIDE` 向下继续沿用 ExoKit 的右臂结构关系：上臂刚性连接组件连接至 `ELBOW`，`ELBOW` 输出侧再通过前臂柔性链节、前臂转接件和臂箍连接台架假肢或末端负载。
- 系统改为落地承重，不让医生承担设备自重；当前第一验收配置仍是台架假肢或连杆，不是人体穿戴验证。
- 台架只作为验证配置；最终机械主视图描述右侧、落地承重的单臂系统。
- 电机安装和输出连接仍须适配项目实际执行器，不能把 ExoKit 的 Dynamixel 安装接口视为原样可用。

## Resolution

机械架构按结构承力主链和负载接入分支分别表达。结构承力主链为：`台架固定基座/支撑杆 -> SHOULDER_SIDE 固定座 -> SHOULDER_SIDE -> joint_rod_long + arm_rail -> ELBOW -> chain_mm/chain_mf -> forearm_chain_adapter -> forearm_cuff -> 台架假肢或末端负载`。上臂臂箍和前臂臂箍是台架假肢或未来手臂接入机械链的负载分支，不与软件控制方向混写。

本项目只替换 ExoKit 在 `SHOULDER_SIDE` 上游的 `SHOULDER_BACK`、背架和肩部柔性链节连接；`SHOULDER_SIDE` 以下的关节、上臂刚性连接、`ELBOW`、前臂柔性链节和臂箍关系继续采用 ExoKit 结构语义。实际固定座承载能力、关节输出方向、机械强度和人体安全尚未通过实物验证。

落地支撑、轴承、减速和连杆结构属于机械内部实现。除非它们改变关节方向、零位、活动范围、传动比、反馈换算或允许输出，否则不作为软件模块，也不继续展开机械设计。
