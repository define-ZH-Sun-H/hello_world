/**
 ****************************************************************************************************
 * @file        led.h
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

#ifndef _led_H
#define _led_H

#include "driver/gpio.h"
#include "esp_err.h"

extern bool LED_Flag;

void led_init(void);
void led_toggle(void);
void led_task(void *pvParameters);

#endif
