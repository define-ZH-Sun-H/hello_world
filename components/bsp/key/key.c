/**
 * @file        key.c
 * @brief       按键驱动 — 纯 GPIO 驱动
 *
 * 职责范围（仅）：
 *   1. GPIO 配置 + 中断（ISR 已注释，预留）
 *   2. KeyInd_Scan()：10ms 周期轮询 GPIO → 防抖 → 设事件组位
 *
 * 不做的事：
 *   不直接调用 menu/oled_display/led/rgb，
 *   不管理连击逻辑，
 *   不直接发送队列消息。
 *   以上均由 sys_ctrl.c 通过事件组 + 显示状态查询完成。
 */

#include <stdio.h>
#include "key.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_task_wdt.h"
#include "debug.h"

/* 按键状态结构体（取代松散数组） */
typedef struct {
    uint8_t  state;          /* 稳定状态：1=释放，0=按下 */
    uint8_t  filter_cnt;     /* 防抖计数器 */
    uint16_t hold_cnt;       /* 长按保持计数器 */
} key_ind_t;

static key_ind_t s_keys[KEY_IND_COUNT];      /* 4 个按键的结构体数组 */

/* 按键事件组句柄 */
EventGroupHandle_t key_event_group = NULL;

/**
 * @brief 按键初始化：配置 GPIO 上拉输入 + 下降沿中断（预留）
 */
void key_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << KEY1_GPIO_PIN) |
                        (1ULL << KEY2_GPIO_PIN) |
                        (1ULL << KEY3_GPIO_PIN) |
                        (1ULL << KEY4_GPIO_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&io_conf);

    DBG_INFO("按键 GPIO 已初始化\n");

    key_event_group = xEventGroupCreate();
    DBG_INFO("按键事件组已创建\n");
}

/**
 * @brief 按键扫描：防抖 → 设事件组位（长按检测保留）
 *
 * 每次被调用时遍历 4 个按键：
 *   1. 读 GPIO 电平
 *   2. 防抖（连续 N 次一致才确认状态变化）
 *   3. 状态变化时设对应事件组位（PRESS 设 bit 0-3）
 *   4. 按下时累计 hold_cnt，达到阈值设长按位（bit 4-7）
 *
 * 连击逻辑（ClickCnt/ClickFlag/ClickClearCnt）已全部删除。
 */
void KeyInd_Scan(void)
{
    uint8_t i;
    uint8_t level;

    for (i = 0; i < KEY_IND_COUNT; i++) {
        /* 读 GPIO 电平 */
        switch (i) {
        case 0: level = gpio_get_level(KEY1_GPIO_PIN); break;
        case 1: level = gpio_get_level(KEY2_GPIO_PIN); break;
        case 2: level = gpio_get_level(KEY3_GPIO_PIN); break;
        case 3: level = gpio_get_level(KEY4_GPIO_PIN); break;
        default: level = 1; break;
        }

        /* ---- 防抖：状态变化时累积计数，确认后切换 ---- */
        if (s_keys[i].state != level) {
            s_keys[i].filter_cnt++;
            if (s_keys[i].filter_cnt >= KEY_IND_DEBOUNCE_THRESHOLD) {
                s_keys[i].state = level;
                s_keys[i].filter_cnt = 0;

                if (level == 0) {
                    /* 按下一→ 设事件组位（bit 0-3） */
                    switch (i) {
                    case 0: xEventGroupSetBits(key_event_group, KEY_EVENT_BIT_1); break;
                    case 1: xEventGroupSetBits(key_event_group, KEY_EVENT_BIT_2); break;
                    case 2: xEventGroupSetBits(key_event_group, KEY_EVENT_BIT_3); break;
                    case 3: xEventGroupSetBits(key_event_group, KEY_EVENT_BIT_4); break;
                    }
                }
            }
        } else {
            s_keys[i].filter_cnt = 0;
        }

        /* ---- 按下状态：累计长按计数器 ---- */
        if (s_keys[i].state == 0) {
            s_keys[i].hold_cnt++;
            if (s_keys[i].hold_cnt == KEY_IND_LONG_PRESS_TIME) {
                /* 长按 → 设长按事件组位（bit 4-7） */
                switch (i) {
                case 0: xEventGroupSetBits(key_event_group, KEY_EVENT_BIT_1_LONG); break;
                case 1: xEventGroupSetBits(key_event_group, KEY_EVENT_BIT_2_LONG); break;
                case 2: xEventGroupSetBits(key_event_group, KEY_EVENT_BIT_3_LONG); break;
                case 3: xEventGroupSetBits(key_event_group, KEY_EVENT_BIT_4_LONG); break;
                }
            } else if (s_keys[i].hold_cnt > KEY_IND_LONG_PRESS_TIME) {
                s_keys[i].hold_cnt = KEY_IND_LONG_PRESS_TIME;
            }
        } else {
            s_keys[i].hold_cnt = 0;
        }
    }
}

/**
 * @brief 按键任务 — 维持轮询（作为 fallback）
 *
 * 目前由 sys_ctrl_task 替代，此任务保留作为 fallback。
 * sys_ctrl.c 直接调用 KeyInd_Scan()，不再使用 key_task()。
 */
void key_task(void *pvParameters)
{
    esp_task_wdt_add(NULL);
    while (1) {
        KeyInd_Scan();
        vTaskDelay(pdMS_TO_TICKS(10));
        esp_task_wdt_reset();
    }
}
