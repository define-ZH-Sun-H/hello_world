#include "app.h"
#include <stddef.h>      /* NULL */
#include "sensor_init.h"
// #include "oled_display.h"  /* 已替换为 TFT LCD */
#include "rgb.h"
#include "mqtt.h"
#include "lvgl_app.h"

static const app_t *s_current_app = NULL;
static bool s_active = false;

void app_launch(const app_t *app)
{
    if (s_active && s_current_app && s_current_app->on_exit) {
        s_current_app->on_exit();
    }
    s_current_app = app;
    s_active = (app != NULL);
    if (s_active && app->on_enter) {
        app->on_enter();
    }
}

void app_exit(void)
{
    if (s_active && s_current_app && s_current_app->on_exit) {
        s_current_app->on_exit();
    }
    s_current_app = NULL;
    s_active = false;
}

bool app_is_active(void)
{
    return s_active;
}

const app_t *app_get_current(void)
{
    return s_current_app;
}

void app_render(void)
{
    if (s_active && s_current_app && s_current_app->on_render) {
        s_current_app->on_render();
    }
}

bool app_handle_key(uint8_t key_id, int event)
{
    if (s_active && s_current_app && s_current_app->on_key) {
        return s_current_app->on_key(key_id, event);
    }
    return false;
}

/* ================================================================
 * 集中创建所有 FreeRTOS 任务
 *
 * 所有任务参数（栈/优先级/核绑定）在此一目了然。
 * 在 WiFi 和 MQTT 启动之前调用。
 * ================================================================ */

void app_start_tasks(void)
{
    sys_ctrl_start();                   /* 系统控制（10ms 周期，含按键扫描 + 事件分发） */
    // sensor_start();                     /* 传感器采集（100ms, core 0, pri 10） */
    // oled_display_start();               /* OLED 已替换为 TFT LCD */
    // rgb_start_rainbow();                /* RGB 彩虹循环（core 1, pri 3） */
    lvgl_app_start();                   /* 触控彩屏完整初始化 → 创建 LVGL 任务 */
    mqtt_audio_test_start();            /* 音频采集 → MQTT 发布（等 MQTT 就绪后自动启动） */
}
