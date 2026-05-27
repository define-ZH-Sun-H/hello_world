# 需求确认

## 1. 位掩码定义
```
KEY_EVENT_BIT_1 = (1 << 0)   // KEY1 → bit 0
KEY_EVENT_BIT_2 = (1 << 1)   // KEY2 → bit 1
KEY_EVENT_BIT_3 = (1 << 2)   // KEY3 → bit 2
KEY_EVENT_BIT_4 = (1 << 3)   // KEY4 → bit 3
```

## 2. 事件组创建位置
- **位置**: app_main Phase 2（数据/队列初始化阶段），与 oled_display_init() 同级
- **方式**: 在 key.h 中声明外部句柄 `extern EventGroupHandle_t key_event_group`，在 key.c 中定义

## 3. 设置/清除时机
- **位置**: KeyInd_HandleEvents() 中处理
  - `KEY_IND_EVENT_PRESS` → 设置对应位
  - `KEY_IND_EVENT_RELEASE` → 清除对应位
  - `KEY_IND_EVENT_LONG_PRESS` — 保持按下状态不变
  - `KEY_IND_EVENT_CLICK_MULTI` — 按连击最后一次状态处理
