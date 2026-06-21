/**
 * @file sntp.c
 * @brief SNTP 校时实现
 *
 * 从 mqtt.c 独立拆分，职责单一：只做 NTP 时间同步。
 * 通过 sntp_start() 启动，sntp_is_synced() 查询状态。
 */

#include <stdbool.h>
#include <time.h>
#include "esp_log.h"
#include "esp_sntp.h"

static const char *TAG = "sntp";

/* 同步状态标记（由回调写入） */
static bool s_time_synced = false;

/**
 * @brief SNTP 时间同步完成回调
 *
 * lwIP SNTP 内部在收到 NTP 回复后调用。
 * 运行在 tcpip_task 上下文，只做标记不做阻塞操作。
 *
 * @param tv 同步后的系统时间（未使用）
 */
static void on_time_synced(struct timeval *tv)
{
    s_time_synced = true;
    ESP_LOGI(TAG, "时间同步成功");
}

void sntp_start(void)
{
    ESP_LOGI(TAG, "启动 SNTP 校时...");

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_set_time_sync_notification_cb(on_time_synced);

    setenv("TZ", "CST-8", 1);
    tzset();

    esp_sntp_init();
}

bool sntp_is_synced(void)
{
    return s_time_synced;
}
