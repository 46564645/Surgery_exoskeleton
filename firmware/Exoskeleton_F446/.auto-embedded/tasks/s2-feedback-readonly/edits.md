# 编辑清单

| 文件 | 改动 | 验证标准 | 结果 | commit |
|---|---|---|---|---|

| App/devices/c620_feedback.c/.h | L2 C620大端/SI/接收时间戳/新鲜度状态，无输出路径 | Codec/invalid/freshness/wrap测试与ARM构建 | 通过，未板验 | 未提交 |
| App/application/can_monitor.c/.h、console_commands.c | 每帧接入及elbowfeedback；原始查询/错误保留 | 隔离/陈旧队列/查询格式/启动失败 | 主机通过，未烧录 | 未提交 |
| Core/Inc/s2_config.h、CMakeLists.txt | 集中ID1/暂定100ms，显式源与include | 100/101ms、配置范围、Debug构建 | 通过，阈值未真机验证 | 未提交 |
| tests/host/test_c620.c/.h、test_s1.c、can_mock.c/.h、run_host_tests.py | 8项C620场景及既有25项回归 | MSVC C11 /WX | 33/33 PASS | 未提交 |
| tests/bench_can_serial.py | help兼容旧/新增查询表，不改变断开驱动器前提 | Python语法及主机命令表 | 未用于已接驱动器真机验收 | 未提交 |
| README.md、App/devices/README.md、prd.md、task.json、schedule及接口登记 | 单位、软件/板测区分、用户批准顺序与剩余肩侧门槛 | 与真实证据一致 | S2继续active，非完成 | 未提交 |
