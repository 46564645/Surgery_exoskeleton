# Surgery Upper Exoskeleton

当前主工作目录：`/Users/mzc/Desktop/Surgery_upper_exoskeleton`。

本仓库按该目录的现有结构维护。旧目录作为本地历史副本，不与本目录自动同步。

## 项目资料

- [系统架构蓝图](Main_%20project/docs/system_architecture_blueprint.md)
- [需求蓝图](Main_%20project/docs/Drafts/project_requirements_blueprint.md)
- [技术文档](Main_%20project/docs/upper_limb_exoskeleton_technical_document%20%281%29.md)
- [硬件清单](Main_%20project/Hardware_information_list/hardware_components_reference.md)
- [硬件接口登记](Main_%20project/Hardware_information_list/hardware_interface_registry.md)
- [架构决策地图](Main_%20project/docs/Drafts/wayfinder/project_blueprint_architecture/map.md)
- [开源借鉴索引](Open-source%20projects/开源项目借鉴索引表.md)

`self models/` 保存当前机械源文件和导出文件。`Open-source projects/` 为只读参考资料。

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
