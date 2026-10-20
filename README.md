# RK3588 车载智能监测与设备健康管理系统

以《基于RK3588B的车载智能监测与设备健康管理系统技术任务书.txt》为需求基线，面向正点原子 ATK-DLRK3588B 的 Linux/C++17 工程。

当前交付：完成软件阶段1（工程基础）与阶段2（采集算法和多故障诊断），程序版本v0.1.0。详见[阶段计划](docs/roadmap.md)与[阶段2验证](docs/reports/stage2-validation.md)。实板驱动、LVGL、真实CAN/TCP收发、W25Q128持久化及完整系统验收尚未完成。仿真不能替代硬件验收。

## 本机运行

```sh
./scripts/check.sh
./build/vehicle-monitor --simulate --duration 5 --config config/vehicle.conf
./build/vehicle-monitor --version
```

Ctrl+C/SIGTERM 可优雅退出。演示产生交替60/90 km/h的明确模拟数据，新增合成姿态、母线与液位异常场景；温湿度保持无效。日志目前仅输出stdout，不代表已写入Flash。队列满时拒绝新事件并统计 dropped；正式关键事件保护和存储故障处理待实现。

## 交叉编译

使用厂商SDK配套的 AArch64 Linux 工具链，不能使用旧工程的 arm-none-eabi-gcc。

```sh
export RK_CROSS_PREFIX=/实际SDK路径/bin/aarch64-linux-gnu-
# SDK要求显式sysroot时设置 RK_SYSROOT；勿填写其他SDK的sysroot
cmake -S . -B build-rk -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-linux.cmake -DBUILD_TESTING=OFF
cmake --build build-rk -j2
```

已找到本机厂商SDK并完成交叉编译，可执行 `./scripts/build_rk.sh`。板端SSH当前返回No route to host，板端执行待验证。详见 [SDK验证记录](docs/reports/sdk-validation.md)。部署说明见 [deploy](deploy/README.md)。

## 工程入口

- `include/vehicle/`、`src/`：快照、队列、配置、诊断、协议及模拟运行管理。
- `platform/linux/`、`gui/`：设备访问合同与LVGL移植计划。
- `config/`、`cmake/`、`deploy/`：配置、工具链及部署边界。
- `protocols/`、`tests/`：协议草案及主机测试。
- `scripts/board_probe.sh`：板端只读环境探测。
- [可行性报告](docs/feasibility.md)、[架构与待办](docs/architecture.md)、[接口冻结表](docs/hardware/interfaces.csv)、[验收矩阵](docs/acceptance.csv)。

`moni_traffic/`为保留的STM32历史参考，根构建不使用它。旧文档中的供电、引脚、怠速与可靠性描述不得作为本项目已验证结论。旧树LVGL实际为8.3.11，与任务书8.2不一致。

## Git 迁移

远程仓库：https://github.com/Arucarer/rk3588_vehicle_monitor

```sh
git clone https://github.com/Arucarer/rk3588_vehicle_monitor.git
cd rk3588_vehicle_monitor
./scripts/check.sh
```

`.gitignore`排除构建产物、IDE本地配置、重复RAR和约249MB的STM32参考目录`moni_traffic/`。原始任务书与背景文档纳入版本管理；本地历史参考资料保持原位，新Linux工程构建不依赖它们。
