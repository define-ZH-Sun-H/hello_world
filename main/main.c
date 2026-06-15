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
#include "esp_system.h"
#include "sys_info.h"
#include "storage_init.h"
#include "bsp.h"
#include "menu.h"
#include "rgb.h"
#include "wifi.h"
#include "sys_ctrl.h"
#include "mqtt.h"

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

/**
 * @brief 统一创建所有 FreeRTOS 任务
 *
 * 所有 xTaskCreatePinnedToCore 集中于此，方便查看栈/优先级/核绑定。
 * 必须在所有硬件初始化完成后调用。
 */
static void create_all_tasks(void)
{
    /* 系统控制任务（10ms 周期，含按键扫描 + 事件分发）— 替代原 key_task */
    sys_ctrl_init();

    /* 传感器采集（100ms 周期，core 0，优先级 4） */
    xTaskCreatePinnedToCore(sensor_task, "sensor_task", 2560, NULL, 10, NULL, 0);

    /* OLED 显示（50Hz 刷新，core 1，优先级 5） */
    xTaskCreatePinnedToCore(display_task, "oled_disp", 3072, NULL, 8, NULL, 1);

    /* 音频采集 → MQTT 发布（等 MQTT 就绪后自动启动录音） */
    mqtt_audio_test_start();

    // xTaskCreatePinnedToCore(led_task, "ledtask", 2048, NULL, 4, NULL, 1);
    // xTaskCreatePinnedToCore(rgb_task, "rgbtask", 2048, NULL, 3, NULL, 1);
}

void app_main(void)
{
    print_chip_info();
    storage_init_all();
    nvs_init();

    /* --- Phase 1: 板级硬件初始化 --- */
    bsp_init();

    /* ================================================================
     * Phase 2 — 数据/队列初始化
     * ================================================================ */
    oled_display_init();                        /* 创建队列 + 初始显示数据 + 发送开机画面 */
    menu_init();                                /* 菜单系统初始化（加载 NVS 设置） */

    /* ================================================================
     * Phase 3 — 统一创建所有任务
     * ================================================================ */
    create_all_tasks();

    /* ================================================================
     * Phase 4 — WiFi Station 连接
     * ================================================================ */
    wifi_init_sta();
    mqtt_app_start();

    // printf("keytask 剩余栈: %u 字节\n", uxTaskGetStackHighWaterMark(xTaskGetHandle("keytask")));
    // printf("ledtask  剩余栈: %u 字节\n", uxTaskGetStackHighWaterMark(xTaskGetHandle("ledtask")));

    while(1)
    {
        // queue_demo_main();
        vTaskDelay(1000);
    }
}
