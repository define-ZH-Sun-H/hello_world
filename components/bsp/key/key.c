/**
 ****************************************************************************************************
 * @file        key.c
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

#include <stdio.h>
#include "key.h"
#include "led.h"
#include "ds18b20.h"
#include "dht11.h"
#include "oled_display.h"
#include "rgb.h"
#include "debug.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"        // for xSemaphoreCreateBinary / GiveFromISR / Take
#include "freertos/event_groups.h"  // for xEventGroupSetBits / ClearBits / WaitBits
#include "esp_task_wdt.h"

/* 二值信号量句柄：按键按下时 ISR 给出，key_task 阻塞等待 */
SemaphoreHandle_t key_sem = NULL;

/* 按键事件组句柄 */
EventGroupHandle_t key_event_group = NULL;

/* 按键中断服务函数（IRAM_ATTR 确保存放在 RAM 中，快速响应） */
static void IRAM_ATTR key_isr_handler(void *arg)
{
    /* 从 ISR 中给出信号量，唤醒等待的任务 */
    /* 参数2传 NULL 表示不在此处做上下文切换，统一在 tick 中断时处理 */
    // xSemaphoreGiveFromISR(key_sem, NULL);

    /* 从 ISR 发送按键消息到显示队列 */
    // disp_msg_t msg = { .type = DISP_MSG_KEY_PRESS, .val1 = (int16_t)(intptr_t)arg };
    // xQueueSendFromISR(disp_queue, &msg, NULL);
}

uint8_t KeyInd_State[KEY_IND_COUNT];           // 稳定状态：1=释放，0=按下
uint8_t KeyInd_FilterCnt[KEY_IND_COUNT];       // 防抖计数器
uint16_t KeyInd_HoldCnt[KEY_IND_COUNT];        // 保持时间计数器
uint8_t KeyInd_ClickCnt[KEY_IND_COUNT];        // 连击计数（直接访问）
uint8_t KeyInd_ClickFlag[KEY_IND_COUNT];       // 连击标志
uint8_t KeyInd_ClickClearCnt[KEY_IND_COUNT];   // 连击清除计数器
// 独立事件变量数组（每个按键独立的事件类型和标志）
KeyIndEventType KeyInd_Event[KEY_IND_COUNT];   // 事件类型数组
uint8_t KeyInd_HasEvent[KEY_IND_COUNT];        // 是否有新事件数组

/**
 * @brief   按键初始化：配置 GPIO 下降沿中断、注册 ISR
 *
 * @note 只做硬件初始化，不创建任务 —— key_task 由 app_main 统一创建。
 */
void key_init(void)
{
    /* 配置 4 个按键 GPIO：上拉输入 + 下降沿中断 */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << KEY1_GPIO_PIN) |
                        (1ULL << KEY2_GPIO_PIN) |
                        (1ULL << KEY3_GPIO_PIN) |
                        (1ULL << KEY4_GPIO_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,    // 下降沿触发：按键按下时电平从高→低
    };
    gpio_config(&io_conf);

    // /* 创建二值信号量（初始值为 0，第一次 Take 会阻塞） */
    // key_sem = xSemaphoreCreateBinary();
    DBG_INFO("按键信号量已创建\n");

    /* 为每个按键注册中断服务函数 */
    // gpio_isr_handler_add(KEY1_GPIO_PIN, key_isr_handler, (void *)KEY1_GPIO_PIN);
    // gpio_isr_handler_add(KEY2_GPIO_PIN, key_isr_handler, (void *)KEY2_GPIO_PIN);
    // gpio_isr_handler_add(KEY3_GPIO_PIN, key_isr_handler, (void *)KEY3_GPIO_PIN);
    // gpio_isr_handler_add(KEY4_GPIO_PIN, key_isr_handler, (void *)KEY4_GPIO_PIN);

    DBG_INFO("按键中断已注册（KEY1~KEY4）\n");
}


/**
 * @brief   按键扫描：先滤波，再处理每个按键的保持计数、长按检测和连击逻辑
 */
