# 项目记忆日志 - hello_world

## 会话指纹
- **项目ID**: hello_world-esp32s3
- **最后会话**: 2026-05-16 WSL 环境恢复与 CLAUDE.md 更新
- **会话链**: S1 环境搭建 → WSL 迁移 → ESP-IDF 安装 → 编译验证

## 快速恢复信息
```
恢复命令: /em rec hello_world
最后活跃: 2026-05-16
```

## 关键决策
- [2026-05-07] 采用 WSL2 + ESP-IDF v5.5.1 作为开发环境，替代 Windows 原生编译
- [2026-05-07] 使用 Gitee 镜像 + ghproxy 下载 ESP-IDF，因 GitHub 直连不可用
- [2026-05-07] 最终从 Windows 已安装的 ESP-IDF 复制到 WSL，避免子模块下载耗时

## 会话历史

### S1 环境搭建与编译验证 (2026-05-07)
- **主要内容**: ESP-IDF 环境搭建、工具链修复、首次编译
- **产出**: hello_world.bin / .elf 编译成功，目标 esp32s3

### 2026-05-16 CLAUDE.md 更新与状态恢复
- **主要内容**: 恢复 WSL 上下文，清理 CLAUDE.md 乱码，追加 Karpathy 行为指南
- **产出**: CLAUDE.md 恢复干净状态，评估注入 Karpathy 行为指南
