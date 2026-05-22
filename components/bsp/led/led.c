/**
 ****************************************************************************************************
 * @file        led.c
 * @author      普中科技
 * @version     V1.0
 * @date        2024-03-01
 * @brief       LED驱动代码
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

#include "led.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"

bool LED_Flag = 0;
static bool led_level = 0;

void led_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_NUM_3),   // 选中 GPIO3
        .mode = GPIO_MODE_OUTPUT,               // 输出模式
        .pull_up_en = GPIO_PULLUP_DISABLE,      // 禁止上拉
        .pull_down_en = GPIO_PULLDOWN_DISABLE,  // 禁止下拉
        .intr_type = GPIO_INTR_DISABLE,         // 禁止中断
    };
    gpio_config(&io_conf);
    gpio_set_level(GPIO_NUM_3, 0);
}

void led_toggle(void)
{
    led_level = !led_level;
    gpio_set_level(GPIO_NUM_3, led_level);
}

void led_task(void *pvParameters)
{
    esp_task_wdt_add(NULL);
    while(1) {
        if(LED_Flag){
            led_toggle();
        }
        else{
            gpio_set_level(GPIO_NUM_3, 0);
        }
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    
}

