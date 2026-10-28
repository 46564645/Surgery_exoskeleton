# S2 有界连续编码器观察

当前S2按用户批准范围（可信连续肩反馈及一次真实变化）验收通过。2026-10-03静止30秒窗口与2026-10-04明确授权的一次受监督微动共同提供证据；掉线/恢复取消、未执行。该结论不包含关节标定、速度方向或S3整体完成。本轮归档并提交至原GitHub仓库，远端结果以对应Git提交记录为准。唯一进度日志在[开发排期](<../../../Main_ project/docs/development_schedule.md>)。

## 当前验收证据（2026-10-04）

| 验收内容 | 已记录结果与边界 |
|---|---|
| 可信连续反馈 | HSE静止30秒：固件600对/1200请求、主机300组不同新鲜观测，年龄7–14ms；两CAN记录的错误/丢帧/溢出增量0。 |
| 一次真实变化 | 单次100ms/20mNm受监督请求：16组新鲜不同快照；输出许可有效时查询到的seq11→15在80ms内连续增长0.0006320477rev，超过已有静止极差0.000164rev；停止后9组新鲜值持续保留相对冻结原点+0.0003077984至+0.0005412102rev。以趋势、持续偏移和时序共同判定，通过当前肩反馈变化范围。 |
| 新鲜度与原始完整性 | 本次位置年龄7–25ms、速度6–24ms，配对相差1ms；58条完整OK/CRLF与原始hex一致。重复缓存和终末fresh0历史值不计入16组新样本。 |
| 实际动作与停止 | prepare/零目标及模式读回、闭环心跳、arm和一次pulse成功；TTL后输出撤回、驱动IDLE，250ms停稳观察通过，最终output/request/token0。 |
| 取消项 | 实物掉线/恢复取消、未执行，不记通过。 |

out反映查询时许可，seq11可能采于pulse受理前；仅取明确采于pulse受理后、stop前的seq12→15，60ms内仍三段连续增加0.0006222725rev（既有静止极差3.79倍）。速度幅值接近已有静止噪声，动作内前三段速度均值与位置变化异号，不能据此通过运动方向或精确动态响应。停止后位置范围0.0002334118rev、9个新样本持续保留正偏移；原点前后保持一致。静止与微动来自不同启动/姿态/采样频率，已有静止范围是经验对照，不是绝对噪声界限；这些限制不新增本轮验收门槛。无需为肉眼可见再驱动。

本次运行前明确记录肩CAN、UART和调度计数0；reader健康门控持续到COMPLETE间接支持肩CAN及console/心跳无故障。末尾未另读两CAN和UART/调度计数，不宣称末端全部计数直接验证为0。历史HSI CAN错误、UART缺O的物理根因继续保留。

原始[本次summary](bench/s3-capture/evidence-relative-20261004T013713353068Z/summary.json)、[events](bench/s3-capture/evidence-relative-20261004T013713353068Z/events.jsonl)、[离线复算](bench/s3-capture/evidence-relative-20261004T013713353068Z/analysis.json)、[范围判定](bench/s3-capture/evidence-relative-20261004T013713353068Z/acceptance.json)与[静止summary](bench/s2-encoder-capture/evidence-static-hse-20261003T0153385075330Z/summary.json)可复核。原始记录的physical_variation_acceptance=review_required保持原样，由独立acceptance记录审阅结论。

本次验收镜像为HSE相对台架候选ELF762c0902…500bcf54，主仓默认HSI且site禁用；不能把台架验收自动等同于默认构建上板通过。以下实现与较早窗口记录保留其原时点；旧“尚未采集/未上板”不代表本次状态。

## 来源与已有证据

本功能复用[限定SDO诊断](s2_encoder_sdo_diagnostic.md)的`gds_sdo_read`编解码器、L1 `BoardCanTx_SubmitAxes(mask=1)`和逐帧接收路径；厂家rev2.2 pp45–46的RxSdo/TxSdo规则，以及实机HW3.11.1/FW0.6.5/node1的[schema](reference/gds_hw3.11.1_fw0.6.5_endpoint0.json)与[固定读取帧](reference/gds_hw3.11.1_fw0.6.5_can_read_recipe.json)是端点依据，不借用旧版端点表。schema SHA256为cfd83580745d91c18d87c6832daa34677685e32e9d051e7f2b4bb562a90c9bc1。L1/reference出处沿用限定诊断和S3说明中的索引映射。

