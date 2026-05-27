/**
 ****************************************************************************************************
 * @file        key.h
 * @author      普中科技
 * @version     V1.0
 * @date        2024-03-01
 * @brief       按键驱动代码
 * @license     Copyright (c) 2024-2034, 深圳市普中科技有限公司
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:普中科技 ESP32-S3 开发板
 * 在线视频:https://space.bilibili.com/2146492485
 * 技术论坛:www.prechin.net
 * 公司网址:www.prechin.cn
 * 购买地址:
 *
 ****************************************************************************************************
 */

#ifndef _key_H
#define _key_H

#include "driver/gpio.h"

/* 按键事件组句柄类型前置声明（避免头文件引入全部 FreeRTOS 依赖） */
struct EventGroupDef_t;
typedef struct EventGroupDef_t *EventGroupHandle_t;

/* 按键引脚定义 */
#define KEY1_GPIO_PIN               GPIO_NUM_36
#define KEY2_GPIO_PIN               GPIO_NUM_37
#define KEY3_GPIO_PIN               GPIO_NUM_35
#define KEY4_GPIO_PIN               GPIO_NUM_0

#define KEY_IND_COUNT 4  // 独立按键数量
// 防抖参数配置
#define KEY_IND_DEBOUNCE_THRESHOLD 2  // 防抖阈值：连续采样N次一致才确认状态变化
#define KEY_IND_LONG_PRESS_TIME 100   // 长按时间：100×10ms = 1000ms (1秒)
#define KEY_IND_CLICK_CLEAR_TIME 20   // 连击清除时间：30×10ms = 300ms

// 按键事件类型（每个按键独立的事件变量）
typedef enum {
    KEY_IND_EVENT_NONE = 0,      // 无事件
    KEY_IND_EVENT_PRESS,         // 按下事件（短按）
    KEY_IND_EVENT_LONG_PRESS,    // 长按事件
    KEY_IND_EVENT_RELEASE,       // 释放事件
    KEY_IND_EVENT_CLICK_MULTI    // 连击事件（2次、3次等）
} KeyIndEventType;

/* 按键事件组位掩码 */
#define KEY_EVENT_BIT_1     (1 << 0)    // KEY1 → bit 0
#define KEY_EVENT_BIT_2     (1 << 1)    // KEY2 → bit 1
#define KEY_EVENT_BIT_3     (1 << 2)    // KEY3 → bit 2
#define KEY_EVENT_BIT_4     (1 << 3)    // KEY4 → bit 3

/* 函数声明 */
void key_init(void);
void key_task(void *pvParameters);

/* 按键事件组句柄（app_main Phase 2 中创建，key_task 写入，其他任务读取） */
extern EventGroupHandle_t key_event_group;

#endif
