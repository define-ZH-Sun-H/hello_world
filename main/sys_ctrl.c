/**
 * @file        sys_ctrl.c
 * @brief       系统控制任务 — 连接输入操作和系统变化
 *
 * ===== 职责 =====
 *
 * 本模块是"操作管理任务"，以 10ms 周期运行，负责三件事：
 *   ① 调用 KeyInd_Scan() 驱动按键扫描（GPIO 采样 + 防抖 + 设事件组位）
 *   ② 读取事件组位掩码
 *   ③ 按当前显示状态将按键事件分发到正确模块：
 *        app 活跃 → app_handle_key
 *        菜单活跃 → menu_handle_key
 *        主页/开机 → 系统操作（唤醒/进菜单/组合键）
 *
 * ===== 不做的 =====
 *
 *   不直接操作硬件（全部委托给 key/oled/rgb/led 驱动）。
 *   不包含任何显示逻辑。
 *   不管理传感器（传感器数据暂存于 sensor 模块内部）。
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "esp_task_wdt.h"

#include "key.h"
#include "app.h"
#include "menu.h"
#include "oled_display.h"
#include "rgb.h"

/* ---- 组合键防重复触发 ---- */
static bool s_combo_handled = false;

/**
 * @brief 系统控制任务（10ms 周期）
 *
 * 工作流程：
 *   1. 调用 KeyInd_Scan() 扫描按键
 *   2. 读取事件组，获取待处理按键位
 *   3. 按当前显示状态分发
 *   4. 清空事件组
 *   5. vTaskDelay 10ms
 */
static void sys_ctrl_task(void *pv)
{
    EventBits_t bits;
    uint8_t i;

    esp_task_wdt_add(NULL);

    while (1) {
        /* ① 按键扫描 */
        KeyInd_Scan();

        /* ② 读取事件组 */
        bits = xEventGroupGetBits(key_event_group);
        if (bits == 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            esp_task_wdt_reset();
            continue;
        }

        /* ③ 预先清空事件组（只清本次读取的位，防止并发写入丢失） */
        xEventGroupClearBits(key_event_group, bits);

        /* ④ 息屏唤醒检测：息屏状态下任何按键仅唤醒屏幕，不触发功能 */
        if (oled_display_is_sleeping()) {
            disp_msg_t m = { .type = DISP_MSG_SCREEN_WAKE };
            xQueueSend(disp_queue, &m, 0);
            vTaskDelay(pdMS_TO_TICKS(10));
            esp_task_wdt_reset();
            continue;
        }

        /* ⑤ 按显示状态分发 */

        if (app_is_active()) {
            /* —— App 模式：所有按键路由到当前 app —— */
            for (i = 0; i < KEY_IND_COUNT; i++) {
                if (bits & (1 << i)) {
                    app_handle_key(i, KEY_IND_EVENT_PRESS);
                }
                if (bits & (1 << (i + 4))) {
                    app_handle_key(i, KEY_IND_EVENT_LONG_PRESS);
                }
            }

        } else if (menu_is_active()) {
            /* —— 菜单模式：按键消息发送到 disp_queue —— */
            for (i = 0; i < KEY_IND_COUNT; i++) {
                if (bits & (1 << i)) {
                    disp_msg_t m = { .type = DISP_MSG_KEY_EVENT, .val1 = i, .val2 = KEY_IND_EVENT_PRESS };
                    xQueueSend(disp_queue, &m, 0);
                }
                if (bits & (1 << (i + 4))) {
                    disp_msg_t m = { .type = DISP_MSG_KEY_EVENT, .val1 = i, .val2 = KEY_IND_EVENT_LONG_PRESS };
                    xQueueSend(disp_queue, &m, 0);
                }
            }

        } else {
            /* —— 主页 / 开机画面模式：系统操作 —— */

            /* K1-K4 短按 → 发送 KEY_EVENT，由 display_task 内 menu_handle_key 处理进入菜单 */
            for (i = 0; i < KEY_IND_COUNT; i++) {
                if (bits & (1 << i)) {
                    disp_msg_t m = { .type = DISP_MSG_KEY_EVENT, .val1 = i, .val2 = KEY_IND_EVENT_PRESS };
                    xQueueSend(disp_queue, &m, 0);
                }
            }

            /* K1 + K2 组合键 → 关闭 RGB */
            {
                bool k1_pressed = (gpio_get_level(KEY1_GPIO_PIN) == 0);
                bool k2_pressed = (gpio_get_level(KEY2_GPIO_PIN) == 0);
                if (k1_pressed && k2_pressed) {
                    if (!s_combo_handled) {
                        rgb_clear();
                        s_combo_handled = true;
                    }
                } else {
                    s_combo_handled = false;
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
        esp_task_wdt_reset();
    }
}

void sys_ctrl_init(void)
{
    xTaskCreatePinnedToCore(sys_ctrl_task, "sys_ctrl", 4096, NULL, 9, NULL, 0);
}
