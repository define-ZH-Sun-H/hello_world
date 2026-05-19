# 项目规格单

## Meta
- **创建日期**: 2026-05-07
- **项目名称**: hello_world
- **芯片型号**: ESP32-S3 (Espressif)
- **架构**: Xtensa LX7
- **构建系统**: ESP-IDF v5.5.1 (GCC)
- **语言**: C
- **当前步骤**: S1
- **整体状态**: ✅ 编译通过

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

## 开发步骤状态

| 步骤 | 名称 | 状态 | 日期 |
|------|------|------|------|
| S1 | 环境搭建与编译验证 | ✅ 完成 | 2026-05-07 |
| S2 | TBD | ⏳ 待定 | — |

## 已初始化外设
- 无（基础工程，仅 printf 输出）

## 驱动层
- 无

## 应用层
- 主循环：打印芯片信息 → 倒计时重启 → esp_restart()

## 调试接口
- printf 重定向（通过 UART）
- 串口 monitor: `idf.py -p <PORT> monitor`

## 问题追踪

### 已解决问题
1. **工具链版本不匹配** — tools.json 指定 `esp-14.2.0_20260121` 但实际下载版本为 `esp-14.2.0_20241119`。已修正 tools.json。
2. **Windows VS Code 无法编译** — build/ 目录由 WSL 生成含 Linux 路径，Windows 工具链不兼容。解决方案：使用 WSL Remote 开发。

### 待解决问题
（无）

## 参考文档
- ESP32-S3 数据手册: https://www.espressif.com/en/products/socs/esp32-s3
- ESP-IDF 编程指南: https://docs.espressif.com/projects/esp-idf/
