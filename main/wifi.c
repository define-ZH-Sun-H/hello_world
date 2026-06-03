/**
 * @file wifi.c
 * @brief WiFi Station 连接实现
 *
 * ESP32-S3 Station 模式的初始化与自动重连。
 * 事件驱动，无需轮询。
 *
 * 工作流程：
 * ```
 * app_main 调用 wifi_init_sta()
 *   ├─ NVS 初始化
 *   ├─ 网络接口层启动
 *   ├─ WiFi 驱动初始化
 *   ├─ 注册事件回调
 *   ├─ 配置路由器信息
 *   └─ 启动 WiFi
 *
 * WiFi 驱动启动后，事件回调驱动后续流程：
 *   WIFI_EVENT_STA_START       → 调用 esp_wifi_connect() 发起连接
 *   WIFI_EVENT_STA_CONNECTED   → 记录已关联（等待 DHCP）
 *   IP_EVENT_STA_GOT_IP        → 标记 WIFI_CONNECTED_BIT（就绪）
 *   WIFI_EVENT_STA_DISCONNECTED → 清除标记位，自动重连
 * ```
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_log.h"

#include "wifi.h"

/* ================================================================
 * 日志标签
 * ================================================================ */
static const char *TAG = "wifi";

/* ================================================================
 * 事件组定义
 *
 * wifi_event_group 声明在 wifi.h 中（extern），
 * 实现放在此文件中。其他文件通过 include "wifi.h" 访问。
 * ================================================================ */
EventGroupHandle_t wifi_event_group = NULL;

/* ================================================================
 * 事件回调函数（模块内部使用，不对外暴露）
 *
 * 响应 WiFi 驱动和 LwIP 协议栈产生的事件，驱动连接状态机：
 *
 *   WIFI_EVENT_STA_START
 *     └→ 调用 esp_wifi_connect()，发起连接
 *
 *   WIFI_EVENT_STA_CONNECTED
 *     └→ 物理层关联成功，等待 DHCP（什么都不用做）
 *
 *   IP_EVENT_STA_GOT_IP
 *     └→ 获取到 IP 地址 → 置位 WIFI_CONNECTED_BIT
 *         此时网络可用，其他任务可以开始通信
 *
 *   WIFI_EVENT_STA_DISCONNECTED
 *     └→ 连接断开 → 清除 WIFI_CONNECTED_BIT → 自动重连
 *         并打印断开原因码（可用于诊断）：
 *           201 = NO_AP_FOUND（搜不到路由器）
 *           202 = AUTH_FAIL（密码错误）
 *           204 = HANDSHAKE_TIMEOUT（DHCP 超时）
 * ================================================================ */
static void wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    if (base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                /* WiFi 射频已启动，触发连接路由器 */
                ESP_LOGI(TAG, "WiFi 驱动就绪，开始连接");
                esp_wifi_connect();
                break;

            case WIFI_EVENT_STA_CONNECTED:
                /* 已关联到路由器（MAC 层通），等待 DHCP 获取 IP */
                ESP_LOGI(TAG, "WiFi 已连接到路由器");
                break;

            case WIFI_EVENT_STA_DISCONNECTED: {
                /* 连接断开，打印原因并自动重连 */
                wifi_event_sta_disconnected_t *ev =
                    (wifi_event_sta_disconnected_t *)event_data;

                ESP_LOGW(TAG, "WiFi 断开, reason=%d, 准备重连...", ev->reason);

                /* 通知其他任务 WiFi 已不可用 */
                xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);

                /* 自动重连（简单策略：断即重连） */
                esp_wifi_connect();
                break;
            }

            default:
                break;
        }
    } else if (base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            /* LwIP 通过 DHCP 获取到 IP 地址，网络层就绪 */
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;

            ESP_LOGI(TAG, "已获取 IP: " IPSTR, IP2STR(&event->ip_info.ip));

            /* 通知其他任务 WiFi 网络已可用 */
            xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        }
    }
}

/* ================================================================
 * WiFi Station 初始化
 *
 * 按照 ESP-IDF 要求的顺序一步步初始化。
 * 每一步出错都会中断（ESP_ERROR_CHECK），串口会打印出错位置。
 *
 * @note 必须在 WiFi 相关的无线通信开始前调用。
 * @note 如果 NVS 分区格式不匹配，会自动擦除重试。
 * ================================================================ */
void wifi_init_sta(void)
{
    ESP_LOGI(TAG, "初始化 WiFi Station...");

    /* ------------------------------------------------------------
     * 1. NVS 初始化
     *
     * WiFi 驱动需要在 NVS（非易失性存储）中保存运行时参数，
     * 如上次连接的 AP 的 MAC 地址、校准数据等。
     * 必须在 esp_wifi_init() 之前调用。
     *
     * 开发过程中多次烧录可能导致 NVS 分区布局变化，
     * 首次失败时自动擦除重试。
     * ------------------------------------------------------------ */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS 分区需要擦除，正在重试...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS 初始化完成");

    /* ------------------------------------------------------------
     * 2. 初始化网络接口层
     *
     * esp_netif_init()      — 启动 LwIP 协议栈
     * esp_event_loop_create_default() — 创建事件分发任务
     * esp_netif_create_default_wifi_sta() — 注册 Station 网卡
     *
     * 这三步将 ESP32 的 WiFi 硬件注册为系统可识别的网络接口，
     * 并关联 TCP/IP 协议栈和 DHCP 客户端。
     * ------------------------------------------------------------ */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    ESP_LOGI(TAG, "网络接口层初始化完成");

    /* ------------------------------------------------------------
     * 3. WiFi 驱动初始化
     *
     * 使用 WIFI_INIT_CONFIG_DEFAULT() 宏获取默认配置，
     * 包括缓冲区大小、并发连接数等。无需修改。
     * ------------------------------------------------------------ */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_LOGI(TAG, "WiFi 驱动初始化完成");

    /* ------------------------------------------------------------
     * 4. 注册事件处理器
     *
     * 创建事件组用于通知连接状态，注册两个事件回调：
     *   - WIFI_EVENT: 物理层事件（启动/连接/断开）
     *   - IP_EVENT:   网络层事件（获取 IP/丢失 IP）
     *
     * 注意：回调函数中不要做阻塞操作（延时、HTTP 请求等）。
     * 耗时操作应通过事件组通知其他任务处理。
     * ------------------------------------------------------------ */
    wifi_event_group = xEventGroupCreate();
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                               &wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                               &wifi_event_handler, NULL);
    ESP_LOGI(TAG, "事件处理器注册完成");

    /* ------------------------------------------------------------
     * 5. 配置目标路由器
     *
     * 设置 STA 模式、指定 SSID 和密码。
     * 必须在 esp_wifi_start() 之前调用。
     * ------------------------------------------------------------ */
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "Redmi K70E",
            .password = "s13523061092",
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_LOGI(TAG, "路由器配置完成 (SSID: %s)", wifi_config.sta.ssid);

    /* ------------------------------------------------------------
     * 6. 启动 WiFi
     *
     * 启动后，事件回调驱动后续流程：
     *   START → esp_wifi_connect() → CONNECTED → GOT_IP
     * ------------------------------------------------------------ */
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "初始化完成，等待连接...");
}
