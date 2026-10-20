# LVGL 8.2 车载界面

软件阶段已实现，画布固定240×320，五个页面：仪表、设备健康、CAN节点、告警历史、参数设置。

![页面预览](../docs/previews/gui-pages.png)

预览由真实LVGL渲染生成，数据为测试夹具，不是实屏照片。

## 运行

```sh
./scripts/check.sh
# 本机有SDL2和桌面时，鼠标点击/拖动，滚轮滚动，左右键切页，ESC或关闭窗口退出应用
./build/vehicle-monitor --simulate --can-simulate --duration 60 --gui
# 不需要桌面，运行真实LVGL离屏渲染并在退出时保存240×320 PPM
./build/vehicle-monitor --simulate --duration 5 --gui-headless --gui-capture /tmp/vehicle-ui.ppm
```

主机SDL窗口放大2倍，逻辑坐标仍为240×320；鼠标输入按2倍缩放换算。未编译SDL时`--gui`报错退出，可使用`--gui-headless`。板端交叉构建默认只有离屏后端，不依赖或占用现有HDMI/DRM设备；SPI屏/DRM/fbdev/evdev适配尚未实现。

## 页面与交互

- 仪表：车速、温湿度、俯仰、横滚；顶部显示模拟来源、TCP连接状态、故障数量及确认状态。无效/过期值明确显示“无效”，不会将断开表示为0。
- 健康：震动g、动态模拟量和数字量；内容可滚动。初始演示有母线电压bus和液位开关liquid。
- 节点：节点1/2在线状态、心跳序号及状态字节；距最后有效心跳>=3秒在界面判离线。未配置CAN显示未启用。
- 历史：两条一页，最新在前；“上一页/下一页”查阅，点击记录查看可滚动完整原文、UTC与时钟同步状态、版本及配置标识；详情右上角返回。列表为摘要，完整信息在详情。样本不占告警列表。
- 设置：点击选择一个参数，使用-/+编辑，再保存；字段有物理单位。控件限制单字段范围，跨字段约束在ParameterStore校验；后台保存成功/失败有提示。默认只读。

“确认告警”投递有界请求给诊断主线程，仅确认当前有效告警，故障保留，蜂鸣策略据确认状态静音。顶部变为“已确认”并保留故障数量；新状态/等级变化仍会撤销确认。

修改配置需显式传入独立配置文件与现有写入开关（此开关同时允许已启用TCP的远程写入）：

```sh
cp config/vehicle.conf /tmp/vehicle-ui.conf
./build/vehicle-monitor --simulate --duration 60 --gui --config /tmp/vehicle-ui.conf --allow-remote-set
```

保存通过独立8条有界队列及设置线程调用ParameterStore，不在LVGL线程或采集共享锁内执行磁盘I/O。确认队列容量8，满载会明确提示忙。保存成功表示参数存储已完成；诊断在下次循环应用，GUI新值从主线程快照回显。

## 数据、历史和线程

每个进程只创建一个GuiService/GuiWindow。全部LVGL、tick、flush、输入回调、控件与截图操作属于GUI线程；主线程仅发布GuiStateStore快照。快照目标10Hz渲染数据，LVGL定时器约5ms调度，实际刷新能力仍须实测。数据快照超过500ms显示“数据过期”。温湿度显示有效期5秒，速度/姿态与通道采用配置stale_ms。

未启用持久化日志时，界面保留最多128条本次运行故障变化并标记“内存”，退出即失去。启用`--log-file`时从LogService获取最近最多256条**已提交记录**（也受配置日志容量限制）；过滤样本后显示告警/诊断，标记“持久化”。不与Flash写入线程同时调用CircularLog，也不重新打开已锁定文件。GUI历史为最近窗口，完整介质历史需停机后用vehicle-log-export导出。

## 依赖、字体与构建

LVGL固定为v8.2.0，commit `0019fc541f759b3323add63034502b0248afc58f`，源码和MIT许可随仓库保存；版本编译时断言。来源及SHA256见[依赖清单](../third_party/README.md)。接口依据[LVGL 8.2显示移植说明](https://docs.lvgl.io/8.2/porting/display.html)。

中文字体Vehicle UI 16来自Noto Sans CJK，裁剪为项目字表，4bpp、22px行高；生成源码纳入Git，普通构建无需Pillow或系统字体。OFL许可见fonts/NOTO-COPYRIGHT.txt；重生成工具为tools/generate_ui_font.py（Pillow7.0.0与源字体SHA记录在生成文件）。增加中文文案需更新fonts/charset.txt并重生成，ASCII可直接使用。

CMake默认`VEHICLE_BUILD_GUI=ON`，主机自动检测SDL2。纯业务构建可用`-DVEHICLE_BUILD_GUI=OFF`。主机交互使用系统SDL2（本机2.0.10），ARM64交叉构建不隐式混入主机SDL库。

## 尚未验收

没有SPI屏控制器、触摸坐标/驱动、旋转、电平、DRM/fbdev flush的实板验证；无30分钟真实触摸测试、实屏10Hz测量或24小时可靠性结论。SDL/离屏验证不能替代任务书第6.9项实屏验收。

## 源码阅读

`include/vehicle/gui.hpp`定义GUI快照与交互回调；`service.cpp`管理GUI线程、设置队列和进度；`window.cpp`创建页面、渲染及SDL/离屏输入输出；`lv_conf.h`固定LVGL配置，`fonts/`保存裁剪字体与许可。主线程发布GuiStateStore，GUI读取显示，操作通过回调或队列回到业务。接着看 [核心运行主线](../src/README.md)。
