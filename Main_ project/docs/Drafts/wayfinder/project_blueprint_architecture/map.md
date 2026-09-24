# 上肢助力外骨骼项目蓝图架构决策地图

## Destination

形成一份可交付的完整系统架构蓝图：机械、驱动与供电、传感与通信、嵌入式软件、控制行为和安全关系采用同等深度进行拆分，并最终给出架构概述、模块清单和模块间关系图。

蓝图应足以指导后续模块设计，但不提前写具体实现代码、控制参数或未经确认的硬件细节。

## Notes

- 使用 Wayfinder 逐项解决架构决定；当前阶段只做规划和决策，不实施模块。
- ExoKit 是系统领域架构和交互语义的主参考，不能把其 Arduino/Dynamixel 工程直接移植到 STM32F446RE。
- 所有专业视图和模块划分必须先回答“保留了 ExoKit 的什么思想、对象结构、职责关系或行为语义”，再说明为了本项目硬件做了哪些适配修改。不得从通用 STM32、电机驱动或其他机器人项目重新建立一套与 ExoKit 无关的总体架构。
- 硬件适配遵循“上层保持 ExoKit，底层按实物替换”：保留 `ExoskeletonSystem / RightArm / Joint`、`Condition / Reaction / Event`及动作生命周期；将 Arduino、Dynamixel、TaskManagerIO、动态内存和原有输出方式替换为 STM32F446RE、GIM6010-8/GDS68、裸 M3508/C620及本项目安全控制所需实现。
- 后续遇到新问题先检查 ExoKit 的实际代码或文档，再对照权威索引表 `Main_ project/OPEN_SOURCE_PROJECTS_INDEX.md`。索引已覆盖且不冲突时直接采用并记录；只有索引未覆盖、参考不适配或仍存在实质功能取舍时，才说明依据并请用户确认。
- 当 ExoKit 未覆盖某项功能或控制细节时，先用《开源项目借鉴索引表》确定所属模块和主参考，再只检查该行列出的对应开源项目；索引中的旧硬件映射不得覆盖本地图内用户最新确认的绑定关系。
- 已写入蓝图的内容也必须持续与《开源项目借鉴索引表》回查。索引已覆盖的模块直接采用该行的主参考、拿走部分、复用判断和改写量，不再请用户重复选择参考路线；只有索引缺失、索引方案与最新项目约束冲突或仍存在功能取舍时才提问。
- 每个使用外部参考形成的决定，必须在对应票据和最终蓝图模块旁记录：索引表模块编号与名称、主参考项目、实际核对的源码/文档位置、具体借鉴内容，以及保留或修改 ExoKit 的部分。不得只写项目名称或只在对话中说明来源。
- 只把会实质改变系统行为、模块所有权或验证路线，且存在两条以上合理路径的问题交给用户确认。ExoKit 或索引已经给出明确可用方案、且不与项目约束冲突时，直接采用并记录来源、拿走部分、改写量和边界，不重复提问。确需确认的问题提供 2-3 个明确方案、推荐项、相对对应模块的 ExoKit 参考实现预计改写量及优缺点。
- 同一轮集中确认一组处于同一抽象层级的问题；控制软件按“总体程序架构、分层边界、层间依赖与数据流、层内模块、底层驱动”的顺序自上而下推进，避免在总体架构未定时提前钻入状态字段或驱动细节。
- 固定硬件边界：STM32F446RE；`SHOULDER_SIDE` 使用 GIM6010-8 + GDS68；`ELBOW` 使用裸 M3508 + C620。
- 右臂机械链固定为：台架固定基座/支撑杆 -> `SHOULDER_SIDE` 固定座 -> `SHOULDER_SIDE` -> ExoKit 上臂刚性连接组件（`joint_rod_long`、`arm_rail`、上臂臂箍）-> `ELBOW` -> ExoKit 前臂柔性链节（`chain_mm`、`chain_mf`）-> `forearm_chain_adapter` 与前臂臂箍 -> 台架假肢或末端负载。
- 软件对象固定为 `RIGHT/SHOULDER_SIDE -> GIM6010-8/GDS68`、`RIGHT/ELBOW -> M3508/C620`。本系统不配置 `SHOULDER_BACK`；原 ExoKit 的 `SHOULDER_BACK -> flexible chain link -> SHOULDER_SIDE` 上游结构由台架固定座直接连接 `SHOULDER_SIDE` 的方式取代。
- 当前控制路线：电机编码器反馈；持续重力补偿作为基础；动作放大依据方向和速度介入；力传感器只作辅助监测；当前不使用 IMU。
- 系统只开发右臂；最终机械系统采用落地承重路径，由支撑杆把设备载荷传递到地面，不把整机重量交给医生承担。
- 后续只保留会影响关节方向、零位、活动范围、传动比、反馈换算和允许输出的软件相关机械接口，不展开机械内部设计。
- 设置“具体接线门禁”：当前蓝图只描述逻辑接口、供电域和信息方向；一旦工作需要指定 STM32 外设实例或引脚、连接器针脚、驱动器端子、传感器线色、CAN 端点或线束编号，必须立即停止。先依据权威硬件清单 `Main_ project/hardware_list/hardware_components_reference.md`、接口编码表 `Main_ project/hardware_list/hardware_interface_registry.md` 及对应官方资料核对，经用户确认后才能继续具体接线设计。
- 接口编码至少区分设备编号、接口编号、信号编号和电源域，并保存“来源设备、目标设备、信号/电压类型、方向、官方依据、验证状态”；编码前不得用临时引脚名形成事实上的接线决定。
- 项目文档、图表、接口和后续软件统一使用 ExoKit 语义名 `SHOULDER_SIDE`、`ELBOW`，不再使用旧的数字关节编号作为当前系统名称。
- 机械、电气、软件和控制必须在同一系统边界内互相映射，不能形成四份彼此无关的清单。
- 参考文章只提供蓝图格式和分层原则，不能替代项目自身的领域划分。

