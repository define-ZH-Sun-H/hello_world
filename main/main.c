/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bsp.h"
#include "system.h"
#include "network.h"
#include "app.h"

void app_main(void)
{
    printf("[MAIN] 普中 ESP32-S3 + ST7789 + LVGL 启动\n");

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
     * Phase 4 — 创建所有 FreeRTOS 任务
     * ================================================================ */
    app_start_tasks();              /* sys_ctrl、LVGL、传感器采集、MQTT */
}