void KeyInd_Scan(void)
{
    uint8_t i;
    uint8_t currentPinState;
    for (i = 0; i < KEY_IND_COUNT; i++)
    {
        switch (i) {
        case 0: currentPinState = gpio_get_level(KEY1_GPIO_PIN); break;
        case 1: currentPinState = gpio_get_level(KEY2_GPIO_PIN); break;
        case 2: currentPinState = gpio_get_level(KEY3_GPIO_PIN); break;
        case 3: currentPinState = gpio_get_level(KEY4_GPIO_PIN); break;
        default: currentPinState = 0;  break; // 安全默认值（释放状态）
        }
        if (KeyInd_State[i] != currentPinState){
            // 引脚状态变化，增加防抖计数器
            KeyInd_FilterCnt[i]+=1;
            if (KeyInd_FilterCnt[i] >= KEY_IND_DEBOUNCE_THRESHOLD) {
                // 防抖完成，确认状态变化
                KeyInd_State[i] = currentPinState;
                KeyInd_FilterCnt[i] = 0;
                // 生成事件
                if (currentPinState == 0) {  // 按下事件
                    KeyInd_Event[i] = KEY_IND_EVENT_PRESS;
                    KeyInd_HasEvent[i] = 1;
                } else {  // 释放事件
                    KeyInd_Event[i] = KEY_IND_EVENT_RELEASE;
                    KeyInd_HasEvent[i] = 1;
                }
            }
        }
        if (KeyInd_State[i] == 0) {  // 按键按下状态
            // 增加保持时间计数器（用于长按检测）
            KeyInd_HoldCnt[i]+=1;
            // 长按检测
            if (KeyInd_HoldCnt[i] == KEY_IND_LONG_PRESS_TIME) {
                // 达到长按时间，生成长按事件
                KeyInd_Event[i] = KEY_IND_EVENT_LONG_PRESS;
                KeyInd_HasEvent[i] = 1;

                // 长按时重置连击计数
                KeyInd_ClickCnt[i] = 0;
                KeyInd_ClickClearCnt[i] = 0;
                KeyInd_ClickFlag[i] = 0;  // 长按时禁止连击
            } else if (KeyInd_HoldCnt[i] > KEY_IND_LONG_PRESS_TIME) {
                // 保持长按状态，防止计数器溢出
                KeyInd_HoldCnt[i] = KEY_IND_LONG_PRESS_TIME;
            }

            // 连击检测（基于PT-SZH的Click_Cnt设计）
            if (KeyInd_ClickFlag[i]) {
                KeyInd_ClickFlag[i] = 0;  // 清除标志，防止重复计数
                KeyInd_ClickCnt[i]+=1;     // 增加连击计数

                // 连击事件（2次及以上）
                if (KeyInd_ClickCnt[i] >= 2) {
                    KeyInd_Event[i] = KEY_IND_EVENT_CLICK_MULTI;
                    KeyInd_HasEvent[i] = 1;
                }
            }

            // 连击清除时间设置
            if (KeyInd_ClickCnt[i] == 5) {
                KeyInd_ClickClearCnt[i] = 5;  // 特殊处理5次连击
            } else {
                KeyInd_ClickClearCnt[i] = KEY_IND_CLICK_CLEAR_TIME;
            }
        } else {  // 按键释放状态
            KeyInd_ClickFlag[i] = 1;  // 允许下一次连击检测
            KeyInd_HoldCnt[i] = 0;    // 重置长按计数器

            // 连击清除计数器递减
            if (KeyInd_ClickClearCnt[i] > 0) {
                KeyInd_ClickClearCnt[i]-=1;
            } else {
                KeyInd_ClickCnt[i] = 0;  // 清除连击计数
            }
        }
    }
}

/**
 * @brief   按键事件处理：遍历所有按键，分派已产生的事件到对应的处理分支
 */
