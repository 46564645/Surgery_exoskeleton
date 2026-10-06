# GDS68 MIT适配依据与待验证项

研究日期：2026-10-06。目标设备：本项目肩侧 GIM6010-8 / GDS68，已归档身份 HW3.11.1 / FW0.6.5、node 1。

本文保存手册、参考源码与实机接口定义的对照结论，供后续适配检索。MIT尚未接入或验收；本文不授权切换模式、写参数、升级固件或运动。CAN `0x10` 是STM32 HAL的帧格式错误，尚无证据表明改用MIT能够解决该错误。

## 1. 来源与证据等级

| 来源 | 已核对内容 | 适用边界 |
|---|---|---|
| [厂家GDS68 / GIM6010-8手册 rev2.2](https://14180476.s21i.faiusr.com/61/ABUIABA9GAAgzpi6wQYoh9D9CQ.pdf) | §3.1.7（原PDF第33页）MIT模式、轴侧定义；§4.1.1 ID规则；§4.1.2（原PDF第47–49页）Mit_Control及模式字段；§4.6.1（原PDF第85页）SDK入口 | 手册规定；不等于HW3.11.1 / FW0.6.5的所有行为已实测 |
| [厂家SDK MWMotor.c](https://gitee.com/cyberbeast/mwmotorsdk/blob/master/MWSDK/MWMotor.c) / [MWMotor.h](https://gitee.com/cyberbeast/mwmotorsdk/blob/master/MWSDK/MWMotor.h) | `MWMitControl`、`MWMitControlRec`、`MWSetControllerMode`及命令/模式枚举 | 2026-10-06读取的master源码实现；链接会变化，头文件默认 `FIRMWARE_VERSION_NUMBER=513`，不能当作FW0.6.5参数表 |
| [Rhino固定版本 gim6010_drv.c](https://github.com/Coderyne/Rhino-Pi-X1-ROS2HomeRobots/blob/a4b24b80c00aa1cbf58d2a8a20167d8710d97bec/Code/STM32/ros2-wheel-foot-car/User/Devices/SW_Motor/gim6010_drv.c) | `SWMitControl`、`SWSetControllMode`、`GIM6010_fbdata` | 项目参考实现；其力矩倍率、控制参数和硬件配置不直接继承 |
| [实机endpoint0归档schema](gds_hw3.11.1_fw0.6.5_endpoint0.json) | 本机MIT相关字段的名称、端点ID、类型和访问属性 | 实机接口定义；不是参数取值快照，也不证明实际处理行为 |

对应项目开源借鉴索引第8行（GIM6010-8驱动适配）。复用候选为报文封装、反馈分发骨架；转换、校验及平台接入仍需按本项目条件适配。

## 2. 手册规定与源码实现

### 报文与模式

- 标准CAN ID为 `(node_id << 5) | 0x008`；node 1对应 `0x028`。
- MIT发送载荷为8字节：位置16位，速度、Kp、Kd、力矩各12位。
- 位置高8位在BYTE0、低8位在BYTE1；其余字段跨字节按位打包。不能将普通SIMPLE浮点字段的小端解析直接用于MIT载荷。
- 手册MIT配置为 `control_mode=3`、`input_mode=9`。配置模式和进入闭环是不同操作，本文未执行任何一项。
- 手册定义：CAN MIT的位置、速度和力矩均为**输出轴侧**；USB控制输入为**转子侧**。CAN MIT位置为rad、速度为rad/s、力矩为Nm；USB示例位置/速度使用turns、turn/s。

### 参考SDK固定编码范围

| 字段 | 位宽 | 源码编码范围 |
|---|---:|---:|
| 位置 | 16 | −12.5～+12.5 rad |
| 速度 | 12 | −65～+65 rad/s |
| 力矩 | 12 | −50～+50 Nm |
| Kp | 12 | 0～500 |
| Kd | 12 | 0～5 |

以上是所读SDK中的固定量化范围，**不是本机已读参数，也不是项目运行限值**。Kp/Kd的轴侧、单位及换算须与实际固件一致，不能仅凭量化范围推定。

## 3. 实机接口定义与适配缺口

### 可配置量程的实际值尚未确认

归档schema中的以下字段均为 `float`、可读写；本次只核查归档，没有连接设备读取或写入。

| 字段（前缀 `axis0.controller.config.`） | 端点ID |
|---|---:|
| `mit_max_pos` | 335 |
| `mit_max_vel` | 336 |
| `mit_max_torque` | 337 |
| `mit_max_kp` | 338 |
| `mit_max_kd` | 339 |

字段存在不证明实际取值等于SDK常量，也不证明它们在FW0.6.5中的缩放、边界处理及收发路径已验证。后续确定编码范围时，须使用匹配设备的实际读值及适用协议依据，不能仅把SDK版本宏改为605就认定兼容。

### 轴侧与力矩倍率

Rhino的 `SWMitControl` 在编码前执行 `ffTorque = ffTorque * 8.0f`，属于该项目处理。当前本项目SIMPLE力矩链采用转子Nm，MIT按手册采用输出轴Nm；直接插入这段代码可能重复转换或使用错误轴侧。后续须明确每个接口接收的轴侧、单位、符号及转换位置，再决定转换公式，不复制该倍率或参考项目控制参数。

### 编码类型与输入校验

厂家SDK和Rhino的位置量化变量均使用 `int16_t`，但16位无符号编码范围为0～65535，超过有符号16位类型可表示的范围。不能依赖窄化转换或带符号移位的偶然结果。后续适配应使用适当的无符号类型及有界中间值，并检查有限值、上下界、量化和位打包；不得让NaN、无穷大或超范围值直接进入编码。

## 4. 后续使用边界

- 可参考厂家SDK和Rhino的字段排列、节点/命令分派；不整套替换当前CAN底层或七属性反馈。
- 实际MIT量程、输出轴/转子侧换算、增益语义及反馈解释确认前，不把MIT编码视为可上板控制的完成实现。
- 后续若实现适配，至少核对最小值、最大值、零附近量化、越界及非有限输入，检查打包/解码一致性；协议检查通过不等于运动验收。
- MIT模式切换、反馈与停止行为均尚未在本机验收。通信 `0x10` 的根因仍需独立证据，MIT研究不推进S3/S4验收状态。

本轮仅保存研究参考；固件、驱动参数、接线均无变化，未编译、烧录、连接设备或执行硬件验证。
