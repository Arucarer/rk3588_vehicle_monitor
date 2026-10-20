# 自动测试与验证思路

## 职责

验证软件行为和异常路径。入口为根目录 `./scripts/check.sh`，由CTest组织C++单元测试和Python进程级测试。

## 测试分工

| 测试组 | 主要覆盖 |
|---|---|
| core / acquisition | 配置、队列、帧解析、速度、姿态、滤波及防抖 |
| communication / network-integration | CAN节点、命令、TCP分片与异常帧、回环通信 |
| storage / storage-integration | 循环覆盖、CRC、损坏恢复、日志导出 |
| runtime-unit / runtime / health-runtime | 进度检查、参数错误、退出及故障注入 |
| gui / gui-runtime | 页面、无效数据、交互与离屏运行 |
| deployment / simulation | 安装恢复、运行控制及模拟冒烟 |

## 如何读一个测试

先看输入与时间推进，再看状态/事件/退出码断言，最后对照对应模块。单元测试隔离算法，进程级测试验证模块协作；设备真实精度、脉冲峰值、物理断网及掉电需要板端证据。

已有执行结果见 [报告索引](../docs/reports/README.md)，任务书覆盖关系见 [验收矩阵](../docs/acceptance.csv)。
