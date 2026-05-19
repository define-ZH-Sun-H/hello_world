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

## 2026-05-07 - 从 Windows 复制 ESP-IDF 到 WSL

### 决策内容
放弃通过 git clone + 子模块下载的方式安装 ESP-IDF，改为从 Windows 已安装的副本直接复制到 WSL。

### 理由
1. GitHub 直连不可用，Gitee 镜像 + ghproxy 子模块下载极慢且不稳定
2. Windows 已存在完整可用的 ESP-IDF v5.5.1 安装

### 影响
- 需要修复 CRLF 行尾问题（`sed -i 's/\r$//'`）
- install.sh 需要重新生成 Python venv 和工具链配置
