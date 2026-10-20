# 首次实板环境与运行冒烟 · 2026-10-20

证据来源：用户在本次对话粘贴的开发板终端输出。助手未直接SSH登录；未提供原始metrics JSONL，不能据此报告CPU/RSS、每个健康周期或精确时延。

## 已通过

命令：`vehicle-monitor --version`及`--simulate --can-simulate --duration 10 --config vehicle.conf --metrics-file /tmp/vehicle-test-<时间>.jsonl`。

- ARM64程序在板端可启动，版本0.2.0+c2bfce9ce6e0，源码指纹c2bfce9ce6e00933c7f11ad05118f0602e396de48334f882df0ba4d3d5cfe8d7。
- 模拟超速、液位、母线及倾斜出现告警和恢复；节点2出现离线故障、恢复与正常，节点1未见离线故障。
- 输出`SIMULATION stopped; log_dropped=0`，退出码0，未见HEALTH超时错误。
- 这证明当前构建的启动依赖和短时软件运行路径在该板可用，不等同于所有依赖/功能均已验收。

提取的运行输出见[原始片段](board-smoke-20261020.txt)。未把模拟传感器或CAN注入当作物理外设测试。

## 板端实际观察

| 项目 | 用户提供的探测结果 | 含义/后续 |
|---|---|---|
| 内核 | Linux5.10.160 #6，aarch64，构建时间2026-10-12 | 与用户说明一致；不证明等于主机当前.config |
| 系统 | Buildroot2021.11，VERSION=-g2a024528-dirty，glibc2.37 | 保存实际系统标识，不以发行名推测libc |
| 设备树model | RK3588 MyBoard Project | 实际自定义板型，物理底板修订仍待核对 |
| PID1 | /usr/bin/busybox | 后续优先BusyBox/SysV启动路径，仍需核对启动目录与调用方式 |
| I2C | /dev/i2c-0..7及9 | 总线接口存在，不证明MPU6050已连接或可用 |
| GPIO | /dev/gpiochip0..5 | 不等于针脚已冻结或可直接驱动负载 |
| IIO | iio:device0=fec10000.saradc，voltage0..7_raw及scale | 8个驱动逻辑通道，不代表8路都已引出、空闲或适合车载电压 |
| DRM | card0为display-subsystem，card1为NPU | card1不能当作第二个普通显示输出；未见/dev/fb* |
| 输入 | event0..13 | 尚未读取设备名称/能力，不能认定已有触摸屏 |
| MTD | /proc/mtd仅表头 | 未暴露日志Flash分区，W25Q128尚未确认绑定 |
| CAN | 网络列表中无CAN接口 | 当前未暴露SocketCAN接口，需结合运行内核与DT确认 |
| watchdog | /dev/watchdog及/dev/watchdog0 | 节点存在，不代表应用已打开/喂狗或复位已验证 |
| 以太网 | eth0 UP/LOWER_UP，SSH/SCP已能使用 | 管理链路可用，未进行本项目TCP协议板端验证 |

## 下一步

更新board_probe.sh并再次只读探测：现有I2C设备绑定、ADC scale、DRM连接器、输入名称、watchdog sysfs元数据、运行内核配置和CAN设备树候选节点。脚本不扫描I2C地址、不请求ADC新采样、不写GPIO/Flash、不切换显示模式、不打开watchdog字符设备。

其次验证板端离屏LVGL与文件模拟NOR，随后进行真实SARADC/IIO及已确认传感器的单模块适配。引脚、电压域、屏幕控制器和量程仍需接口冻结；不得根据软件节点数直接接线。

本轮未启用GUI、持久化日志文件、TCP客户端、真实CAN或看门狗，也未进行10分钟/24小时稳定性测试。
