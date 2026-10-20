# 实板详细资源核对 · 2026-10-20

来源：用户提供的第二轮board_probe.sh终端报告；未由助手直接访问开发板。仅为资源/配置确认，未执行外设读数或功能验收。

| 资源 | 实板证据 | 结论 |
|---|---|---|
| 板载六轴 | I2C4的4-0036=sh3001_acc，4-0036-1=sh3001_gyro；驱动gsensor_sh3001/gyro_sh3001 | 实际是SH3001；不是MPU6050。沿厂商evdev适配，不能绕过已绑定内核驱动直接操作I2C |
| 加速度接口 | input名称gsensor，当前event1，ABS_X/Y/Z | 节点号可能变化，应按设备名/父设备识别；有绑定不代表已启用上报 |
| 陀螺接口 | input名称gyro，当前event3，ABS_RX/RY/RZ | 与加速度分开上报，需处理时间对齐、单位和量程 |
| SARADC | in_voltage_scale=0.439453125，IIO仅此设备 | 对照本机rockchip_saradc驱动，电压scale单位mV/LSB；量程/引出/占用仍须核对，不能接12V/24V |
| HDMI | card0-HDMI-A-1=disconnected；Writeback=unknown | 探测时未检测到已连接HDMI显示器，不与此前已验证HDMI能力矛盾。不是触摸屏验证 |
| 输入设备 | 电源键、gsensor、gyro、Razer复合鼠标/键盘、ADC键、耳机/音频状态 | 报告中未发现明确触摸屏设备 |
| CAN内核 | CAN和CAN_RAW已启用，CAN_ROCKCHIP未启用 | 应用SocketCAN接口不足以令控制器可用，需补驱动构建 |
| CAN设备树 | fea50000、fea60000、fea70000三个can节点均disabled | 需按底板原理图选择对应控制器及pinctrl/收发器配置后启用，不能全开试错 |
| DHT11/MPU6050 | DHT11、INV_MPU6050_I2C/SPI未启用 | 指定传感器尚未具备对应内核访问路径；不默认更改任务书器件 |
| SPI NOR | MTD_SPI_NOR=y，但/proc/mtd空 | 仅框架存在，未暴露W25Q128日志分区 |
| watchdog | Synopsys DesignWare，timeout=89，nowayout=1，state=active | 已处于活动态；尚未确认由谁维持、实际时间单位/复位行为。应用继续不打开/dev/watchdog，不修改其状态 |

## 本机SDK代码核对

仅作下一步适配依据，不证明与运行驱动逐字一致：

- kernel/drivers/input/sensors/accel/sh3001_acc.c：SENSOR_ON时通过ABS_X/Y/Z上报经过方向矩阵变换的原始short数据。
- kernel/drivers/input/sensors/gyro/sh3001_gyro.c：SENSOR_ON时通过ABS_RX/RY/RZ上报原始short数据。
- 两个驱动都不能直接把原始整数当g或deg/s；必须进一步核对初始化量程、比例及启用机制，再接入AttitudeEstimator。
- kernel/drivers/iio/adc/rockchip_saradc.c的IIO_CHAN_INFO_SCALE将参考电压uV除1000，并按ADC位数提供分数，因此此驱动的scale为mV/LSB；不据此指定每个物理针脚的允许电压。

SH3001可作为板载传感器适配验证对象，但任务书指定MPU6050仍保留；若最终替换，需要记录硬件变更及验收差异。

## 下一步顺序

1. 在已上传版本上验证离屏GUI、文件模拟NOR和按秒健康指标，避免显示连接问题混入应用验证。
2. 基于厂商SH3001接口核对启用状态、量程/单位和时间戳，或先冻结可用SARADC通道再读数。
3. 按实际原理图冻结CAN控制器/引脚、SPI Flash与屏幕配置，之后才编译设备树/内核补丁。
4. 明确现有watchdog维持者与nowayout行为后再安排实际复位测试。

未进行接线、i2cdetect扫描、GPIO写入、CAN/MTD启用、显示模式切换或看门狗字符设备访问。
