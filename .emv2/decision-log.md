# 关键决策记录

## 2026-05-07 - 采用 WSL2 + ESP-IDF 作为开发环境

### 决策内容
放弃 Windows 原生 ESP-IDF 开发环境，改用 WSL2 Ubuntu + ESP-IDF v5.5.1。

### 理由
1. ESP-IDF 官方推荐 Linux 环境，工具链兼容性更好
2. 避免 Windows 路径转换（msys2）导致的各类问题
3. WSL2 性能接近原生 Linux

### 影响
- 需要 VS Code Remote-WSL 扩展进行开发
- 构建产物与 Windows 不交叉兼容
- 串口烧录需要将 USB 设备透传到 WSL（或通过 Windows 侧烧录）

### 参考
- ESP-IDF 文档推荐 Linux/macOS 作为主要开发环境

## 2026-05-27 - 纳入 FreeRTOS 物联网学习方案 (plan.md)

### 决策内容
将 `plan.md`（FreeRTOS 物联网学习方案，共 8 个阶段）纳入 EM 体系，映射为 S1-S8 步骤。

### 映射关系
| EM 步骤 | Plan Phase | 名称 |
|---------|-----------|------|
| S1 | Phase 1 | 环境搭建与第一个 RTOS 程序 |
| S2 | Phase 2 | 任务间通信 — 队列与信号量 |
| S3 | Phase 3 | 高级 IPC 与事件驱动 |
| S4 | Phase 4 | 文件系统与数据记录 |
| S5 | Phase 5 | WiFi + MQTT 物联网通信 |
| S6 | Phase 6 | BLE 通信 (NimBLE) |
| S7 | Phase 7 | 综合物联网项目 |
| S8 | Phase 8 | 最终验证 |

### 理由
1. plan.md 是完整的学习路线图，涵盖从基础到综合项目的全部阶段
2. 现有代码（事件组、RGB、队列）已覆盖 Phase 3 大部分内容，避免重复开发
3. 需要统一的步骤追踪体系管理后续 6 个阶段

### 影响
- S3 标记为"部分完成"：事件组代码已实现，但任务通知、继电器定时器、蜂鸣器 PWM 待补充
- S4 为当前工作阶段：文件系统（SPIFFS）+ SD 卡 + OV2640 摄像头
- 后续步骤依赖前置步骤的模块和知识基础

## 2026-05-07 - 从 Windows 复制 ESP-IDF 到 WSL

### 决策内容
放弃通过 git clone + 子模块下载的方式安装 ESP-IDF，改为从 Windows 已安装的副本直接复制到 WSL。

### 理由
1. GitHub 直连不可用，Gitee 镜像 + ghproxy 子模块下载极慢且不稳定
2. Windows 已存在完整可用的 ESP-IDF v5.5.1 安装

### 影响
- 需要修复 CRLF 行尾问题（`sed -i 's/\r$//'`）
- install.sh 需要重新生成 Python venv 和工具链配置
