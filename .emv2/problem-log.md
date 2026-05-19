# 问题追踪

## 当前问题

（无）

---

## 历史归档

### [2026-05-07] 工具链版本不匹配
- **状态**: resolved
- **步骤**: S1
- **描述**: ESP-IDF tools.json 中 `xtensa-esp-elf` 和 `riscv32-esp-elf` 版本指定为 `esp-14.2.0_20260121`，但实际可下载版本为 `esp-14.2.0_20241119`
- **解决方案**: 修改 tools.json 版本名为 `esp-14.2.0_20241119` 以匹配实际下载版本

### [2026-05-07] Windows VS Code 编译失败
- **状态**: resolved
- **步骤**: S1
- **描述**: WSL 中编译生成的 build/ 目录包含 Linux 路径，Windows 原生 VS Code + ESP-IDF 插件无法识别
- **解决方案**: 改用 VS Code Remote-WSL 连接 WSL 进行开发编译
