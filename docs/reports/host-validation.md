# 主机验证记录 · 2026-10-20

环境：Ubuntu x86_64 / GCC9.4 / CMake3.16.3；全部为软件主机测试。

执行：`./scripts/check.sh`。CTest包含core与simulation两项。
core覆盖速度公式/无效周期、阈值滞回与确认、数据过期、状态序号、队列满与排空关闭、CRC标准向量、TCP逐字节分片/合并/错误CRC/非法长度/大量噪声有界缓存。
simulation运行一秒并优雅结束。配置与无后端拒绝测试在主机补充执行。

不覆盖传感器时序、GPIO脉冲计数、实际屏幕、SocketCAN物理链路、TCP连接恢复、Flash擦写与断电、看门狗、P95延迟、24小时稳定性。后续交叉编译已通过，见sdk-validation.md；实板执行未执行。
