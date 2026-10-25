# 鼠标界面中文字库

来源：Google Fonts 的 Noto Sans SC（思源黑体系列），由 Google Fonts text 子集接口提供字形。

许可：SIL Open Font License 1.1，见同目录 OFL.txt。版权声明与许可保留，生成的衍生字库命名为 ui_font_16 / ui_font_20。

MouseUi-subset.woff2 的SHA256：8453cd750098108f89244e7098f35a1645896ec354cad91e558b93c6df31165c。

本地生成工具：lv_font_conv 1.5.3。仅包含当前界面字符与ASCII，避免把整套中文字库放进STM32 Flash。

文字原文：Tools/ui-texts.json。执行Tools/build-ui-fonts.ps1生成两个C字库及UTF-8字节串头文件。新增字符超出现有子集时工具会拒绝，需要先更新子集源字体。
