# 公共类型与模块接口

## 职责

定义模块共享的数据和服务边界。这里看“输入输出是什么”，[src](../../src/README.md)看“具体如何实现”。

## 接口索引

| 头文件 | 阅读重点 |
|---|---|
| core.hpp | Measurement、Snapshot、StateStore、有界队列、Config及TCP帧 |
| acquisition.hpp | 速度、姿态、模拟量、数字量与环境数据处理器 |
| diagnosis.hpp | FaultRule、FaultStatus、FaultEvent与AlarmOutput |
| storage.hpp | NorDevice → CircularLog → LogService |
| communication.hpp | NodeManager、SocketCan、ParameterStore、CommandHandler与TcpClient |
| gui.hpp | GUI快照、交互回调、窗口与服务 |
| health.hpp | 基础线程进度登记与超时检查 |
| runtime.hpp | WorkerProgress、故障注入、指标与运行监督 |

## 贯穿项目的约定

测量同时携带数值、单位、来源、有效位、单调时间和错误码；使用前检查新鲜度，失效不等于零。`StateStore`短锁复制快照，多个读取者互不消费对方的数据；队列有容量与溢出计数。日志另存日历时间用于历史展示。

推荐顺序：`core.hpp` → `acquisition.hpp` → `diagnosis.hpp` → 存储/通信/GUI → `runtime.hpp`，再对照 [整体架构](../../docs/architecture.md)。
