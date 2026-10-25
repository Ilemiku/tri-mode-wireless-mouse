# 三模鼠标

基于 STM32F103ZE、STM32F103C8T6 和 ESP32-S3 的三模鼠标实现，支持 USB HID、私有 2.4GHz、BLE，以及本地触摸设置、MQTT 配置和 ESP32 OTA。

## 代码结构

```text
MouseProject/
├── Mouse/                   # STM32F103ZE 鼠标本体
│   ├── Core/                # 程序入口、FreeRTOS任务、LVGL界面与配置
│   ├── Dirver/              # 外设适配、传感器/射频/显示/触摸驱动
│   ├── Usb_Device/          # USB设备栈与HID描述符
│   ├── FreeRTOS/            # 内核与Cortex-M3端口
│   ├── ThirdParty/          # LVGL、字体及许可
│   ├── Project/             # Keil工程
│   └── Tools/               # 编译、字体生成及调试工具
├── mouse-2.4G/              # C8T6接收器：nRF24L01接收转USB HID
├── Esp32Mouse/              # ESP32-S3蓝牙桥与联网管理
│   ├── src/ble/             # BLE HID
│   ├── src/frame/           # 串口帧解析
│   ├── src/uart/            # 串口适配
│   ├── src/manage/          # Wi-Fi、MQTT与OTA
│   └── Tools/               # 配网与本机证书工具
├── MouseConsole/            # Node.js服务与浏览器控制台
│   ├── public/              # 页面、样式、交互
│   ├── test/                # 自动测试
│   └── tools/               # 设备查询与升级验证工具
└── TheMouse/                # 早期HAL过渡代码，非当前主工程
```

`Dirver`为历史目录名称，保留以兼容现有工程引用。

## 主要入口

| 模块 | 文件 | 职责 |
|---|---|---|
| 程序启动 | `Mouse/Core/Src/main.c` | 外设初始化与调度器启动 |
| 任务与模式 | `Mouse/Core/Src/app_rtos.c` | 输入采集、报告传输、后台服务与模式路由 |
| 本地界面 | `Mouse/Core/Src/app_ui.c` | LVGL、触摸与显示刷新 |
| 设置入口 | `Mouse/Dirver/Drv_dev/Src/esp32_link.c` | UART管理帧、设置请求与结果 |
| 蓝牙桥 | `Esp32Mouse/src/main.c` | 串口收帧与BLE报告转发 |
| 网络管理 | `Esp32Mouse/src/manage/mouse_manage.c` | Wi-Fi、MQTT及ZE命令关联 |
| 固件升级 | `Esp32Mouse/src/manage/mouse_ota.c` | HTTPS下载、应用槽与自检 |
| 电脑服务 | `MouseConsole/server.mjs` | 网页、MQTT Broker及升级文件服务 |
| 网页交互 | `MouseConsole/public/app.js` | 状态展示、配置与升级操作 |

## 构建

- ZE：Keil MDK AC5，打开 `Mouse/Project/Project.uvprojx`。
- C8T6：Keil MDK AC5，打开 `mouse-2.4G/MDK-ARM/mouse-2.4G.uvprojx`。
- ESP32-S3：PlatformIO、ESP-IDF；先在 `Esp32Mouse` 中准备本机OTA证书，再构建：

```text
python -m pip install -r Tools/requirements.txt
python Tools/setup_ota_tls.py --host YOUR_PC_LAN_IP
pio run -e esp32s3
```

控制台在 `MouseConsole` 目录运行：

```text
npm ci
npm test
npm start
```

本机网页默认 `http://127.0.0.1:8088`。网络凭据、令牌、证书私钥和构建产物不纳入版本控制。管理服务面向可信局域网，不直接部署到公网。

项目自有C代码使用4空格和独立大括号，网页使用2空格。第三方组件保留各自的来源说明与许可证。
