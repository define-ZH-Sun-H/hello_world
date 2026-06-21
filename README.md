# ESP32 智能球罩项目

> 基于 ESP32-S3 的智能物联网终端，集成触控彩屏、传感器采集、OLED 显示和摄像头功能。

| 支持目标 | ESP32 | ESP32-S3 |
|---------|-------|----------|
| **已验证** | - | ✅ |

---

## 项目概述

本项目是一个多功能 ESP32-S3 固件，作为智能球罩设备的核心控制器。主要功能包括：

- **触控彩屏**：ST7789 240×320 TFT + XPT2046 触摸，LVGL GUI 框架
- **环境监测**：温湿度（DHT11）、温度（DS18B20）传感器数据采集
- **OLED 显示**：0.96 寸 OLED 屏幕，显示传感器数据、系统状态、菜单界面
- **摄像头**：OV2640 摄像头模组（ESP32-S3）
- **按键交互**：多功能按键驱动，支持菜单导航与控制
- **RGB LED**：可编程全彩指示灯（WS2812）
- **SD 卡**：数据记录和文件存储
- **app 框架**：组件化生命周期管理系统

---

## 项目结构

```
├── CMakeLists.txt                  # 顶层 CMake 构建配置
├── main/
│   ├── CMakeLists.txt
│   ├── idf_component.yml           # ESP 注册表组件依赖
│   └── main.c                      # 主程序入口（仅入口，不驻留业务代码）
│
├── components/
│   ├── app/
│   │   ├── CMakeLists.txt
│   │   ├── app.c / .h              # 应用生命周期框架（init/run/stop）
│   │   ├── menu.c / .h             # OLED 菜单系统
│   │   └── sys_ctrl.c / .h         # 系统控制模块
│   │
│   └── bsp/                         # 板级支持包
│       ├── CMakeLists.txt
│       ├── bsp.h                   # BSP 统一头文件
│       │
│       ├── audio/                   # 音频驱动
│       │   ├── audio.c / .h
│       │
│       ├── camera/                  # OV2640 摄像头（需安装 esp32-camera）
│       │   ├── camera_test.c / .h
│       │
│       ├── debug/
│       │   └── debug.h             # 调试工具
│       │
│       ├── iic/
│       │   └── iic.c / .h          # I2C 驱动
│       │
│       ├── key/
│       │   └── key.c / .h          # GPIO 按键驱动
│       │
│       ├── led/
│       │   └── led.c / .h          # 普通 LED 控制
│       │
│       ├── oled/
│       │   ├── oled.c / .h         # SSD1306 OLED 底层驱动
│       │   ├── oled_display.c / .h  # OLED 显示管理层
│       │   ├── oled_image.h        # 图片数据
│       │   └── oledfont.h          # 字库
│       │
│       ├── rgb/
│       │   ├── rgb.c / .h          # RGB LED 控制（WS2812）
│       │   └── led_strip_encoder.c / .h
│       │
│       ├── sd/
│       │   └── sd.c / .h           # SD 卡驱动（SPI 模式）
│       │
│       ├── sensor/
│       │   ├── dht11.c / .h        # DHT11 温湿度传感器
│       │   ├── ds18b20.c / .h      # DS18B20 温度传感器
│       │   └── sensor_init.c / .h  # 传感器初始化与管理
│       │
│       └── touch_lcd/              # 触控彩屏（ST7789 + XPT2046 + LVGL）
│           ├── lvgl_app.c / .h     # LVGL 初始化（硬件 → LVGL 内核 → UI）
│           ├── lv_port_disp.c / .h # LVGL 显示移植（双缓冲 + DMA 同步）
│           ├── lv_port_indev.c / .h# LVGL 触摸输入移植（XPT2046 驱动）
│           └── lcd_test.c / .h     # LCD 独立测试（纯色/彩条/触摸坐标）
│
├── pytest_hello_world.py           # 自动化测试脚本
└── README.md                       # 本文件
```

---

## 快速开始

### 环境要求

- ESP-IDF v5.x（推荐 v5.3 或更新版本）
- ESP32-S3 开发板
- USB 数据线（用于烧录和串口监视）

### 编译

```bash
idf.py set-target esp32s3
idf.py build
```

### 烧录

```bash
idf.py -p PORT flash
```

将 `PORT` 替换为实际串口（Windows 下如 `COM3`，Linux 下如 `/dev/ttyACM0`）。

### 监视串口输出

```bash
idf.py -p PORT monitor
```

按 `Ctrl+]` 退出监视器。

---

## 主要功能模块

| 模块 | 位置 | 说明 |
|------|------|------|
| **触控彩屏** | `bsp/touch_lcd/` | ST7789 240×320 TFT + XPT2046 触摸，LVGL GUI 框架（双缓冲+DMA） |
| **LVGL 显示层** | `bsp/touch_lcd/lv_port_disp.c` | 双缓冲 + DMA 完成回调，避免显示撕裂 |
| **LVGL 触摸层** | `bsp/touch_lcd/lv_port_indev.c` | XPT2046 驱动封装为 LVGL indev，5 次采样平均 |
| **系统控制** | `app/sys_ctrl.c` | 按键事件轮询、系统状态分发、LED 指示 |
| **菜单系统** | `app/menu.c` | OLED 多级菜单导航、状态切换 |
| **OLED 显示** | `bsp/oled/oled_display.c` | 传感器数据展示、主页信息、页面刷新管理 |
| **按键驱动** | `bsp/key/key.c` | GPIO 扫描、事件组标志位、短按/长按检测 |
| **温湿度** | `bsp/sensor/dht11.c` | 单总线协议读取温湿度 |
| **温度** | `bsp/sensor/ds18b20.c` | OneWire 协议读取温度 |
| **RGB LED** | `bsp/rgb/rgb.c` | 全彩 LED 控制（WS2812） |
| **SD 卡** | `bsp/sd/sd.c` | SPI 模式 SD 卡读写 |
| **摄像头** | `bsp/camera/camera_test.c` | OV2640 初始化与拍照（需安装 esp32-camera） |
| **app 框架** | `app/app.c` | 生命周期回调、模块化启动/停止 |
| **音频** | `bsp/audio/audio.c` | 音频驱动 |

---

## 硬件引脚分配

> 具体引脚定义请参考 `components/bsp/bsp.h` 及 `main/main.c` 中的配置。

---

## 故障排除

### 烧录失败

- 检查串口连接：`idf.py -p PORT monitor` 看是否有输出
- 降低烧录波特率：在 `menuconfig` 中调整

### WiFi 连接失败

- 确认路由器 SSID/密码配置正确
- 检查天线连接

### OLED 无显示

- 确认 I2C 地址正确（默认 0x3C）
- 检查接线（SCL/SDA）

---

## 许可证

ESP-IDF 部分基于 Apache License 2.0。
项目自有代码保留所有权利。
