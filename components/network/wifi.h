/**
 * @file wifi.h
 * @brief WiFi Station 连接模块
 *
 * 封装 ESP32-S3 Station 模式的初始化与连接管理。
 * 提供事件组通知机制，供其他任务等待 WiFi 就绪。
 *
 * 使用示例：
 * @code
 *     wifi_init_sta();
 *     // 在其他任务中等待连接完成
 *     xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT,
 *                         pdFALSE, pdTRUE, portMAX_DELAY);
 * @endcode
 */

#ifndef WIFI_H
#define WIFI_H

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * 事件组标记位
 *
 * 当 WiFi 获取到 IP 地址后，wifi_event_group 中的 WIFI_CONNECTED_BIT 被置位；
 * 当 WiFi 断开时，该位被清除。其他任务可以等待或查询此位。
 * ================================================================ */
#define WIFI_CONNECTED_BIT      BIT0

/* ================================================================
 * WiFi AP 列表配置
 *
 * 从 SPIFFS /spiffs/wifi_ap.json 加载，支持最多 5 个 AP。
 * JSON 格式：[{"ssid":"...", "pswd":"...", "prio":N}]
 * ================================================================ */
#define WIFI_AP_LIST_MAX  5

typedef struct {
    char     ssid[32];
    char     password[64];
    uint8_t  priority;      /* 0-255，预留排序用 */
} wifi_ap_entry_t;

typedef struct {
    wifi_ap_entry_t ap_list[WIFI_AP_LIST_MAX];
    uint8_t         ap_count;   /* 0 = 跳过 WiFi */
} wifi_network_config_t;

/**
 * @brief WiFi 状态事件组（全局可见）
 *
 * 其他任务通过此事件组等待或检查 WiFi 连接状态：
 * @code
 *     // 等待连接（阻塞直到 WiFi 就绪）
 *     xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT,
 *                         pdFALSE, pdTRUE, portMAX_DELAY);
 *
 *     // 轮询检查
 *     if (xEventGroupGetBits(wifi_event_group) & WIFI_CONNECTED_BIT) {
 *         // WiFi 已连接
 *     }
 * @endcode
 */
extern EventGroupHandle_t wifi_event_group;


/**
 * @brief 初始化并连接 WiFi Station
 *
 * 完整的初始化流程：
 *   1. NVS 初始化（WiFi 驱动内部存储）
 *   2. 网络接口层（esp_netif）
 *   3. 事件循环
 *   4. WiFi 驱动初始化
 *   5. 注册事件处理器
 *   6. 配置路由器 SSID/密码
 *   7. 启动 WiFi（自动连接）
 *
 * 调用后，WiFi 会在后台自动连接和重连，无需额外处理。
 * 通过 wifi_event_group 获知连接状态。
 *
 * @return void
 */
void wifi_init_sta(void);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_H */
