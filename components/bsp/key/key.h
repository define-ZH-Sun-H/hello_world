/**
 * @file        key.h
 * @brief       按键驱动头文件 — GPIO 配置 + 事件组位掩码
 */
#ifndef _key_H
#define _key_H

#include "driver/gpio.h"

struct EventGroupDef_t;
typedef struct EventGroupDef_t *EventGroupHandle_t;

/* 按键引脚定义 */
#define KEY1_GPIO_PIN               GPIO_NUM_36
#define KEY2_GPIO_PIN               GPIO_NUM_37
#define KEY3_GPIO_PIN               GPIO_NUM_35
#define KEY4_GPIO_PIN               GPIO_NUM_0

#define KEY_IND_COUNT 4
#define KEY_IND_DEBOUNCE_THRESHOLD 2
#define KEY_IND_LONG_PRESS_TIME 100      /* 100×10ms = 1000ms */

/* 按键事件类型（供 menu.c 等外部模块判断事件性质） */
typedef enum {
    KEY_IND_EVENT_NONE = 0,
    KEY_IND_EVENT_PRESS,
    KEY_IND_EVENT_LONG_PRESS,
    KEY_IND_EVENT_RELEASE,
    KEY_IND_EVENT_CLICK_MULTI,   /* 保留枚举值防止 menu.c 编译警告 */
} KeyIndEventType;

/* 按键事件组位掩码 */
#define KEY_EVENT_BIT_1     (1 << 0)
#define KEY_EVENT_BIT_2     (1 << 1)
#define KEY_EVENT_BIT_3     (1 << 2)
#define KEY_EVENT_BIT_4     (1 << 3)
#define KEY_EVENT_BIT_1_LONG (1 << 4)
#define KEY_EVENT_BIT_2_LONG (1 << 5)
#define KEY_EVENT_BIT_3_LONG (1 << 6)
#define KEY_EVENT_BIT_4_LONG (1 << 7)

/* 函数声明 */
void key_init(void);
void KeyInd_Scan(void);
void key_task(void *pvParameters);

/* 按键事件组句柄 */
extern EventGroupHandle_t key_event_group;

#endif