16:21 CAN端点372位置2.8687734603881836rev与紧邻Native USB同名位置2.868832588195801rev近似；端点378也有匹配应答，而周期029仍全零。顺序读取没有同期手动动作，静止微小速度不能证明运动/符号。该证据证明固定格式能读取具名encoder估计；厂家周期029内部取值源仍未确认。

## 实现边界

`App/application/encoder_stream.c/.h`在主循环人工启动一次有界观察。ISR只搬帧；没有动态分配、软件发送积压、CAN2发送、属性写入、模式/校准/使能接口。`src=encoder_sdo`是转子编码器估计，单位rev/rev/s，内部乘2π得到rad/rad/s；不是原始计数、关节零位或已确认机构坐标。

| 项目 | 当前实现 |
|---|---|
| 请求/应答 | CAN1/node1，标准DATA/DLC8，ID024/opcode0固定读372位置后378速度；ID025正确节点/端点/opcode/reserved、有限LE float应答 |
| 周期与总量 | 每对至少50ms、最多20Hz；30秒或1200请求上限，无补跑积压周期 |
| 应答与配对 | 单请求等待≤50ms，同时核对接收戳与主循环处理年龄；只有同周期完整位置/速度对才发布，两个接收戳差≤100ms |
| 新鲜度 | 当前会话active且完整对中较老样本年龄≤100ms才valid；停止立即valid=0，旧缓存保留用于诊断 |
| IDLE心跳 | life连续、state1/error0/fault0，年龄≤250ms；与100ms配对预算分开，未放宽S3规则 |
| 所有权 | 每次MCU启动仅一个会话，与shoulderreadpos/shoulderreadvel一次性模式互斥；不自动启动、重启或重试 |

启动及运行要求SAFE_OFF、configured/transport_bound/output_allowed/request_valid/arm_token均0、串口无故障、CAN1已启动且无bus-off。启动和每次请求前输入队列为空；运行期间CAN错误/drop/invalid/fov相对基线有增量即结束。无匹配应答、错误SDO、队列久不清、超时、过期、所有权变化或串口故障也结束，不重发、不恢复；已提交读请求的晚应答不能变成新样本。历史错误计数保留。

SDO没有线上事务token。必须隔离唯一CAN主机并假定设备按序应答；空输入队列、固定端点和接收戳不能证明设备级关联，也不能完全区分同端点重复应答。会话/pairs是本地计数。MotorWizard退出，其他CAN主机不并行；不以开启闭环解决反馈。CAN只读请求可能刷新厂家看门狗，不保证厂家内部无其他副作用。

连续观察独立于旧`shoulderfeedback`的029档案，也未连接S3生产许可提供者。旧档案100ms心跳预算可显示wire_valid=0，而本观察仍满足250ms；不能据此覆盖S3门控或把旧029零值当编码器变化。enabled_axes仍0。

## 命令与状态

- `encstart`：无参数，人工开始；阻塞条件不满足返回ERR，本次可能已到达MCU的启动不能重试。
- `encstop`：无参数，停止观察并使valid=0，不复位、不使能。
- `encfeedback`：无CAN发送，查询src/active/reason/valid/pairs、pos_rev/vel_rev_s、独立pos_ms/vel_ms、较老样本age_ms、elapsed_ms/tx/rejected及output_allowed。

reason编号为0无故障、1人工停止、2达到上限、3所有权/门控、4心跳、5CAN、6发送失败、7应答超时、8应答不合格、9样本过期、10串口故障。文本数值保留六位小数，超出显示范围时不能仅用文本当完整float精度。

## 当前现场结果与本机提示

2026-10-03首次连续窗口在00:34:29.844发起，00:34:30.021出现READY，00:34:34.871因肩侧CAN新增错误中止，固件elapsed=4952ms、reason=5、valid=0。MCU已完成99对，主机保存50组有效观测；tx=199是提交次数，不代表199次线上发送成功。肩侧err由0增至1、last_err=0x40（HAL Bit recessive），肘侧错误无增量。用户没有收到手动动作指令；主线程00:35:04收到就绪通知时窗口已中止。此次未达到连续验收，也未完成物理变化，不能以微小噪声或99对宣布通过。

