#!/bin/sh
# Read-only discovery. Does not scan I2C addresses or touch flash/GPIO/watchdog.
set -u
printf '=== kernel ===\n'
uname -a
printf '=== OS ===\n'
cat /etc/os-release 2>/dev/null || true
printf '=== board ===\n'
if [ -r /proc/device-tree/model ]; then tr '\000' '\n' </proc/device-tree/model; fi
printf '=== init ===\n'
readlink /proc/1/exe || true
printf '=== device interfaces ===\n'
for pattern in '/dev/i2c-*' '/dev/gpiochip*' '/dev/mtd*' '/dev/fb*' '/dev/dri/*' '/dev/input/event*' '/dev/watchdog*'; do
 ls -l $pattern 2>/dev/null || true
done
printf '=== IIO devices ===\n'
for dev in /sys/bus/iio/devices/iio:device*; do
 [ -d "$dev" ] || continue
 printf '%s: ' "$dev"
 cat "$dev/name" 2>/dev/null || true
 ls "$dev" 2>/dev/null || true
done
printf '=== MTD partition table ===\n'
cat /proc/mtd 2>/dev/null || true
printf '=== network ===\n'
ip -details link show 2>/dev/null || true
printf '=== libc ===\n'
ldd --version 2>&1 | head -n 2 || true
printf '=== I2C devices already bound (no address scan) ===\n'
for dev in /sys/bus/i2c/devices/*-*; do
 [ -d "$dev" ] || continue
 [ -r "$dev/name" ] || continue
 printf '%s: ' "$dev"
 cat "$dev/name"
 readlink "$dev/driver" 2>/dev/null || true
done
printf '=== IIO scale and driver (no ADC sample request) ===\n'
for dev in /sys/bus/iio/devices/iio:device*; do
 [ -d "$dev" ] || continue
 printf '%s\n' "$dev"
 readlink "$dev/device/driver" 2>/dev/null || true
 for attr in "$dev"/*scale; do
  [ -r "$attr" ] || continue
  printf '%s: ' "$attr"
  cat "$attr" 2>/dev/null || true
 done
done
printf '=== DRM connector status and modes (no modeset) ===\n'
for connector in /sys/class/drm/card*-*; do
 [ -r "$connector/status" ] || continue
 printf '%s: ' "$connector"
 cat "$connector/status"
 cat "$connector/modes" 2>/dev/null || true
done
printf '=== Input names and capabilities ===\n'
cat /proc/bus/input/devices 2>/dev/null || true
printf '=== Watchdog metadata (device NOT opened) ===\n'
for dev in /sys/class/watchdog/watchdog*; do
 [ -d "$dev" ] || continue
 for attr in identity timeout nowayout state; do
  [ -r "$dev/$attr" ] || continue
  printf '%s/%s: ' "$dev" "$attr"
  cat "$dev/$attr" 2>/dev/null || true
 done
done
printf '=== Running kernel feature config (if exposed) ===\n'
if [ -r /proc/config.gz ] && command -v zcat >/dev/null 2>&1; then
 zcat /proc/config.gz | grep -E '^(# )?CONFIG_(CAN|CAN_RAW|CAN_ROCKCHIP|DHT11|INV_MPU6050_I2C|INV_MPU6050_SPI|MTD_SPI_NOR|DRM_ROCKCHIP|FB|IIO|GPIO_CDEV|WATCHDOG|DW_WATCHDOG)(=| is)' || true
else
 printf 'Running kernel config unavailable; host SDK .config is not runtime evidence.\n'
fi
printf '=== CAN device-tree candidates (no changes) ===\n'
if [ -d /sys/firmware/devicetree/base ]; then
 find /sys/firmware/devicetree/base -type d -name '*can@*' 2>/dev/null | while IFS= read -r node; do
  printf '%s\n' "$node"
  for attr in status compatible; do
   if [ -r "$node/$attr" ]; then
    printf '%s: ' "$attr"
    tr '\000' '\n' < "$node/$attr"
   elif [ "$attr" = status ]; then
    printf 'status: absent (device-tree default enabled; does not prove driver binding)\n'
   fi
  done
 done
fi
