# 验证报告索引

## 职责

记录执行条件、实际结果和证据。阶段完成指对应软件范围完成，不自动代表任务书硬件验收通过。

## 推荐阅读

| 内容 | 入口 |
|---|---|
| 软件集成、13项主机测试及组合运行 | [第6阶段报告](stage6-validation.md) |
| 算法、存储、通信、GUI分阶段验证 | [阶段2](stage2-validation.md)、[阶段3](stage3-validation.md)、[阶段4](stage4-validation.md)、[阶段5](stage5-validation.md) |
| SDK与ARM64构建 | [SDK验证](sdk-validation.md) |
| 板端首次运行与资源调查 | [冒烟](board-smoke-20261020.md)、[资源明细](board-detail-20261020.md) |
| 板端离屏GUI及文件日志 | [GUI与日志](board-gui-storage-20261020.md) |
| 真实以太网60秒基础通信 | [TCP联调](board-tcp-20261020.md) |

## 如何判断结论

区分主机/板端、模拟/真实设备、测试时长与证据范围。JSON/CSV保存结构化结果，Markdown解释条件及边界；后续测试应增加新证据，保留历史报告。下一步见 [实板推进清单](../hardware/next-steps.md)。
