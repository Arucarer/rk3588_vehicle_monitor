# 板载SH3001只读事件探测

当前已确认厂商驱动绑定I2C4的SH3001，input名称gsensor/gyro（本次event1/event3）。它不是任务书MPU6050，不默认替代器件。

已提供vehicle-input-probe：仅以O_RDONLY打开evdev，查询设备名称并poll/read；不执行EVIOCGRAB、不写sysfs、不启用传感器、不改量程、不访问I2C寄存器/看门狗。输出为未标定原始整数，不作g或deg/s换算。尚未在板端运行该工具。

Ubuntu项目目录构建并上传：

```sh
cmake --build build-rk --target vehicle-input-probe -j2
scp build-rk/vehicle-input-probe root@192.168.88.101:/tmp/vehicle-monitor-test/
```

开发板终端运行：

```sh
cd /tmp/vehicle-monitor-test
chmod +x vehicle-input-probe
./vehicle-input-probe 10 /dev/input/event1 /dev/input/event3
```

每路仅打印前10个数据帧，结尾给出总事件/帧数和SYN_DROPPED计数。尚未观察到的轴显示?，不补成0。发生丢事件时废弃轴有效状态，重新等待后续轴更新。

若reports=0或live_data=NO，仅说明本次没有取得可用轴事件；驱动绑定不等于传感器已开启上报，不能据此判定模块损坏。下一步核对厂商启用接口，再讨论显式开启与恢复原状态。事件节点号可能变化，运行前应与/proc/bus/input/devices核对，工具会打印实际名称。

本机已编译及完成非法参数/非evdev拒绝检查，ARM64交叉编译通过。原始轴比例、方向矩阵、时间同步和真正姿态精度仍待验证。
