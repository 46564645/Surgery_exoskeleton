# 硬件资源 spec（hardware）

> 全工程共享的"硬件事实基线"。RESEARCH 阶段冻结，后续以此为准；改动留变更记录。
> 机器可读锁定区在 `hw-lock.yaml`，REVIEW 的 ARCH-8 与冲突检测据此运行。

## 芯片与框架
- 芯片型号：STM32F446RET6 / NUCLEO-F446RE。
- 开发框架：CubeF4 V1.28.3，HAL/CMSIS，裸机。
- 主频 / 时钟树：HSI经PLL，SYSCLK/HCLK 84MHz，APB1 42MHz、APB2 84MHz（配置事实）。
- S1串口：USART2 PA2/PA3 AF7，115200 8N1，无流控；DMA1 Stream5 RX循环、Stream6 TX普通，均Channel4。
- NVIC：GROUP4；通信IRQ均5，SysTick为0；BSP_PB_Init之后board_runtime将EXTI15_10设为10并逐项读回，失败不启动App。
- hw-lock登记配置和设计保留项，不作为接线、电平或整机安全验证证明。S2 CAN接收平台已配置，USB查询已验证控制器启动，尚无真实CAN收帧验证；HX711仍只保留资源。

## 引脚 / DMA / 中断（人类视图）
> 详细分配维护在 `hw-lock.yaml`（机器可读）；此处写设计理由与变更记录。

## 变更记录
| 日期 | 改了什么 | 原因 |
|---|---|---|
| 2026-09-25 | 填入S1实际配置和既有CAN/HX711设计保留项 | 用户批准通信基础实施计划；硬件验证结果单列开发日志 |

## 沉淀（promote 回流）
> 本板踩过的硬件坑（如某 strap 脚、某外设时钟门）沉淀于此。
- [坑/gotcha] NUCLEO BSP_PB_Init会把EXTI15_10优先级设为15；本项目在其后由BoardRuntime_FinalizeInterrupts设为10并核验GROUP4及全部相关IRQ。应用启动放main的USER CODE BEGIN WHILE、while之前，不能放在BSP之前的USER CODE BEGIN 2。源码与再生成检查已验证，S1板上IRQ读回已完成，详见唯一开发日志。

- [S2] CAN2依赖CAN1时钟；过滤器分界14，CAN1 bank0/FIFO0、CAN2 bank14/FIFO1。配置由.ioc与hw-lock维护，CAN错误不自动恢复，查询不等于设备在线。
