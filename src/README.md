# 核心实现与运行主线

## 职责

把采集、诊断、通信、存储和GUI组装成可运行应用。先读 `main.cpp`，再按下表追踪模块；接口定义在 [include/vehicle](../include/vehicle/README.md)。

## 文件分工

| 文件 | 主要内容 |
|---|---|
| main.cpp | 参数校验、资源创建、模拟采集、诊断循环与退出清理 |
| core.cpp | 配置校验、快照、速度换算、CRC与TCP帧编解码 |
| acquisition.cpp | 速度估计、姿态互补滤波、模拟量滤波、数字防抖和DHT数据处理 |
| diagnosis.cpp | 多故障持续时间、滞回、恢复、确认及声光节奏计算 |
| storage.cpp | NOR文件后端、循环日志、恢复、异步写入与导出 |
| can_node.cpp | CAN心跳解析、节点超时和SocketCAN服务 |
| commands.cpp | 参数存储、命令校验、ACK/NACK与请求去重 |
| network.cpp | 非阻塞TCP、心跳、上报、应答超时与重连 |
| runtime.cpp | 故障注入、运行指标及独立健康监督 |

## 一轮数据如何流动

采集线程约20ms生成快照并发布到 `StateStore`；主线程约50ms读取，合并CAN/TCP状态，执行 `Diagnosis`，将变化事件分别交给输出队列、持久化日志和GUI状态。TCP独立读取快照，不抢走GUI或诊断的数据。

日志、CAN、TCP、GUI各有工作线程；监督检查有效进度，异常触发退出。信号或到期后停止生产、等待线程并清理资源。GUI的LVGL调用集中在GUI线程，磁盘写入交给对应服务。

## 理解边界

采集算法可以独立测试，但当前应用采集入口仍是显式仿真。声光策略输出逻辑电平，尚未驱动真实GPIO；文件NOR也不是MTD。读实现时结合 [测试](../tests/README.md) 区分算法验证与硬件验证。
