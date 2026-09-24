---
title: 确定 ExoKit 继承边界
type: research
status: closed
parent: ../map.md
blocked_by: []
claimed_by: codex
---

## Question

ExoKit 的项目架构哪些部分可以直接成为本项目蓝图的基础，哪些部分因 STM32F446RE、GIM6010-8、M3508+C620 和常驻重力补偿而必须重写？

## Resolution

保留为架构主线：

- `Exoskeleton / Arm / Joint` 的设备对象关系。
- `Condition / Reaction / Event` 的动作表达和生命周期。
- 动作开始、运行、取消、结束后恢复的行为语义。
- `jointamplify`、`jointlock`、`cancel` 等外部命令语义。

不能直接移植：

- `DynamixelShield`、Dynamixel 工作模式和 Arduino 硬件接口。
- `HardwareSerial`、Arduino `String`、`millis()` 和 TaskManagerIO 调度。
- 大量运行时 `new/delete` 所形成的对象所有权方式。
- 将当前 Reaction 直接等同于最终执行器输出的控制方式。

本项目必须补充：

- GIM6010-8 与 M3508+C620 的统一关节执行器边界。
- 常驻重力补偿与临时动作助力的输出合成边界。
- 独立于动作层的系统状态和安全监督权。
- 适合 STM32 的确定性调度、静态资源管理和通信适配。

因此结论是“直接继承领域架构和行为语义，重新实现运行时与硬件控制架构”，而不是直接编译或移植 ExoKit 源码。

