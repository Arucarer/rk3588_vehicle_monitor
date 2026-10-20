# CAN与TCP软件联调

完整字节布局、命令、错误码与容量见[协议](../protocols/README.md)。

## 本机TCP回环

终端1启动上位机：

```sh
python3 tools/monitor_server.py --bind 127.0.0.1 --port 9000 --duration 60
```

终端2启动模拟采集+CAN节点注入+TCP客户端：

```sh
./build/vehicle-monitor --simulate --can-simulate --duration 30 --tcp 127.0.0.1:9000
```

上位机自动ACK心跳和状态，连接后查询参数并打印收到内容。启用远程设置时先将配置复制为独立运行配置（避免修改仓库默认配置）：

```sh
cp config/vehicle.conf /tmp/vehicle-runtime.conf
# 上位机选项 --set overspeed_kmh=100 在每次建立连接后发送设置
python3 tools/monitor_server.py --port 9000 --duration 60 --set overspeed_kmh=100
# 另一终端
./build/vehicle-monitor --simulate --duration 30 --config /tmp/vehicle-runtime.conf --allow-remote-set --tcp 127.0.0.1:9000
```

工具默认只绑定回环；板端接入时上位机可绑定已确认的主机IP，如192.168.88.100，板端客户端用`--tcp 192.168.88.100:9000`。当前没有执行板端网络测试。

需要观察日志可同时增加 `--log-file /tmp/vehicle-comm.nor`，使用方式见[日志说明](storage.md)。网络断开不会暂停模拟采集与本地日志；不实现离线告警网络重传。

## CAN节点注入测试

`./scripts/check.sh`自动执行双节点注入、掉线恢复和TCP回环测试，不需要root或vcan。

若已有配置好的vcan0，可在两终端运行：

```sh
./build/vehicle-monitor --simulate --can-interface vcan0 --duration 20
python3 tools/can_simulator.py --interface vcan0 --duration 15
```

工具仅发送测试报文，不创建接口、不配置bitrate、不改变网络。真实can0上的发送需先确认500kbit/s、节点ID、收发器、终端和线束，再将接口名替换。未验证的板上CAN驱动不能通过运行此命令自动补齐。

本机没有现成vcan接口，因此本阶段未执行vcan/物理CAN链路测试；提供SocketCAN适配代码、缺失接口重试测试和可复用的报文发生器。

## 长时间与故障验证

可将上位机`--duration`与应用`--duration`设置为1800进行30分钟收发，再在台架切断链路60秒，恢复后记录服务端就绪至重连耗时。当前自动化只覆盖短时真实回环连接关闭和ACK黑洞，不将其算作任务书30分钟/60秒断网验收通过。
