#!/bin/sh
set -eu
SDK_DIR=${RK_SDK_DIR:-"$HOME/rk3588_linux_sdk"}
RK_CROSS_PREFIX=${RK_CROSS_PREFIX:-"$SDK_DIR/buildroot/output/rockchip_atk_dlrk3588/host/bin/aarch64-buildroot-linux-gnu-"}
export RK_CROSS_PREFIX
[ -x "${RK_CROSS_PREFIX}g++" ] || { printf 'Compiler missing: %s\n' "${RK_CROSS_PREFIX}g++" >&2; exit 1; }
cmake -S . -B build-rk -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-linux.cmake -DBUILD_TESTING=OFF
cmake --build build-rk -j2
file build-rk/vehicle-monitor
sha256sum build-rk/vehicle-monitor