void KeyInd_HandleEvents(void) {
    uint8_t i;
    for (i = 0; i < KEY_IND_COUNT; i++) {
        if (!KeyInd_HasEvent[i]) {
            continue;
        }

        switch (i) {
        case 0:  // ========== K1 ==========
            switch (KeyInd_Event[i]) {
            case KEY_IND_EVENT_PRESS:
                LED_Flag = !LED_Flag;
                rgb_set_color(255, 0, 0);
                xEventGroupSetBits(key_event_group, KEY_EVENT_BIT_1);
                { disp_msg_t m = { .type = DISP_MSG_SCREEN_WAKE }; xQueueSend(disp_queue, &m, 0); }
                DBG_DEBUG("K1 按下\n");
                break;
            case KEY_IND_EVENT_LONG_PRESS:
                DBG_DEBUG("K1 长按\n");
                break;
            case KEY_IND_EVENT_RELEASE:
                xEventGroupClearBits(key_event_group, KEY_EVENT_BIT_1);
                DBG_DEBUG("K1 释放\n");
                break;
            case KEY_IND_EVENT_CLICK_MULTI:
                DBG_DEBUG("K1 连击 %d 次\n", KeyInd_ClickCnt[i]);
                break;
            default:
                break;
            }
            break;

        case 1:  // ========== K2 ==========
            switch (KeyInd_Event[i]) {
            case KEY_IND_EVENT_PRESS:
                LED_Flag = !LED_Flag;
                rgb_set_color(255, 255, 0);
                xEventGroupSetBits(key_event_group, KEY_EVENT_BIT_2);
                { disp_msg_t m = { .type = DISP_MSG_SCREEN_WAKE }; xQueueSend(disp_queue, &m, 0); }
                DBG_DEBUG("K2 按下\n");
                break;
            case KEY_IND_EVENT_LONG_PRESS:
                DBG_DEBUG("K2 长按\n");
                break;
            case KEY_IND_EVENT_RELEASE:
                xEventGroupClearBits(key_event_group, KEY_EVENT_BIT_2);
                DBG_DEBUG("K2 释放\n");
                break;
            case KEY_IND_EVENT_CLICK_MULTI:
                DBG_DEBUG("K2 连击 %d 次\n", KeyInd_ClickCnt[i]);
                break;
            default:
                break;
            }
            break;

        case 2:  // ========== K3 ==========
            switch (KeyInd_Event[i]) {
            case KEY_IND_EVENT_PRESS:
                LED_Flag = !LED_Flag;
                rgb_set_color(0, 0, 255);
                xEventGroupSetBits(key_event_group, KEY_EVENT_BIT_3);
                { disp_msg_t m = { .type = DISP_MSG_SCREEN_WAKE }; xQueueSend(disp_queue, &m, 0); }
                DBG_DEBUG("K3 按下\n");
                break;
            case KEY_IND_EVENT_LONG_PRESS:
                DBG_DEBUG("K3 长按\n");
                break;
            case KEY_IND_EVENT_RELEASE:
                xEventGroupClearBits(key_event_group, KEY_EVENT_BIT_3);
                DBG_DEBUG("K3 释放\n");
                break;
            case KEY_IND_EVENT_CLICK_MULTI:
                DBG_DEBUG("K3 连击 %d 次\n", KeyInd_ClickCnt[i]);
                break;
            default:
                break;
            }
            break;

        case 3:  // ========== K4 ==========
            switch (KeyInd_Event[i]) {
            case KEY_IND_EVENT_PRESS:
                LED_Flag = !LED_Flag;
                rgb_set_color(255, 255, 255);
                xEventGroupSetBits(key_event_group, KEY_EVENT_BIT_4);
                { disp_msg_t m = { .type = DISP_MSG_SCREEN_WAKE }; xQueueSend(disp_queue, &m, 0); }
                DBG_DEBUG("K4 按下\n");
                break;
            case KEY_IND_EVENT_LONG_PRESS:
                DBG_DEBUG("K4 长按\n");
                break;
            case KEY_IND_EVENT_RELEASE:
                xEventGroupClearBits(key_event_group, KEY_EVENT_BIT_4);
                DBG_DEBUG("K4 释放\n");
                break;
            case KEY_IND_EVENT_CLICK_MULTI:
                DBG_DEBUG("K4 连击 %d 次\n", KeyInd_ClickCnt[i]);
                break;
            default:
                break;
            }
            break;

        default:
            break;
        }
        // 清除已处理的事件
        if (i < KEY_IND_COUNT) {
            KeyInd_Event[i] = KEY_IND_EVENT_NONE;
            KeyInd_HasEvent[i] = 0;
        }
    }
    /* KEY1+KEY2 同时按下 → 关闭 RGB LED（使用事件组检测） */
    {
        EventBits_t bits = xEventGroupWaitBits(
                key_event_group,
                KEY_EVENT_BIT_1 | KEY_EVENT_BIT_2,
                pdTRUE,         /* 清除 bit（一次性检测，避免重复触发） */
                pdTRUE,         /* AND：两个按键都按下 */
                0);             /* 不阻塞，纯轮询 */
        if ((bits & (KEY_EVENT_BIT_1 | KEY_EVENT_BIT_2)) == (KEY_EVENT_BIT_1 | KEY_EVENT_BIT_2)) {
            rgb_clear();
            DBG_DEBUG("KEY1+KEY2 组合键: RGB 关闭\n");
        }
    }
}

/**
 * @brief   按键任务：等待中断信号量 → 防抖延迟 → 扫描 → 处理事件
 * @note    区别于旧版轮询，任务大部分时间阻塞在 xSemaphoreTake，几乎不占 CPU
 *          带超时（8s）是为了定期喂狗，而非按键触发也需要复位 WDT
 */
void key_task(void *pvParameters)
{
    esp_task_wdt_add(NULL);
    while(1) {
        // /* 阻塞等待按键中断给出的信号量（最多等 8 秒，超时用于喂狗） */
        // /* portMAX_DELAY 会永久阻塞，但 WDT 10s 会超时，所以带超时 */
        // xSemaphoreTake(key_sem, pdMS_TO_TICKS(2000));

        /* 扫描一次所有按键状态 */
        KeyInd_Scan();

        /* 处理已产生的事件（按下/长按/释放/连击） */
        KeyInd_HandleEvents();

        /* 防抖：等待 20ms 让电平稳定后再读 GPIO */
        vTaskDelay(pdMS_TO_TICKS(10));

        /* 喂狗：无论按键触发还是超时唤醒，都复位看门狗 */
        esp_task_wdt_reset();
    }
}
