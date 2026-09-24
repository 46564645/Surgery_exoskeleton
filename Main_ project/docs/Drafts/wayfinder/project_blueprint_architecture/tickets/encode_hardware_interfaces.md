---
title: 建立硬件接口编码表
type: task
status: closed
parent: ../map.md
blocked_by:
  - define_electrical_modules
claimed_by: codex
---

## Question

在进入跨域接口映射、CubeMX 资源分配或具体接线前，如何依据 Hardware List 为每台设备、接口组和信号建立稳定、唯一且可追溯的编码，同时不提前决定项目接线和 MCU 资源分配？

## Scope

- 使用 `Main_ project/hardware_list/hardware_components_reference.md`作为本轮硬件清单来源。
- 编码设备、接口组、接口内信号、电源域和逻辑网络。
- 记录接口方向、证据和验证状态。
- 可以登记对应型号官方资料已经明确的厂家连接器、针位和信号身份，但不分配 STM32 外设实例或 GPIO，不建立跨设备接线，不设计线束。
- 不添加 Hardware List 中不存在的电磁铁、急停器件、DC-DC 或保护器件。

## Deliverable

`Main_ project/hardware_list/hardware_interface_registry.md`

## Resolution

已建立型号资料级《硬件接口编码表》，覆盖 Hardware List 中 MCU-01、ACT-01、ESC-01、MOTOR-01、FORCE-01、ADC-01、CAN-01/02、PWR-01 和 IMU-01/02，并登记电源域、逻辑网络、接口组及资料能够确认的信号。

登记采用四级证据边界：对应型号厂家资料可以确认设备接口；芯片或参考板资料只标为`REFERENCE_ONLY`；商品页只保留直接可见事实；项目规划只标为`PLANNED`。因此GDS68、C620、裸M3508和NUCLEO可按型号手册细化；FORCE-01已确定为5 kg直梁式传感器，ADC-01资料原理图确认固定10 SPS，但二者仍需到货实物核对；未核对实物的SN65HVD230模块和缺少厂家手册的S-250-24继续保留明确门禁。

本票据没有选择 CAN1/CAN2、USART、GPIO、AF、DMA、中断、CAN ID、控制周期、供电支路或线束。文档末尾列出了每台设备进入接线设计前必须取得的实物证据。完成后，“定义跨域接口与关系”可以继续描述编码后的端点关系，但具体接线和 CubeMX 资源锁定仍需等待这些实物核对项。
