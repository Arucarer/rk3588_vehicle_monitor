# 平台适配层

## 职责

约定Linux设备如何接入业务，隔离设备节点、驱动访问与物理量处理。当前目录主要保存适配合同，真实设备仍逐项接入。

## 模块关系

设备驱动 → 平台读取 → [采集处理器](../include/vehicle/acquisition.hpp) → 状态快照 → 诊断与展示。SocketCAN用户态实现目前位于 `src/can_node.cpp`，并非所有平台代码都已归入本目录。

## 阅读入口

[Linux设备合同](linux/README.md)列出IIO、GPIO、MTD、显示与看门狗约束；[硬件资料](../docs/hardware/README.md)记录接口冻结；[运行主线](../src/README.md)说明业务消费方式。适配失败须发布无效状态，不能自动切换成模拟正常数据。
