# control

L3现有 `joint_state.c/.h` 实现已确认坐标转换，`safety_supervisor.c/.h`实现双关键关节配置/反馈/范围/速度检查；由L4唯一输出门控使用，不持有HAL句柄或直接发送。

S3离线实现与生产默认关闭边界见[说明](../../docs/s3_output_preparation.md)。重力补偿、FOLLOW、Lock、Amplify仍未实现。自有源文件显式加入顶层CMakeLists.txt；未知参数不能作为运行默认值。
