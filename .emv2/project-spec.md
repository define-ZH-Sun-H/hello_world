# 项目规格单

## Meta
- **创建日期**: 2026-05-07
- **最后更新**: 2026-05-26
- **项目名称**: hello_world
- **芯片型号**: ESP32-S3 (Espressif)
- **架构**: Xtensa LX7
- **构建系统**: ESP-IDF v5.5.1 (GCC)
- **语言**: C
- **当前步骤**: S4
- **整体状态**: 🔄 进行中 — 文件系统与数据记录 (Phase 4/8)
- **串口**: `/dev/ttyUSB0` / 460800

## 开发环境
- **平台**: WSL2 (Ubuntu) + VS Code Remote-WSL
- **IDF 路径**: `/root/esp/esp-idf`
- **工具链路径**: `/root/esp/.espressif/tools/`
- **Python 虚拟环境**: `/root/esp/.espressif/python_env/idf5.5_py3.14_env/`
- **构建脚本**: `/root/esp/build.sh`

## 硬件配置
- **芯片:** ESP32-S3
- **核心数:** 2 (Xtensa LX7)
- **WiFi:** 支持
- **BT/BLE:** 支持
- **Flash:** 2MB
- **开发板:** 普中科技 ESP32-S3 开发板

## 开发步骤状态

| 步骤 | 名称 | 对应 Phase | 状态 | 日期 |
|------|------|-----------|------|------|
| S1 | 环境搭建与第一个 RTOS 程序 | Phase 1 | ✅ 完成 | 2026-05-07 |
| S2 | 任务间通信 — 队列与信号量 | Phase 2 | ✅ 完成 | 2026-05-26 |
| S3 | 高级 IPC 与事件驱动 | Phase 3 | 🔄 部分完成 | 2026-05-26 |
| S4 | 文件系统与数据记录 | Phase 4 | 🔄 进行中 | — |
| S5 | WiFi + MQTT 物联网通信 | Phase 5 | ⏳ 待定 | — |
| S6 | BLE 通信 (NimBLE) | Phase 6 | ⏳ 待定 | — |
| S7 | 综合物联网项目 | Phase 7 | ⏳ 待定 | — |
| S8 | 最终验证 | Phase 8 | ⏳ 待定 | — |

## 已初始化外设

| 外设 | 接口 | GPIO | 状态 |
|------|------|------|------|
| UART (printf) | 内置 | — | ✅ |
| GPIO 按键 ×4 | GPIO输入+上拉 | 36, 37, 35, 0 | ✅ |
| LED | GPIO输出 | — | ✅ |
| I2C (OLED) | I2C_NUM_1 | — | ✅ |
| RMT (RGB WS2812) | RMT | 16 | ✅ |
| 1-Wire (DS18B20) | GPIO OD | 13 | ✅ |
| DHT11 | GPIO OD | 45 | ✅ |
| TWDT | 内置 | — | ✅ |

## 驱动层

| 驱动 | 文件 | 说明 |
|------|------|------|
| DS18B20 | `sensor/ds18b20.c` | 温度传感器，1-Wire协议，精确0.0625°C |
| DHT11 | `sensor/dht11.c` | 温湿度传感器，温精度1°C，湿度1%RH |
| OLED (SSD1306) | `oled/oled.c` | 0.96" OLED 128×64，I2C驱动 |
| OLED Display | `oled/oled_display.c` | 显示框架：状态栏+分隔线+三行内容 |
| Key | `key/key.c` | 4键独立扫描，防抖+长按+连击检测 |
| RGB (WS2812) | `rgb/rgb.c` | RMT驱动，支持RGB/HSV设置 |
| LED | `led/led.c` | 板载LED控制 |
| IIC | `iic/iic.c` | I2C底层读写封装 |
| Debug | `debug/debug.h` | 编译级日志控制(ERR/WARN/INFO/DEBUG) |

## 应用层

| 任务 | 周期 | 核心 | 优先级 | 说明 |
|------|------|------|--------|------|
| key_task | 10ms | core 0 | 6 | 按键扫描+事件处理 |
| sensor_task | 100ms | core 0 | 10 | DS18B20+DHT11 采集 |
| display_task | 50Hz | core 1 | 5 | OLED刷新 |

### 通信机制
- **disp_queue**: FreeRTOS 队列（长度5），用于事件驱动更新（传感器数据、按键）
- **g_disp.dirty**: 脏标志合并相同帧内的多次变更，减少I2C刷新

## 代码架构

```
hello_world/
├── main/main.c              — 3阶段初始化：硬件→数据队列→任务创建
├── components/bsp/
│   ├── bsp.h                — BSP总头文件
│   ├── debug/debug.h        — 调试日志系统
│   ├── key/key.c/.h         — 按键驱动
│   ├── led/led.c/.h         — LED驱动
│   ├── iic/iic.c/.h         — I2C驱动
│   ├── oled/                — OLED驱动+显示框架
│   ├── rgb/rgb.c/.h         — RGB LED驱动
│   └── sensor/              — DS18B20+DHT11+传感器采集
```

## 调试接口
- printf 重定向（通过 UART）
- 串口 monitor: `idf.py -p <PORT> monitor`
- 调试日志级别：当前 DBG_LEVEL_INFO

## 问题追踪

### 已解决问题
1. **工具链版本不匹配** — tools.json 版本修正 (2026-05-07)
2. **Windows VS Code 无法编译** — 改用 WSL Remote (2026-05-07)
3. **项目初始化流程重构** — 3阶段初始化为：硬件→数据/队列→任务创建 (2026-05-16)
4. **DHT11 读取间隔控制** — 添加显式1s间隔，避免频繁读取 (2026-05-16)
5. **DS18B20 冗余 start()** — 移除 get_temperature() 中的 ds18b20_start() (2026-05-16)

### 待解决问题
（无）

## 学习路线图
完整 8 阶段 FreeRTOS 物联网学习方案详见 [plan.md](../plan.md)。

### S4 目标 — 文件系统与数据记录
- **SPIFFS**: 分区表增加 storage 分区，挂载 SPIFFS，存储配置文件
- **SD 卡**: SDMMC/SDSPI 挂载，定时写入温湿度 CSV，按键触发拍照存 JPEG
- **OV2640 摄像头**: esp32-camera 组件，PSRAM 帧缓冲，拍照写入 SD
- **反模式**: 不频繁写小文件，PSRAM 分配大缓冲区，拍照后释放帧缓冲区

## 参考文档
- ESP32-S3 数据手册: https://www.espressif.com/en/products/socs/esp32-s3
- ESP-IDF 编程指南: https://docs.espressif.com/projects/esp-idf/
