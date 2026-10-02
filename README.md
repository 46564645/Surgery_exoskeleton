# Surgery Upper Exoskeleton

以当前 Git 克隆目录作为项目根目录；文档中的工程路径均相对于此目录。当前默认开发环境为 Windows，旧目录作为本地历史副本，不与当前克隆自动同步。

## 开发入口

- VS Code：打开根目录的 [Exoskeleton.code-workspace](Exoskeleton.code-workspace)，选择 `Firmware — F446` 为活动 CMake 工程。
- 唯一活动固件为 `firmware/Exoskeleton_F446/`。工具准备、Windows 预设、构建、软件检查及串口检查见[固件 README](firmware/Exoskeleton_F446/README.md)。
- CubeMX：打开活动固件目录内的 `Exoskeleton_F446.ioc`，生成位置也必须指向该目录。
- 当前阶段和实测结果见[开发排期与唯一日志](<Main_ project/docs/development_schedule.md>)；Mac 的历史记录保留，现行 Mac 构建方式见固件 README 的兼容说明。

## 项目资料

- [系统架构蓝图](Main_%20project/docs/system_architecture_blueprint.md)
- [需求蓝图](Main_%20project/docs/Drafts/project_requirements_blueprint.md)
- [技术文档](Main_%20project/docs/upper_limb_exoskeleton_technical_document%20%281%29.md)
- [硬件清单](Main_%20project/Hardware_information_list/hardware_components_reference.md)
- [硬件接口登记](Main_%20project/Hardware_information_list/hardware_interface_registry.md)
- [电机与 CAN 模块图纸的详细文字描述](Main_%20project/Hardware_information_list/motor_can_diagrams_text.md)
- [NUCLEO-F446RE / MB1136 C04 的 AI 结构化 JSON](Main_%20project/Hardware_information_list/nucleo_f446re_mb1136_c04_ai.json)（项目接口摘录，非整板完整网表）
- [架构决策地图](Main_%20project/docs/Drafts/wayfinder/project_blueprint_architecture/map.md)
- [开源借鉴索引](Open-source%20projects/开源项目借鉴索引表.md)

`self models/` 保存当前机械源文件和导出文件。`Open-source projects/` 为只读参考资料。

AI 读取硬件手册时，有对应型号和版本且正文可读的 Word 就优先读 Word；具体入口见[手册读取索引](<Main_ project/Hardware_information_list/motor_can_diagrams_text.md#01-手册读取入口>)。接头方向、针号及图中线路仍核对原 PDF/原理图，现行接线与验证状态以硬件接口登记为准。

本次上传保留现有文档内容。文件整理后，部分文档中的旧相对路径仍需后续修正；蓝图引用的五张 SVG 当前未包含在此主目录中。

## 完整获取参考仓库

独立第三方 Git 仓库以子模块记录其来源和固定提交。GitHub 的 Download ZIP 不包含子模块源码，完整获取请运行：

```sh
git clone --recurse-submodules https://github.com/46564645/Surgery_exoskeleton.git
```

已有克隆补齐参考库：

```sh
git submodule update --init --recursive
```

不含独立 Git 历史的参考快照，以及项目文档、硬件资料和 CAD，直接保存在本仓库中。
