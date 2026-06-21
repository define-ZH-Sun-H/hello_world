/**
 * @file network.c
 * @brief 网络栈统一初始化实现
 */

#include "network.h"
#include "esp_log.h"

static const char *TAG = "network";

void network_init(void)
{
    ESP_LOGI(TAG, "=== 网络栈初始化 ===");

    /* Phase 1: WiFi 连接（内部加载 AP 列表，ap_count==0 时跳过） */
    wifi_init_sta();

    /* Phase 2: MQTT 启动（任务内等 WiFi 就绪，内部无 WiFi 时超时退出） */
    mqtt_app_start();

    /* Phase 3: SNTP 时间同步（需 LwIP 已初始化，无 WiFi 时跳过） */
    if (wifi_is_initialized()) {
        sntp_start();
    }

    ESP_LOGI(TAG, "=== 网络栈初始化完成 ===");
}
