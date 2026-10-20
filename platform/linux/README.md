# Linux 设备适配合同（待实现）
任何设备打开或读取失败，返回来源、错误码、单调时间戳及 valid=false，禁止回退为仿真或正常零值。每个设备句柄由单一采集线程持有，阻塞访问须有超时和可终止策略。

| 模块 | 接口 | 前置条件 |
|---|---|---|
| MPU6050 | IIO 优先，备选 i2c-dev | 确认芯片、地址、总线、scale；不得双重占用 |
| DHT11 | 内核 DHT11/IIO | 确认驱动支持，周期至少按器件要求；禁止用户态忙等 |
| hall | 内核计数或 GPIO 事件 | libgpiod API 依实际镜像冻结；峰值计数丢失验收 |
| ADC | IIO | 输入保护、通道量程、scale/offset、外置ADC型号冻结 |
| DI/alarm | GPIO character device | 极性、去抖和驱动电路冻结 |
| CAN | PF_CAN / SocketCAN | 收发器、终端、电源及500kbit/s台架验证 |
| Flash | SPI NOR / MTD | 专用分区及擦除几何冻结；禁止自动擦除未知分区 |
| display/input | fbdev 或 DRM + evdev | LCD与触摸控制器、驱动及240×320方向确认 |
| watchdog | Linux watchdog ioctl | 关键线程进度联锁、timeout/nowayout行为实测 |

本目录保留硬件适配合同；第4阶段已在src/can_node.cpp实现SocketCAN用户态适配，其他真实板级设备尚未适配。SocketCAN物理链路未验证，详见docs/reports/stage4-validation.md。

## 接入顺序

先在 [硬件表](../../docs/hardware/README.md)冻结接口，再实现设备读取与资源释放，交给采集算法转换，最后发布快照。对照 [公共接口](../../include/vehicle/README.md)保留单位、来源、错误码与有效期；用断开、超时和恢复场景验证，不只测试正常读数。
