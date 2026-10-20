# SDK与交叉编译验证 · 2026-10-20

## 用户提供的板端基线（待在线复核）

ATK-DLRK3588B / MYBOARD-RK3588 / Buildroot / Linux5.10.160。
主机192.168.88.100，板端192.168.88.101；HDMI已验证2560×1440@60Hz。
IMX415使用/dev/video22、4K NV12，用于现有视觉项目，本项目不默认占用摄像头或显示设备。
其他任务书传感器尚未确认采购与接线。HDMI仅作为开发显示，未批准替代240×320 SPI触摸屏。

## 本机实际核对

SDK：/home/aaa/rk3588_linux_sdk。
manifest引用atk-rk3588_linux5.10_release.xml。
Rockchip配置device/rockchip/.chips/rk3588/rk3588_myboard_defconfig指定：
RK_BUILDROOT_BASE_CFG=atk_dlrk3588、RK_KERNEL_DTS_NAME=rk3588-myboard、RK_ROOTFS_HOSTNAME=MYBOARD-RK3588。
这里的rk3588_myboard_defconfig是Rockchip顶层/内核/U-Boot配置名，实际Buildroot输出目录为rockchip_atk_dlrk3588。

内核Git HEAD：4e85133438866491ef8cbd2917ce8e4af0188f53。
Buildroot Git HEAD：29a41f894e9ee74c12cc69112a3ddebcab9bdc1a。
HEAD只标识基线，未证明工作树无修改或对应板端已烧写镜像。

工具链：buildroot/output/rockchip_atk_dlrk3588/host/bin/aarch64-buildroot-linux-gnu-g++，GCC10.4.0。
sysroot：该host目录下aarch64-buildroot-linux-gnu/sysroot（工具链自动使用）。
执行./scripts/build_rk.sh可复现。交叉构建已通过。
产物build-rk/vehicle-monitor：AArch64 Linux PIE，解释器/lib/ld-linux-aarch64.so.1。
依赖libstdc++.so.6、libm.so.6、libgcc_s.so.1、libc.so.6与动态加载器，仍需检查板端版本。

## 当前内核.config适配缺口

已启用GPIO_CDEV、IIO、MTD_SPI_NOR、WATCHDOG、DRM、FB、CAN及CAN_RAW。
CONFIG_DHT11、CONFIG_INV_MPU6050_I2C、CONFIG_INV_MPU6050_SPI、CONFIG_CAN_ROCKCHIP未启用。
这意味着需要继续核查厂商替代驱动、设备树和真实设备绑定；不能直接认定指定传感器与CAN控制器已可用。
这里只核对本机kernel/.config，不代表板端运行配置。未修改SDK或生成未经验证的烧写补丁。

## 板端连接结果

执行BatchMode、StrictHostKeyChecking=yes的root@192.168.88.101 SSH连接，返回No route to host。
本机ens33已具有192.168.88.100/24，但尚无法访问板端。未进入认证阶段，登录用户名亦待核对。
未传输文件、未执行板端探测、未改网络或启动现有应用。

网络恢复后先执行只读scripts/board_probe.sh，再部署到独立临时目录执行--version及显式仿真，之后逐模块实板适配。
