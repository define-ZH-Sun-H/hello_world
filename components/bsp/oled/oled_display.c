#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/queue.h"
#include "oled.h"
#include "oled_image.h"
#include "oled_display.h"
#include "app.h"              /* for app_is_active(), app_render() */

/* 前向声明菜单函数（避免 BSP 组件依赖 main 组件） */
bool menu_is_active(void);
void menu_render(void);
bool menu_handle_key(uint8_t key_id, int event);

/* 主页状态（menu.c 通过 setter 写入 wifi_on/bt_on） */
home_state_t g_home = { 0 };

/* 显示消息队列 */
QueueHandle_t disp_queue = NULL;

/* 内部状态 */
static bool s_sleep = false;        /* 息屏状态（替换 g_disp.sleep） */
static bool s_dirty = false;        /* 脏帧标志（替换 g_disp.dirty） */
static char s_time_str[6] = "00:00"; /* 状态栏时间 "HH:MM" */

/* 显示模式 */
typedef enum {
    DISP_MODE_BOOT = 0,
    DISP_MODE_NORMAL,
} disp_mode_t;

static disp_mode_t s_display_mode = DISP_MODE_BOOT;

/* 通用 OLED 定时器 */
static TimerHandle_t xOledTimer = NULL;

/* ---- 布局常量 ---- */
#define STATUS_BAR_H       16
#define SEPARATOR_Y        16
#define CONTENT_START_Y    18

static void oled_timer_callback(TimerHandle_t xTimer)
{
    disp_msg_t msg = { .type = DISP_MSG_NONE };

    switch (s_display_mode) {
    case DISP_MODE_BOOT:
        s_display_mode = DISP_MODE_NORMAL;
        msg.type = DISP_MSG_SCREEN_WAKE;
        xTimerChangePeriod(xOledTimer, pdMS_TO_TICKS(60000), 0);
        break;

    case DISP_MODE_NORMAL:
        msg.type = DISP_MSG_SCREEN_SLEEP;
        break;
    }

    if (msg.type != DISP_MSG_NONE) {
        xQueueSend(disp_queue, &msg, 0);
    }
}

static void draw_separator(void)
{
    for (uint8_t i = 0; i < 128; i++)
        oled_draw_point(i, SEPARATOR_Y, 1);
}

/* ========== 队列消息消费（handler 表调度） ========== */

typedef void (*disp_handler_t)(const disp_msg_t *msg);

/* ---------- handler 函数 ---------- */

static void handle_boot_screen(const disp_msg_t *m)
{
    s_display_mode = DISP_MODE_BOOT;
    s_dirty = true;
    xTimerChangePeriod(xOledTimer, pdMS_TO_TICKS(2000), 0);
    xTimerReset(xOledTimer, 0);
}

static void handle_screen_sleep(const disp_msg_t *m)
{
    s_sleep = true;
    oled_off();
    s_dirty = false;
}

static void handle_screen_wake(const disp_msg_t *m)
{
    s_sleep = false;
    oled_on();
    s_dirty = true;
    if (s_display_mode == DISP_MODE_NORMAL) {
        xTimerReset(xOledTimer, 0);
    }
}

static void handle_key_event(const disp_msg_t *m)
{
    menu_handle_key(m->val1, m->val2);
    s_dirty = true;
}

static void handle_set_wifi(const disp_msg_t *m)
{
    g_home.wifi_on = m->val1 ? 1 : 0;
    s_dirty = true;
}

static void handle_set_bt(const disp_msg_t *m)
{
    g_home.bt_on = m->val1 ? 1 : 0;
    s_dirty = true;
}

static void handle_set_sleep(const disp_msg_t *m)
{
    s_sleep = m->val1 ? true : false;
    if (m->val1) {
        oled_off();
        s_dirty = false;
    } else {
        oled_on();
        s_dirty = true;
    }
}

static void handle_set_time(const disp_msg_t *m)
{
    s_time_str[0] = '0' + (m->val1 / 10) % 10;
    s_time_str[1] = '0' + m->val1 % 10;
    s_time_str[2] = ':';
    s_time_str[3] = '0' + (m->val2 / 10) % 10;
    s_time_str[4] = '0' + m->val2 % 10;
    s_dirty = true;
}

static void handle_mark_dirty(const disp_msg_t *m)
{
    s_dirty = true;
}

/* ---------- 查表 ---------- */

