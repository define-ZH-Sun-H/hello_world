#ifndef __OLED_DISPLAY_H
#define __OLED_DISPLAY_H

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/* 显示消息类型 */
typedef enum {
    DISP_MSG_NONE = 0,
    DISP_MSG_SCREEN_SLEEP,    /* 无操作超时，息屏 */
    DISP_MSG_SCREEN_WAKE,     /* 按键唤醒屏幕 */
    DISP_MSG_BOOT_SCREEN,     /* 显示开机画面（2s 后自动切主页） */
    DISP_MSG_KEY_EVENT,       /* 按键事件：val1=key_id, val2=event */
    DISP_MSG_SET_WIFI,        /* 设置状态栏 WiFi 图标 (val1=bool) */
    DISP_MSG_SET_BT,          /* 设置状态栏 bt  图标 (val1=bool) */
    DISP_MSG_SET_SLEEP,       /* 设置息屏状态 (val1=bool) */
    DISP_MSG_SET_TIME,        /* 设置状态栏时间 (val1=hour, val2=min) */
    DISP_MSG_MARK_DIRTY,      /* 请求下一帧重绘 */
    DISP_MSG_COUNT,           /* 最后一个，用于数组大小 */
} disp_msg_type_t;

/* ===== 渲染模式（render_frame 状态机） ===== */
typedef enum {
    RENDER_MODE_BOOT = 0,    /* 开机画面 */
    RENDER_MODE_HOME,        /* 主页：状态栏 + 项目名称 */
    RENDER_MODE_MENU,        /* 菜单界面 */
    RENDER_MODE_APP,         /* App 全屏 */
} render_mode_t;

/* 队列消息结构体 */
typedef struct {
    disp_msg_type_t type;
    int16_t val1;
    int16_t val2;
} disp_msg_t;

/*
 * 主页状态 —— 仅供 menu.c 写入字段、oled_display.c 读取渲染。
 * wifi_on / bt_on 控制主页状态栏的图标显示。
 */
typedef struct {
    uint8_t  wifi_on : 1;          /* WiFi 连接状态 */
    uint8_t  bt_on   : 1;          /* 蓝牙连接状态 */
} home_state_t;

extern home_state_t g_home;             /* 主页状态（menu.c 写入） */
extern QueueHandle_t disp_queue;        /* 显示消息队列 */

/* 初始化显示系统（创建队列、定时器等） */
void oled_display_init(void);

/**
 * @brief 启动显示刷新任务
 *
 * 创建 display_task，50Hz 刷新率，core 1，优先级 8。
 * 必须在 oled_display_init 之后调用。
 */
void oled_display_start(void);

/* 显示任务函数（50Hz 刷新），由 app_main 统一创建 */
void display_task(void *pv);

/* 主页状态栏 setter（供 menu.c 调用） */
void oled_display_set_wifi(bool on);
void oled_display_set_bt(bool on);
void oled_display_set_sleep(bool on);

/* 请求下一帧重新渲染（替代直接写 g_disp.dirty） */
void oled_display_mark_dirty(void);

/* 设置状态栏时间字符串（格式 "HH:MM"，如 "14:30"） */
void oled_display_set_time(const char *time_str);

/* 查询息屏状态 */
bool oled_display_is_sleeping(void);

#endif
