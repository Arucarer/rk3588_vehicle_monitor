# 业务配置

## 职责

`vehicle.conf`保存当前可配置的车轮参数、超速阈值、滞回和数据有效期。使用严格 `key=value` 格式，不支持行尾注释。

## 参数含义

| 参数 | 作用 |
|---|---|
| circumference_m / pulses_per_rev | 轮周长（m）与每圈脉冲数，用于速度换算 |
| overspeed_kmh / hysteresis_kmh | 超速阈值与恢复滞回（km/h） |
| stale_ms | 测量有效期（ms） |

## 从配置到运行

启动时 `Config` 校验 → `ParameterStore`发布配置 → 主循环应用新修订。GUI/TCP默认只读；显式允许写入后，经临时文件、同步和替换保存。范围及命令见 [协议](../protocols/README.md)。

模拟速度固定为场景值，修改周长不会改变60/90km/h演示。板卡针脚、ADC量程等另在 [硬件资料](../docs/hardware/README.md)冻结，不能从本文件推断接线。
