#ifndef __OLED_DISPLAY_H
#define __OLED_DISPLAY_H

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/* 显示消息类型 */
typedef enum {
    DISP_MSG_NONE = 0,
    DISP_MSG_KEY_PRESS,      /* 按键按下：val1 = key_id */
    DISP_MSG_DS18B20_TEMP,   /* DS18B20 温度：val1 = temp ×10 */
    DISP_MSG_DHT11_DATA,     /* DHT11 数据：val1 = temp, val2 = humi */
} disp_msg_type_t;

/* 队列消息结构体 */
typedef struct {
    disp_msg_type_t type;
    int16_t val1;
    int16_t val2;
} disp_msg_t;

/* 显示内容结构体 —— 各任务只写此结构体 + dirty 标志，显示任务自动刷新 */
typedef struct {
    /* ===== 状态栏 ===== */
    uint8_t  wifi_on : 1;          /* WiFi 连接状态 */
    uint8_t  bt_on   : 1;          /* 蓝牙连接状态 */
    /* 时间占位（后续扩展） */

    /* ===== 内容区 ===== */
    int16_t  ds18b20_temp;         /* DS18B20 温度，单位 0.1°C（285 = 28.5°C） */
    int8_t   dht11_temp;           /* DHT11 温度整数部分 */
    uint8_t  dht11_humi;           /* DHT11 湿度整数部分 */
    uint32_t key_count;            /* 按键总次数 */

    bool dirty;                     /* 有数据变更，需要刷新屏幕 */
} display_t;

extern display_t g_disp;
extern QueueHandle_t disp_queue;     /* 显示消息队列句柄 */

/* 启动显示任务（优先级 5，50Hz 刷新） */
void oled_display_task_start(void);

#endif
