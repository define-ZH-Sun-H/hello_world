# 项目记忆日志 - hello_world

## 会话指纹
- **项目ID**: hello_world-esp32s3
- **最后会话**: 2026-05-27 纳入 plan.md 总体规划到 EM 体系
- **会话链**: S1 环境搭建 → S2 外设驱动 → S3 事件组 → S4 文件系统与数据记录（当前）

## 快速恢复信息
```
恢复命令: /em rec hello_world
最后活跃: 2026-05-27
```

## 关键决策
- [2026-05-07] 采用 WSL2 + ESP-IDF v5.5.1 作为开发环境，替代 Windows 原生编译
- [2026-05-07] 使用 Gitee 镜像 + ghproxy 下载 ESP-IDF
- [2026-05-07] 从 Windows 已安装的 ESP-IDF 复制到 WSL，避免子模块下载耗时
- [2026-05-16] 项目初始化改为 3 阶段：硬件初始化 → 数据/队列创建 → 任务统一创建
- [2026-05-16] DS18B20 采用异步转换模式：start() 与 get_temperature() 分离，间隔 800ms
- [2026-05-16] DHT11 添加显式 1s 读取间隔
- [2026-05-16] OLED 显示采用 disp_queue + dirty 标志双驱动机制

## 关键发现
- DS18B20 转换耗时约 750ms（芯片内部转换，非 CPU 忙等）
- DHT11 读取最小间隔 1s（芯片规格限制）
- 1-Wire 和 DHT11 位级操作为硬延时（esp_rom_delay_us），但 DHT11 整体读取中有 vTaskDelay 让出 CPU
- key_task 优先级 6 高于 sensor_task 优先级 4（后调整为 sensor_task 10 > key_task 6）
- `gpio_install_isr_service` 必须在 `gpio_isr_handler_add` 之前调用

## 会话历史

### S1 环境搭建与编译验证 (2026-05-07)
- **主要内容**: ESP-IDF 环境搭建、工具链修复、首次编译
- **产出**: hello_world.bin / .elf 编译成功，目标 esp32s3

### S2 外设驱动与传感器采集 (2026-05-16)
- **主要内容**: 重构初始化流程、添加 DS18B20/DHT11 传感器驱动、OLED显示、按键扫描、RGB LED
- **产出**: 完整的 3 任务系统（key/sensor/display），build 通过
- **待办**: 烧录到开发板验证

### 存量接入 (2026-05-26)
- **主要内容**: /em si 更新项目状态文件
- **产出**: project-spec.md / memory-log.md 更新

### S3 讨论: 按键事件组 + 组合键 (2026-05-26)
- **主要内容**: /em new + /em disc 流程
- **功能**: 创建 FreeRTOS Event Group 跟踪 4 键状态；KEY1+KEY2 同时按下 → rgb_clear()
- **产出**: discussion/2026-05-26-key-event-group/ 含 split/requirements/milestones
- **待实现**: S3-A 到 S3-E 五个子步骤

### plan.md 纳入 EM 体系 (2026-05-27)
- **主要内容**: 将 `plan.md`（FreeRTOS 物联网学习方案，8 阶段）纳入 EM 体系
- **映射**: plan Phase 1-8 → EM S1-S8
- **当前步骤**: S4（文件系统与数据记录 = Phase 4）
- **决策**: decision-log.md 记录完整映射关系
- **S3 状态**: 标记为"部分完成"（事件组已实现，任务通知/继电器/蜂鸣器待补充）
