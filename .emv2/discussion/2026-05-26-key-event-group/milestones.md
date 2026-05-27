# 子流程拆分

## S3: 按键事件组 + 组合键
功能描述：为 4 个按键创建 FreeRTOS Event Group，并在 KeyInd_HandleEvents 中添加组合键检测。

### S3-A: 事件组基础设施
- **所属子系统**: key
- **开发内容**:
  1. 在 `key.h` 添加 `#include "freertos/event_groups.h"`、位掩码定义、`extern EventGroupHandle_t key_event_group`
  2. 在 `key.c` 定义 `EventGroupHandle_t key_event_group` 句柄
  3. 在 `key_init()` 中执行 `xEventGroupCreate()`（实际改为在 app_main Phase 2 中创建）
- **前置条件**: 无
- **验证方式**: 编译通过

### S3-B: 事件组同步 (KeyInd_HandleEvents 写入)
- **所属子系统**: key
- **开发内容**:
  1. 在 `KeyInd_HandleEvents()` 的 PRESS case 中：`xEventGroupSetBits(key_event_group, KEY_EVENT_BIT_x)`
  2. 在 `KeyInd_HandleEvents()` 的 RELEASE case 中：`xEventGroupClearBits(key_event_group, KEY_EVENT_BIT_x)`
- **前置条件**: S3-A 完成
- **验证方式**: 编译通过

### S3-C: 组合键检测 (KEY1+KEY2)
- **所属子系统**: key
- **开发内容**:
  1. 在 `KeyInd_HandleEvents()` 的轮询循环之后、事件清理之前
  2. 检测 `KeyInd_State[0] == 0 && KeyInd_State[1] == 0`（两键同时按下）
  3. 执行 `rgb_clear()`
- **前置条件**: S3-B 完成
- **验证方式**: 编译通过

### S3-D: app_main Phase 2 事件组创建
- **所属子系统**: main
- **开发内容**:
  1. 在 `app_main()` Phase 2 中创建事件组
- **前置条件**: S3-A 完成
- **验证方式**: 编译通过

### S3-E: 修复 ISR 顺序
- **所属子系统**: main
- **开发内容**:
  1. 将 `gpio_install_isr_service(0)` 移到 `key_init()` 之前（修复当前 bug）
- **前置条件**: 无
- **验证方式**: 编译通过
