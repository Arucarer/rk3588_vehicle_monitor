# 构建与版本配置

## 职责

根 `CMakeLists.txt`组织 `vehicle_core`、可选GUI、应用、工具及CTest；本目录提供ARM64工具链与版本头模板。

## 关键文件

- `aarch64-linux.cmake`：从 `RK_CROSS_PREFIX`选择厂商Linux编译器，按需使用 `RK_SYSROOT`，隔离主机库。
- `version.hpp.in`：生成软件版本、源码指纹及Git基线；构建目录另生成 `version.json`。

## 使用与理解

主机执行 `./scripts/check.sh`，板端产物执行 `./scripts/build_rk.sh`。可用 `-DVEHICLE_BUILD_GUI=OFF`关闭GUI；交叉编译不运行目标板测试。普通构建使用仓库内固定LVGL，无需在线下载。

源码指纹用于识别构建输入，产物SHA256用于校验交付文件，Git基线用于追溯提交，三者含义不同。下一步看 [部署](../deploy/README.md)。
