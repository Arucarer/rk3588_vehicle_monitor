# RK3588 车载智能监测与设备健康管理系统

## 项目简介

基于正点原子 ATK-DLRK3588B 的车载监测软件原型，围绕车速、姿态、环境及设备状态，实现采集处理、故障诊断、告警、日志、可视化和网络上报。以项目技术任务书为需求基线，采用软件先行、实板逐项验证的方式推进。

## 已完成阶段

| 阶段 | 完成内容 |
|---|---|
| 1 工程基础 | 配置、状态快照、有界队列、构建与线程管理 |
| 2 采集与诊断 | 速度/姿态算法、标定滤波、防抖、多故障状态机与声光策略 |
| 3 日志存储 | 文件模拟NOR、CRC、循环覆盖、恢复与导出 |
| 4 CAN与TCP | 节点管理、心跳、状态上报、参数查询与受控修改 |
| 5 LVGL界面 | 仪表、设备健康、节点、告警历史、参数五页面 |
| 6 软件集成与部署 | 健康监督、故障注入、指标、打包与应用恢复 |

13项主机测试和ARM64交叉编译通过，实板已验证模拟运行、离屏GUI、文件日志及60秒以太网TCP通信。真实外设及长时验收按[实板清单](docs/hardware/next-steps.md)推进；阶段证据见[阶段计划](docs/roadmap.md)。

## 技术栈

- **平台与语言**：RK3588 / ARM64、Buildroot Linux、C/C++17、POSIX线程。
- **界面与通信**：LVGL 8.2.0、SDL2（主机可选）、SocketCAN、TCP socket。
- **构建与工具**：CMake、厂商AArch64工具链、CTest、Python 3、Shell、Git。
- **数据与可靠性**：状态快照、有界队列、CRC32、循环日志、线程进度监督。

## 项目框架

```text
include/vehicle/  公共类型与模块接口
src/             采集算法、诊断、日志、CAN/TCP与运行管理
gui/             LVGL页面、字体与显示服务
platform/linux/  Linux硬件适配约定
config/          业务参数
protocols/       TCP与CAN协议
cmake/           交叉编译与版本模板
scripts/         构建、探测与打包
deploy/          安装、启动、监督与恢复
tools/           上位机、信号注入、日志导出与探测工具
tests/           单元与集成测试
docs/            架构、阶段报告、硬件与验收资料
third_party/     固定版本依赖
```

建议从[项目阅读路线](docs/README.md)开始，再看[运行主线](src/README.md)和各目录README。主体数据流为：**采集 → 状态快照 → 诊断 → 告警/日志/界面**，CAN与TCP状态也进入诊断，网络独立读取快照上报。

## 本机运行

```sh
./scripts/check.sh
./build/vehicle-monitor --simulate --duration 5 --config config/vehicle.conf
./build/vehicle-monitor --version
```

Ctrl+C/SIGTERM 可优雅退出。演示产生交替60/90 km/h的明确模拟数据，新增合成姿态、母线与液位异常场景；温湿度保持无效。不指定日志文件时只输出stdout；使用 `--log-file` 可启用文件模拟NOR循环日志，不代表已写入Flash。关键与普通队列分别有界，关键溢出或写失败使程序非零退出。详见[日志运行与导出](docs/storage.md)。

## 集成测试与交付

```sh
# 指标路径必须为新文件
./build/vehicle-monitor --simulate --can-simulate --gui-headless --duration 60 --metrics-file /tmp/vehicle-metrics.jsonl
# 可扩展至1800秒或86400秒；当前实际只完成60秒主机组合测试
python3 tools/soak_test.py --duration 60 --output out/my-soak
python3 scripts/package.py --build-dir build-rk --output-dir out --label software
```

[部署与应用回滚](deploy/README.md)包含校验安装、显式仿真开关、启动/停止、故障重启以及两种init模板；工具不会自动修改本机或板端启动项。

## 图形界面

```sh
# 有SDL2和桌面的主机
./build/vehicle-monitor --simulate --can-simulate --duration 60 --gui
# 无桌面或ARM64构建可用离屏渲染
./build/vehicle-monitor --simulate --duration 5 --gui-headless --gui-capture /tmp/vehicle-ui.ppm
```

![五页界面预览](docs/previews/gui-pages.png)

截图使用合成数据。页面交互、中文字体、历史窗口、配置保存及后端限制见[GUI说明](gui/README.md)。

## 通信演示

```sh
# 终端1
python3 tools/monitor_server.py --port 9000 --duration 60
# 终端2
./build/vehicle-monitor --simulate --can-simulate --duration 30 --tcp 127.0.0.1:9000
```

远程参数默认只读；显式开启写入和SocketCAN测试方法见[通信说明](docs/communication.md)。CAN节点与TCP在线状态进入诊断；不缓存离线网络告警。

## 交叉编译

使用厂商SDK配套的 AArch64 Linux 工具链，不能使用旧工程的 arm-none-eabi-gcc。

```sh
export RK_CROSS_PREFIX=/实际SDK路径/bin/aarch64-linux-gnu-
# SDK要求显式sysroot时设置 RK_SYSROOT；勿填写其他SDK的sysroot
cmake -S . -B build-rk -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-linux.cmake -DBUILD_TESTING=OFF
cmake --build build-rk -j2
```

已使用本机厂商SDK完成交叉编译与板端模拟运行，可执行 `./scripts/build_rk.sh`。详见 [SDK验证记录](docs/reports/sdk-validation.md)。部署说明见 [deploy](deploy/README.md)。

## Git 迁移

远程仓库：https://github.com/Arucarer/rk3588_vehicle_monitor

```sh
git clone https://github.com/Arucarer/rk3588_vehicle_monitor.git
cd rk3588_vehicle_monitor
./scripts/check.sh
```

`.gitignore`排除构建产物、IDE本地配置、重复RAR和约249MB的STM32参考目录`moni_traffic/`。原始任务书与背景文档纳入版本管理；本地历史参考资料保持原位，新Linux工程构建不依赖它们。
