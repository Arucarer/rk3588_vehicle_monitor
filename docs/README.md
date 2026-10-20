# 项目文档与阅读路线

## 职责

把需求、代码和验证证据串起来。首页看全貌，目录README解释实现，专题文档说明细节，报告记录实际测试结果。

## 阅读顺序

1. [首页](../README.md) → [阶段计划](roadmap.md)：了解目标、范围和进度。
2. [架构](architecture.md) → [公共接口](../include/vehicle/README.md) → [运行主线](../src/README.md)：理解数据和线程如何协作。
3. [界面](../gui/README.md)、[存储](storage.md)、[通信](communication.md)：跟进具体功能。
4. [配置](../config/README.md) → [构建](../cmake/README.md) → [脚本](../scripts/README.md) → [部署](../deploy/README.md)：从源码走到板端。
5. [测试](../tests/README.md) → [验证报告](reports/README.md) → [硬件适配](hardware/README.md)：判断哪些已验证、哪些仍需实测。

## 关键资料

[可行性](feasibility.md)说明方案依据；[验收矩阵](acceptance.csv)对应任务书检查项；[协议](../protocols/README.md)定义双方字节布局。历史阶段报告保留当时状态，当前进度优先看阶段计划和最新实板报告。