原始记录在`bench/s2-encoder-capture/evidence-20261003T0034063657053Z/`。只读源码及历史证据诊断位于`D:/codex_tasks/2026-10-01/task/s2-br-diagnostic/read_only_diagnosis.json`：估算肩侧占用约4.15–5.05%，未发现请求并发、帧封装改变或队列堆积证据；电气/实际位时序根因仍未确定。未因该故障更改固件、关保护、清错、复位或重试。

原有capture仅将JSON送回代理，没有用户可见窗口；跨线程通知不能承担一次短现场窗口的动作提示。新增主机可见窗口`visible_capture.ps1`，复用原capture流程，不改固件或CAN命令。

1. 默认`powershell -NoProfile -File .\visible_capture.ps1`只显示“未开始 / 不做动作”，不枚举设备、不打开串口，开始按钮禁用。
2. 下一次现场窗口须先保存故障后状态并解决当前阻碍。当前MCU会话已用，不能直接再encstart；是否及何时复位由主线程明确安排。已完成的被动prepare不例行重复，capture自身有前置核对。
3. 只有主线程已确认新的供电窗口、MCU会话可用后，才以`powershell -NoProfile -File .\visible_capture.ps1 -Live -PoweredWindowConfirmed`打开窗口，由现场用户点击一次“开始一次读取”。程序拥有本次子进程和唯一session，真实READY必须匹配已收到的两组新鲜有效观测，才能显示绿色“可做一次小幅手动变化”。用户直接看本机绿色提示再轻微往返一次，不等待跨线程转述。
4. 后续样本无效、reason异常、ABORT、COMPLETE、进程退出或源/接收年龄达到200ms即撤销绿色；25ms界面检查，状态锁定后不恢复绿色。时钟跳变、迟到或其他session事件也拒绝。该主机提示不是运动输出许可；正常结束或数值变化不自动通过验收。
5. “停止动作提示，等待读取结束”立即撤下绿色；它不声称立即终止CAN读取。原有采集仍按最多30秒上限及finally的encstop完成清理，界面关闭请求等待该清理，不杀进程、不自动重试。

主机提示的软件失效时间预算为200ms加界面调度延迟，不能当硬件实时联锁；界面卡住或不可见时不依赖残留颜色动作。离线WAIT窗口的可见句柄及自身窗口截图已经核对，没有启动采集。带真实采样的可见提示尚未现场验证。

采集器近10Hz查询，可能跳过MCU的20Hz配对，不宣称保存全部MCU样本。设备绑定、原始应答hex/text、UTC/单调时间与CAN统计继续保存在每次evidence目录。操作入口见工具README（`tests/encoder_capture/`已于2026-10-28删除，见git历史；采集记录在`bench/s2-encoder-capture/`）。实物掉线/恢复继续记为取消、未执行。

## 软件与版本证据

七组目标主机场景已通过；心跳观察预算改为250ms后仅重跑受影响的lifecycle/faults两组，均通过。WindowsDebug构建退出0、未报warning，未重复全套或扩大静态检查通过范围。采集工具离线夹具验证和真实USB prepare均已通过；带电连续采集尚未现场运行，不算物理动态成功证据。

当前已上板HSI-derived PLL ELF SHA256=e8466a15caad32216d94a927cb14f336c5ab17b6f7601e211dbd695379ad381e；BIN72604字节/SHA256=993161f700860e0c4178e7eb767698eb863714763a01a88c89302cd360613e6d。image-manifest确认真实S3Site、无测试提供者、enabled_axes=0、CAN自动重传关闭、stream自动启动关闭。记录位于D:/codex_tasks/2026-10-01/task/s2-encoder-stream/host.json、host-final.json、build.json和image-manifest.json；manifest创建时尚未烧录。17:00:56.785536–17:00:59.484836官方烧录/校验/复位退出0；17:01:17.690341独立读回72604B与BIN一致。完整Flash备份524288B已保存。17:02:02.590084被动USB prepare通过，configured/transport_bound/output_allowed0、stream active/pairs/tx0、两CAN rx0与运行错误0；未发送CAN读取或控制命令。证据在同目录usb-flash-20261002T165957Z/result.json及usb-prepare/。16:09一次性镜像28d34ed4保留为历史记录；截至2026-10-03首次窗口中止，连续反馈与手动变化仍无现场通过证据。
