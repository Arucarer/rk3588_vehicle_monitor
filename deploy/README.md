# 软件部署、监督与恢复

当前是**软件仿真版**。安装与构建不会自动启动应用、修改主机init或打开硬件看门狗。所有软件阶段通过不代表板端/硬件验收完成。

## 构建与打包

```sh
./scripts/check.sh
./scripts/build_rk.sh
python3 scripts/package.py --build-dir build-rk --output-dir out --label software
```

包内有应用、导出工具、默认配置、联调工具、许可、部署脚本、manifest.json、release-id及SHA256SUMS。另生成tar.gz。相同输出目录/标签不覆盖旧包。打包会先重新构建，不执行ARM64程序。主机测试包可将build-rk替换为build。

软件版本为0.2.0+源指纹短码；完整应用源指纹、Git基线及架构写入manifest。Git基线不代表工作树干净，源指纹用于区分未提交构建；SHA256SUMS对应实际交付文件。源指纹涵盖项目C++/头文件/GUI配置/字体/业务配置/构建模板，并包含固定LVGL提交标识。编译器/动态依赖仍需依据SDK记录和实际ELF检查核对，不将源码指纹当作二进制校验。

## 安装到独立目录

以下命令在目标设备执行，包需先解压；路径按实际包名替换：

```sh
sh PACKAGE/deploy/install.sh PACKAGE /opt/vehicle-monitor
# 明确启用仿真；未创建此文件时start拒绝运行
 touch /opt/vehicle-monitor/etc/enable-simulation
/opt/vehicle-monitor/vmctl start
/opt/vehicle-monitor/vmctl status
/opt/vehicle-monitor/vmctl stop
```

安装前校验全部SHA256；应用运行时拒绝更新。安装不覆盖已有配置、运行参数及数据；以releases/保存版本，current/previous指向当前/上一版本。应用根目录不能为/。首次默认参数为显式--simulate、时长86400秒，不启用真实传感器或显示。

`etc/arguments`每行一个参数，**不是shell脚本，不执行eval或环境变量替换**。例如增加两行`--gui-headless`、`--can-simulate`；TCP则增加`--tcp`及下一行数值IPv4:端口。路径含空格可作为一整行。配置、日志介质与指标路径由控制脚本追加。

日志模拟介质为var/events.nor，业务输出var/application.log，监督日志var/supervisor.log；每次运行独立的var/metrics-*.jsonl。指标按秒追加，CPU为累计进程秒、RSS为当前驻留KB、温度为thermal_zone0或null（并记录来源）、队列深度与丢弃、故障数量、网络及健康状态。指标/控制目录需要可写，不无限承诺磁盘容量；24小时约数十MB，实际大小以台架测量为准，长期运行应配置外部日志轮转/归档。

## 监督与停止

`vmctl supervise`启动子进程，异常退出最多尝试3次，间隔1/2/3秒，之后停止；正常退出不重启。指标超过10秒未更新时先TERM、等待2秒再KILL。手动stop先TERM，最多给子进程约5秒清理，再强制结束，控制命令最多等待8秒。

应用内部监督线程检查采集与诊断进度（500ms）、TCP/CAN/GUI/存储活动进度（2秒）。等待新日志/配置工作属于正常空闲；错误、死进度和连接掉线分别处理，连接掉线本身不意味着线程卡死。异常触发非零退出，不由独立线程无条件“喂狗”。指标中的watchdog_eligible_ticks只表示健康门控通过次数，**未打开真实/dev/watchdog，也不代表实际喂狗**。

若内核I/O永久阻塞，线程join/同步写也可能不能完成；进程外监督通过指标超时处理，不能承诺用户态从D状态强制恢复。真实硬件watchdog的设备、nowayout、超时与复位行为仍需实板确认。

## 更新与恢复

先stop，再安装另一个已校验软件包。回滚：

```sh
/opt/vehicle-monitor/vmctl stop
/opt/vehicle-monitor/vmctl rollback
/opt/vehicle-monitor/vmctl start
```

回滚只交换应用版本，保留配置、文件模拟NOR和运行记录；不是系统镜像OTA或跨格式迁移。当前日志格式v1兼容本阶段更新。部署切换通过ln -sfn完成，停止态执行；尚未做切换中随机断电测试。介质格式头损坏时保留原文件分析，禁止为恢复运行而自动擦除。

## 开机启动模板（未自动安装）

先用scripts/board_probe.sh确认PID1：

- systemd：deploy/init/vehicle-monitor.service，默认/opt路径，ConditionPathExists要求显式仿真开关；systemd停止整个控制组。
- BusyBox/SysV：deploy/init/S99vehicle-monitor，调用同一控制器。

只安装与实际init匹配的一种模板，再在目标板验证开机启动、停止及重启；本阶段未修改本机或开发板的启动项。

板端脚本需要POSIX sh、Linux /proc、date、stat -c、readlink、ln -sfn、sha256sum、nohup、cp/mv等常见BusyBox/GNU工具。Python打包工具和测试仅要求开发主机Python3，不要求板端安装Python。ARM64当前GUI只有离屏后端，实际SPI/DRM/fbdev与触摸仍待适配。