## Decisions so far

- [确定蓝图覆盖范围](tickets/confirm_blueprint_scope.md)：蓝图覆盖完整系统，机械、电气、传感通信、控制、安全和软件采用同等深度拆分。
- [确定 ExoKit 继承边界](tickets/determine_exokit_reuse_boundary.md)：继承 ExoKit 的设备对象、动作表达和生命周期语义；重写硬件耦合、调度、内存、控制合成与安全执行部分。
- [确定蓝图的架构视图](tickets/define_architecture_viewpoints.md)：使用面向 AI 的 Markdown，以一张系统总览和四张同等深度的专业视图表达完整架构，图中关系必须同时有可直接读取的文字表示。
- [划分机械与人体连接模块](tickets/define_mechanical_modules.md)：台架固定座直接连接 `SHOULDER_SIDE`；从 `SHOULDER_SIDE` 向下沿用 ExoKit 的上臂刚性连接、`ELBOW`、前臂柔性链节和臂箍关系。
- [划分控制与嵌入式软件模块](tickets/define_control_software_modules.md)：采用 L1 平台、L2 设备通信、L3 领域控制、L4 系统应用四层架构；保留 ExoKit 对象和动作语义，增加持续重力补偿、命令合成与独立安全门，并按开源索引锁定 32 个模块的参考路线。
- [划分驱动供电与通信模块](tickets/define_electrical_modules.md)：沿用 ExoKit 的 RightArm/Joint 硬件归属，以双 CAN 适配 `SHOULDER_SIDE`的 GDS68/GIM6010-8 和 `ELBOW`的 C620/裸 M3508；24 V 动力、低压逻辑、串口遥测和辅助力监测分域表达，未确认的接线与保护器件保持 TBD。
- [确定运行状态与安全控制权](tickets/define_operating_states_and_safety_authority.md)：保留 ExoKit Reaction 生命周期和异常关闭全部主动关节的语义，在外围增加独立状态机与 SafetySupervisor；正常模式为 FOLLOW、ASSIST、LOCK，故障拥有最终输出撤销权，并预留未来电磁吸附确认用的 `assist_enable_permission`。
- [建立硬件接口编码表](tickets/encode_hardware_interfaces.md)：按各设备对应型号资料建立接口、信号、电源域和逻辑网络编码；厂家手册、参考板、商品资料和项目规划分级记录，未核对实物的针位不进入接线事实。
- [定义跨域接口与关系](tickets/define_cross_domain_interfaces.md)：以 `XDOM-MECH-*`、`XDOM-ENERGY-*`、`XDOM-INFO-*`、`XDOM-CTRL-*`分开表达载荷、能量、信息和控制权限；台架固定座直接连接 `SHOULDER_SIDE`，安全监督拥有两个主动关节的最终输出许可与撤销权。
- [确定关系图组合与蓝图格式](tickets/select_diagram_set_and_blueprint_format.md)：最终交付为独立的`Main_ project/system_architecture_blueprint.md`，包含五套SVG、Mermaid源图和等价Markdown关系表；五个专业视图共享规范名称、接口编号、方向与验证状态。

## Wayfinding status

- 本轮系统架构票据已全部关闭，权威交付物为`Main_ project/system_architecture_blueprint.md`。
- 蓝图中的`TBD`不是遗漏，而是需要实物识别、具体接线门禁、CAD审查或台架验证后才能解除的工程边界。

## Out of scope

- 编写固件、驱动或控制算法实现。
- 确定未验证的 CAN 字段、引脚、控制周期、阈值、增益、转矩或电流参数。
- 确定 ELBOW 最终减速、输出轴、轴承和人体承力方案。
