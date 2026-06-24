/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_ota_ops.h"

#include "bsp.h"
#include "system.h"
#include "network.h"
#include "app.h"
#include "ota.h"        /* OTA 定时检查 */

void app_main(void)
{
    printf("[MAIN] 普中 ESP32-S3 + ST7789 + LVGL 启动 [版本: %s]\n", FIRMWARE_VERSION);

#if defined(CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE)
    /* 确认当前固件有效，防止 bootloader 在 PENDING_VERIFY 状态触发回滚 */
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    if (running && esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
                ESP_LOGI("ota", "固件已验证，取消回滚");
            } else {
                ESP_LOGE("ota", "取消回滚失败");
            }
        }
    }
#endif

    /* ================================================================
     * Phase 1 — 板级硬件初始化（含触控彩屏）
     * ================================================================ */
    bsp_init();                     /* LED → KEY → 传感器 → RGB → 音频 → SD → TFT LCD → WDT */

    /* ================================================================
     * Phase 2 — 系统层初始化（NVS + SPIFFS）
     * ================================================================ */
    system_init();                  /* NVS 闪存 + SPIFFS 文件系统 */

    /* ================================================================
     * Phase 3 — 网络栈初始化（WiFi → MQTT → SNTP，全部非阻塞）
     * ================================================================ */
    network_init();                 /* 加载 AP 列表 → 连接 WiFi → MQTT → SNTP */

    /* ================================================================
     * Phase 3.5 — 启动 OTA 定时检查（依赖 WiFi，放在 network_init 之后）
     * ================================================================ */
    ota_periodic_check_start();

    /* ================================================================
     * Phase 4 — 创建所有 FreeRTOS 任务
     * ================================================================ */
    app_start_tasks();              /* sys_ctrl、LVGL、传感器采集、MQTT */
}
