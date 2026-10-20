# 联调与维护工具

## 职责

为应用提供上位机、信号注入和离线分析入口。合成输入帮助复现问题，物理链路仍需单独验收。

## 工具索引

| 文件 | 用途 |
|---|---|
| monitor_server.py | 主机TCP接收、ACK及参数查询，可显式发送参数设置 |
| can_simulator.py | 通过SocketCAN发送测试节点心跳 |
| soak_test.py | 启动组合测试并汇总运行证据 |
| log_export.cpp | 构建为vehicle-log-export，离线校验并导出文件NOR日志 |
| input_probe.cpp | 构建为vehicle-input-probe，只读观察evdev事件与丢包 |
| generate_ui_font.py | 根据项目字表重新生成裁剪中文字体 |

## 阅读与使用

TCP从 [通信说明](../docs/communication.md)开始，日志从 [存储说明](../docs/storage.md)开始，输入探测见 [SH3001记录](../docs/hardware/sh3001-probe.md)。Python工具在主机运行；C++探测工具需按目标架构构建后上传。

先理解应用输出，再用工具核对协议、记录和数据来源，最后到 [测试目录](../tests/README.md)查看自动断言。
