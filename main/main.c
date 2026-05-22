/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "bsp.h"
#include "rgb.h"

extern void queue_demo_main(void);

static void rgb_task(void *pv)
{
    uint16_t hue = 0;
    while (1) {
        rgb_set_hsv(hue, 100, 100);
        hue = (hue + 1) % 360;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void wdt_initial(void)
{
    /* bootloader 可能已初始化 TWDT，先反初始化再重配 */
    esp_task_wdt_deinit();

    esp_task_wdt_config_t twdt_config = {
        .timeout_ms = 10000,
        .idle_core_mask = (1 << 0) | (1 << 1),
        .trigger_panic = true,
    };
    esp_task_wdt_init(&twdt_config);
}


void app_main(void)
{
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    printf("This is %s chip with %d CPU core(s), %s%s%s%s, ",
           CONFIG_IDF_TARGET,
           chip_info.cores,
           (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi/" : "",
           (chip_info.features & CHIP_FEATURE_BT) ? "BT" : "",
           (chip_info.features & CHIP_FEATURE_BLE) ? "BLE" : "",
           (chip_info.features & CHIP_FEATURE_IEEE802154) ? ", 802.15.4 (Zigbee/Thread)" : "");

    gpio_install_isr_service(0);// 参数 0 = ESP_INTR_FLAG_DEFAULT

    led_init();
    key_init();

    /* 初始化 OLED 并启动 50Hz 显示任务 */
    {
        i2c_obj_t oled_dev = iic_init(I2C_NUM_1);
        oled_init(oled_dev);
    }
    oled_display_task_start();
    /* 先填充初始显示数据 */
    g_disp.wifi_on = 0;
    g_disp.bt_on   = 0;
    g_disp.ds18b20_temp = 0;
    g_disp.dht11_temp   = 0;
    g_disp.dht11_humi   = 0;
    g_disp.key_count    = 0;
    g_disp.dirty = true;

    if (ds18b20_init() == 0)
        printf("DS18B20 检测成功\n");
    else
        printf("DS18B20 未检测到\n");

    if (dht11_init() == 0)
        printf("DHT11 检测成功\n");
    else
        printf("DHT11 未检测到\n");

    rgb_init();

    wdt_initial();

    xTaskCreatePinnedToCore(key_task,"keytask",2048,NULL,6,NULL,0);
    xTaskCreatePinnedToCore(led_task,"ledtask",2048,NULL,4,NULL,1);
    // xTaskCreatePinnedToCore(rgb_task,"rgbtask",2048,NULL,3,NULL,1);

    // printf("keytask 剩余栈: %u 字节\n", uxTaskGetStackHighWaterMark(xTaskGetHandle("keytask")));
    // printf("ledtask  剩余栈: %u 字节\n", uxTaskGetStackHighWaterMark(xTaskGetHandle("ledtask")));

    while(1)
    {
        // queue_demo_main();
        vTaskDelay(1000);
    }
}
