# application

`app_main`负责HAL毫秒时基上的非阻塞周期调度，`console_commands`负责固定长度收行和CLIP静态命令适配。通过board_console/board_runtime调用平台层，不直接包含HAL头文件。

本轮始终报告`S1_COMM`及`output_allowed=0`，尚无完整状态机或电机输出路径。新增源文件显式加入顶层CMakeLists.txt；通信参数与测试入口见固件根README。