static const disp_handler_t s_disp_handlers[DISP_MSG_COUNT] = {
    [DISP_MSG_NONE]          = NULL,
    [DISP_MSG_BOOT_SCREEN]   = handle_boot_screen,
    [DISP_MSG_SCREEN_SLEEP]  = handle_screen_sleep,
    [DISP_MSG_SCREEN_WAKE]   = handle_screen_wake,
    [DISP_MSG_KEY_EVENT]     = handle_key_event,
    [DISP_MSG_SET_WIFI]      = handle_set_wifi,
    [DISP_MSG_SET_BT]        = handle_set_bt,
    [DISP_MSG_SET_SLEEP]     = handle_set_sleep,
    [DISP_MSG_SET_TIME]      = handle_set_time,
    [DISP_MSG_MARK_DIRTY]    = handle_mark_dirty,
};

/* ---------- process_disp_queue ---------- */

static void process_disp_queue(void)
{
    disp_msg_t msg;

    /* 上限 12 = 队列深度，防止 handler 内部 xQueueSend 嵌套使循环无限 */
    for (int i = 0; i < 12; i++) {
        if (xQueueReceive(disp_queue, &msg, 0) != pdTRUE) break;
        if (msg.type < DISP_MSG_COUNT && s_disp_handlers[msg.type])
            s_disp_handlers[msg.type](&msg);
    }
}

/* ========== 帧渲染 ========== */

static void render_frame(void)
{
    if (!s_dirty || s_sleep) {
        return;
    }

    if (s_display_mode == DISP_MODE_BOOT) {
        /* —— 开机画面 —— */
        oled_clear_gram();
        oled_show_string(4, 8, "Hello World", 24);
        oled_show_string(4, 36, "ESP32-S3 Dome", 12);
        oled_show_string(4, 50, "v0.0.7", 12);
        oled_refresh_gram();

    } else if (app_is_active()) {
        /* —— App 活跃：由 app_render 自行处理全帧 —— */
        app_render();

    } else if (menu_is_active()) {
        /* —— 菜单模式 —— */
        menu_render();

    } else {
        /* —— 主页：状态栏 + 项目名称 —— */
        oled_clear_gram();

        if (g_home.wifi_on)
            oled_draw_bitmap(0, 0, 16, 16, icon_wifi_on);

        if (g_home.bt_on)
            oled_draw_bitmap(18, 0, 16, 16, icon_bt_on);

        /* 状态栏右侧显示时间 */
        oled_show_string(96, 2, s_time_str, 12);

        draw_separator();

        oled_show_string(28, 28, "Hello World", 16);

        oled_refresh_gram();
    }

    s_dirty = false;
}

/* ========== 显示任务 ========== */

void display_task(void *pv)
{
    while (1) {
        process_disp_queue();
        render_frame();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/* ========== 初始化 ========== */

void oled_display_init(void)
{
    disp_queue = xQueueCreate(12, sizeof(disp_msg_t));

    g_home.wifi_on = 0;
    g_home.bt_on   = 0;
    s_sleep = false;
    s_dirty = true;

    xOledTimer = xTimerCreate("oled_tmr",
                                pdMS_TO_TICKS(60000),
                                pdTRUE, NULL,
                                oled_timer_callback);

    /* 启动时发送开机画面消息 */
    {
        disp_msg_t m = { .type = DISP_MSG_BOOT_SCREEN };
        xQueueSend(disp_queue, &m, 0);
    }
}

/* ========== Setter API ========== */

void oled_display_set_wifi(bool on)
{
    disp_msg_t m = { .type = DISP_MSG_SET_WIFI, .val1 = on };
    xQueueSend(disp_queue, &m, 0);
}

void oled_display_set_bt(bool on)
{
    disp_msg_t m = { .type = DISP_MSG_SET_BT, .val1 = on };
    xQueueSend(disp_queue, &m, 0);
}

void oled_display_set_sleep(bool on)
{
    disp_msg_t m = { .type = DISP_MSG_SET_SLEEP, .val1 = on };
    xQueueSend(disp_queue, &m, 0);
}

void oled_display_mark_dirty(void)
{
    disp_msg_t m = { .type = DISP_MSG_MARK_DIRTY };
    xQueueSend(disp_queue, &m, 0);
}

bool oled_display_is_sleeping(void)
{
    return s_sleep;
}

void oled_display_set_time(const char *time_str)
{
    uint8_t hh = (uint8_t)((time_str[0] - '0') * 10 + (time_str[1] - '0'));
    uint8_t mm = (uint8_t)((time_str[3] - '0') * 10 + (time_str[4] - '0'));
    disp_msg_t m = { .type = DISP_MSG_SET_TIME, .val1 = hh, .val2 = mm };
    xQueueSend(disp_queue, &m, 0);
}
