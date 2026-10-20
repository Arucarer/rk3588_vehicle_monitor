# 公共头文件

## 职责

向核心实现、界面和测试提供统一接口，项目类型集中在 `vehicle`命名空间。

## 阅读入口

[vehicle接口索引](vehicle/README.md)按数据、算法、业务和运行服务说明每个头文件。建议先看 `Measurement`、`Snapshot`和 `StateStore`，再沿 [运行主线](../src/README.md)查看实现。

生成的版本头位于构建目录 `generated/vehicle/version.hpp`，由CMake创建，不手工维护。
