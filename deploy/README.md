# 部署边界
当前为显式仿真应用，尚未提供生产自启动服务，避免把模拟数据作为真实监测结果。

交叉编译通过后，先用 `file build-rk/vehicle-monitor`、SDK 的 readelf 检查架构、解释器和依赖；传至板端临时目录执行 `--version`，再执行 `--simulate --duration 5 --config vehicle.conf`。这只验收程序 ABI 和软件骨架。

正式适配后安装至 /opt/vehicle-monitor，配置至 /etc/vehicle-monitor，运行诊断至 /var/log/vehicle-monitor；根据 board_probe 的 PID 1 结果选 BusyBox/SysV init 或 systemd。部署应保留上一版二进制、配置、SHA256 和恢复命令。尚未启用真实看门狗与自动重启。
