# 固定依赖

LVGL v8.2.0：commit `0019fc541f759b3323add63034502b0248afc58f`。
来源：https://github.com/lvgl/lvgl/releases/tag/v8.2.0
归档：https://codeload.github.com/lvgl/lvgl/tar.gz/0019fc541f759b3323add63034502b0248afc58f
下载归档SHA256：8bb0f3cae8870cb76889f32ebc44e4e7641234f4affc283b694f30f5defa9e06。
仅收录src/、lvgl.h、lv_conf_template.h与LICENCE.txt，保留原文件内容，构建不联网。MIT许可见lvgl/LICENCE.txt。
中文字体为Noto Sans CJK派生的裁剪字体Vehicle UI 16，SIL OFL 1.1许可见../gui/fonts/NOTO-COPYRIGHT.txt。已生成字体源码纳入仓库，普通构建不需要字体源文件或Pillow。

## 与项目的关系

根CMake将LVGL源码编译为 `vehicle_lvgl`，由 `vehicle_gui`使用；应用页面位于 [gui](../gui/README.md)，不直接修改第三方控件实现。升级依赖时同步核对版本、许可、字体和界面测试，保持源码与任务书要求一致。
