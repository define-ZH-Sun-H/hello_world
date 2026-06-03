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
#include "freertos/event_groups.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_spiffs.h"
#include "bsp.h"
#include "rgb.h"
#include "wifi.h"

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
    esp_task_wdt_config_t twdt_config = {
        .timeout_ms = 15000,
        .idle_core_mask = (1 << 0) | (1 << 1),
        .trigger_panic = true,
    };
    esp_task_wdt_init(&twdt_config);
}

/**
 * @brief 统一创建所有 FreeRTOS 任务
 *
 * 所有 xTaskCreatePinnedToCore 集中于此，方便查看栈/优先级/核绑定。
 * 必须在所有硬件初始化完成后调用。
 */
static void create_all_tasks(void)
{
    /* 按键扫描（10ms 周期，core 0，优先级 6） */
    xTaskCreatePinnedToCore(key_task, "keytask", 2048, NULL, 9, NULL, 0);

    /* 传感器采集（100ms 周期，core 0，优先级 4） */
    xTaskCreatePinnedToCore(sensor_task, "sensor_task", 2560, NULL, 10, NULL, 0);

    /* OLED 显示（50Hz 刷新，core 1，优先级 5） */
    xTaskCreatePinnedToCore(display_task, "oled_disp", 3072, NULL, 8, NULL, 1);

    // xTaskCreatePinnedToCore(led_task, "ledtask", 2048, NULL, 4, NULL, 1);
    // xTaskCreatePinnedToCore(rgb_task, "rgbtask", 2048, NULL, 3, NULL, 1);
}

void app_main(void)
{
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    DBG_INFO("This is %s chip with %d CPU core(s), %s%s%s%s, ",
           CONFIG_IDF_TARGET,
           chip_info.cores,
           (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi/" : "",
           (chip_info.features & CHIP_FEATURE_BT) ? "BT" : "",
           (chip_info.features & CHIP_FEATURE_BLE) ? "BLE" : "",
           (chip_info.features & CHIP_FEATURE_IEEE802154) ? ", 802.15.4 (Zigbee/Thread)" : "");
        /* ================================================================
     * SPIFFS 挂载 — 配置文件存储
     *
     * 使用 "storage" 分区存放：
     *   - WiFi / MQTT 配置（JSON）
     *   - 用户设置（亮度、主题色等）
     *   - 传感器校准数据（偏移值）
     *
     * 关键配置：
     *        *   base_path          挂载点路径，文件用 "/spiffs/" 访问
     *   partition_label    指向 partitions.csv 中 name="storage" 的分区
     *   max_files          最大同时打开文件数（省 RAM，按需设小）
     *   format_if_mount_failed  挂载失败自动格式化（首次启动/崩溃后自救）
     * ================================================================ */
    {
        esp_vfs_spiffs_conf_t conf = {
            .base_path = "/spiffs",
            .partition_label = "storage",
            .max_files = 5,
            .format_if_mount_failed = true,
        };
        esp_err_t ret = esp_vfs_spiffs_register(&conf);
        if (ret == ESP_OK) {
            size_t total = 0, used = 0;
            esp_spiffs_info("storage", &total, &used);
            DBG_INFO("SPIFFS 挂载成功: %s 分区, 总 %d KB, 已用 %d KB\n",
                     conf.partition_label, total / 1024, used / 1024);
        } else {
            DBG_WARN("SPIFFS 挂载失败: %s\n", esp_err_to_name(ret));
        }
    }
    
    /* ================================================================
     * Phase 1 — 硬件初始化（纯寄存器/外设配置，不创建任何任务）
     * ================================================================ */
    i2c_obj_t oled_dev;
    // gpio_install_isr_service(0);                /* GPIO 中断框架（必须在 key_init 之前） */
    led_init();                                 /* LED GPIO */
    key_init();                                 /* 按键 GPIO + ISR */
    oled_dev = iic_init(I2C_NUM_1);             /* I2C 总线 */
    oled_init(oled_dev);                        /* OLED 控制器 */
    sensor_init();                              /* DS18B20 + DHT11 */
    rgb_init();                                 /* WS2812 RMT */
    wdt_initial();                              /* TWDT 10s */

    /* SD 卡（SPI 模式）— 非阻塞，无卡也不影响启动 */
    {
        esp_err_t sd_ret = sd_spi_init();
        if (sd_ret == ESP_OK) {
            size_t total = 0, free = 0;
            sd_get_fatfs_usage(&total, &free);
            DBG_INFO("SD 卡挂载成功: %s, 总 %d MB, 可用 %d MB\n",
                     SD_MOUNT_POINT, (int)(total / (1024*1024)), (int)(free / (1024*1024)));
        } else {
            DBG_WARN("SD 卡挂载失败: %s（无卡不影响运行）\n", esp_err_to_name(sd_ret));
        }
    }

    /* ================================================================
     * Phase 2 — 数据/队列初始化
     * ================================================================ */
    oled_display_init();                        /* 创建队列 + 初始显示数据 */
    key_event_group = xEventGroupCreate();      /* 创建按键事件组 */
    DBG_INFO("按键事件组已创建\n");

    /* ================================================================
     * Phase 3 — 统一创建所有任务
     * ================================================================ */
    create_all_tasks();

    /* ================================================================
     * Phase 4 — WiFi Station 连接
     * ================================================================ */
    wifi_init_sta();

    // printf("keytask 剩余栈: %u 字节\n", uxTaskGetStackHighWaterMark(xTaskGetHandle("keytask")));
    // printf("ledtask  剩余栈: %u 字节\n", uxTaskGetStackHighWaterMark(xTaskGetHandle("ledtask")));

    while(1)
    {
        // queue_demo_main();
        vTaskDelay(1000);
    }
}
