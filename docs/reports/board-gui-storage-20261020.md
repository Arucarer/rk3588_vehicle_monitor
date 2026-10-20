# 板端离屏GUI与文件日志运行记录 · 2026-10-20

证据来源：用户提供的运行截图、文件列表和两条末尾指标。助手未直接登录开发板，尚未取得完整JSONL、NOR及PPM。

构建版本：0.2.0+c2bfce9ce6e0。请求命令为显式模拟采集、模拟CAN、duration=30、gui-headless、文件日志及退出截图。

## 已观察

- 运行截图有日志初始化`recovered=0 corrupt=0 incomplete=0`及模拟告警、恢复。
- 用户后续终端报告`退出码=0`；未提供程序最终停止汇总，且此前存在排队执行检查命令的情况，因此将该值记录为用户报告，不把它单独当作最终进程退出的独立证据。
- /tmp/vehicle-1792488417.jsonl约11KB、.nor约1.1MB、.ppm约226KB。证明文件已生成，不证明NOR内容已完整查询或PPM图像已检查。
- 两条指标elapsed_ms分别26052、27054：snapshots=1303、1353，healthy=true，unhealthy=[]，log_pending=0，critical_dropped=0，samples_dropped=0。
- 两条指标RSS均4232KB，thermal_zone0均33.307°C；cpu_seconds从2.62925增至2.73802。仅代表这两个采样点，不能据此证明内存长期稳定、全程最高温度或CPU峰值。
- active_faults=3/4符合仍在执行的模拟异常场景，不是物理车辆故障。
- network_connected=false：该命令未开启TCP客户端；hardware_watchdog_enabled=false：应用未访问真实看门狗，与板端现有watchdog active状态为不同概念。

## 当前结论

已有证据支持板端离屏GUI/文件日志/指标路径已运行并生成输出，末尾两个健康采样点正常。完整运行持续时间与全程健康情况需取得原始JSONL核对；文件日志恢复/CRC/序号及截图内容仍待检查。

## 下一步

将上述三个文件从开发板复制回Ubuntu项目out目录：分析完整JSONL，使用主机vehicle-log-export恢复文件模拟NOR（默认容量256）并检查损坏/未提交统计、记录序号；打开PPM验证实际渲染。

这次使用文件模拟NOR，不是W25Q128验收；离屏截图不是SPI屏/HDMI显示或触摸验收。尚未验证板端TCP上报、SH3001真实数据、CAN物理链路及硬件看门狗。

## 后续原始文件验证

用户已将三个原始文件复制到out/board-check-1792488417，助手已直接分析：28条指标全部healthy，时间范围0..27054ms，不能据此认定实际持续30秒。RSS为1684..4232KB（含启动过程），温度32.384..33.307°C；无关键/普通丢弃。

主机vehicle-log-export以容量256恢复文件介质：133条记录，序号1..133连续，corrupt=0、incomplete=0，含27条样本和106条告警变化。验证了这次已提交文件日志的长度/CRC/序号，不等于物理Flash断电验收。

PPM已打开检查：240×320，中文、车速、姿态和布局可见；温湿度明确为无效，界面标注SIM与网络未启用。[实际板端离屏画面](../previews/board-first-frame.png)及[校验摘要/文件SHA256](board-files-verified-20261020.json)。该图片是板端渲染回传，不是实屏照片。
