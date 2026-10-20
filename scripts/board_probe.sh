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
