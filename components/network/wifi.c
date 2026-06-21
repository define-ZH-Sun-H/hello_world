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
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "cJSON.h"

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

/* WiFi AP 列表（从 SPIFFS wifi_ap.json 加载） */
static wifi_network_config_t s_wifi_config = {0};

/* WiFi 是否已实际初始化（LwIP 栈可用） */
static bool s_wifi_inited = false;

/* ================================================================
 * WiFi AP 列表加载
 *
 * 从 SPIFFS 读取 wifi_ap.json，解析到 s_wifi_config.ap_list[]。
 * 文件不存在 / 解析失败 → ap_count = 0。
 * ssid 必填，缺了跳过该项；pswd 和 prio 可选。
 * ================================================================ */
/**
 * @brief 从 SPIFFS 加载 WiFi AP 列表
 *
 * @return ESP_OK 成功，ESP_ERR_NOT_FOUND 文件不存在，
 *         ESP_ERR_INVALID_ARG 解析失败
 */
static esp_err_t wifi_ap_list_load(void)
{
    FILE *f = fopen("/spiffs/wifi_ap.json", "r");
    if (!f) {
        ESP_LOGW(TAG, "wifi_ap.json 不存在，跳过 WiFi");
        return ESP_ERR_NOT_FOUND;
    }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *json = malloc(len + 1);
    if (!json) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }
    fread(json, 1, len, f);
    fclose(f);
    json[len] = '\0';

    cJSON *root = cJSON_Parse(json);
    if (!root || !cJSON_IsArray(root)) {
        ESP_LOGW(TAG, "wifi_ap.json 解析失败");
        free(json);
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    s_wifi_config.ap_count = 0;
    int n = cJSON_GetArraySize(root);
    for (int i = 0; i < n && i < WIFI_AP_LIST_MAX; i++) {
        cJSON *item = cJSON_GetArrayItem(root, i);
        cJSON *ssid = cJSON_GetObjectItem(item, "ssid");
        cJSON *pswd = cJSON_GetObjectItem(item, "pswd");
        cJSON *prio = cJSON_GetObjectItem(item, "prio");

        if (!cJSON_IsString(ssid)) continue;

        wifi_ap_entry_t *ap = &s_wifi_config.ap_list[s_wifi_config.ap_count];
        strncpy(ap->ssid, ssid->valuestring, sizeof(ap->ssid) - 1);
        ap->ssid[sizeof(ap->ssid) - 1] = '\0';

        if (cJSON_IsString(pswd)) {
            strncpy(ap->password, pswd->valuestring, sizeof(ap->password) - 1);
            ap->password[sizeof(ap->password) - 1] = '\0';
        } else {
            ap->password[0] = '\0';     /* 开放网络 */
        }

        ap->priority = cJSON_IsNumber(prio) ? (uint8_t)prio->valueint : 0;
        s_wifi_config.ap_count++;
    }

    cJSON_Delete(root);
    free(json);

    ESP_LOGI(TAG, "已加载 %d 个 WiFi AP", s_wifi_config.ap_count);
    return ESP_OK;
}

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
/**
 * @brief WiFi 事件回调
 *
 * 响应 WiFi 驱动和 LwIP 协议栈产生的事件，驱动连接状态机。
 * 处理的事件：STA_START（发起连接）、STA_CONNECTED（关联成功）、
 * STA_DISCONNECTED（清理标记+自动重连）、GOT_IP（置位就绪标记）。
 *
 * @note 回调中不要做阻塞操作，耗时操作应通过事件组通知其他任务处理。
 *
 * @param arg        用户自定义参数（未使用）
 * @param base       事件基类（WIFI_EVENT 或 IP_EVENT）
 * @param event_id   事件 ID
 * @param event_data 事件数据结构体指针
 */
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
/**
 * @brief 初始化并连接 WiFi Station
 *
 * 完整的初始化流程：
 * 1. NVS 初始化（WiFi 驱动内部存储）
 * 2. 网络接口层（esp_netif）+ 事件循环
 * 3. WiFi 驱动初始化 + 事件处理注册
 * 4. 配置路由器 SSID/密码
 * 5. 启动 WiFi（连接由事件回调自动驱动）
 *
 * 调用后 WiFi 在后台自动连接和重连，无需额外处理。
 * 其他任务通过 wifi_event_group 获知连接状态。
 *
 * @return void
 */
void wifi_init_sta(void)
{
    /* 加载 AP 列表（从 SPIFFS wifi_ap.json） */
    wifi_ap_list_load();

    /* 无论是否有 AP，都先创建事件组，防止其他任务在 NULL 上等待 */
    wifi_event_group = xEventGroupCreate();

    if (s_wifi_config.ap_count == 0) {
        ESP_LOGW(TAG, "AP 列表为空，跳过 WiFi 初始化");
        return;
    }

    ESP_LOGI(TAG, "初始化 WiFi Station (SSID: %s)...",
             s_wifi_config.ap_list[0].ssid);

    /* ------------------------------------------------------------
     * 1. 初始化网络接口层
     *
     * NVS 已由 system_init() 统一初始化，此处不再重复。
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
     * 2. WiFi 驱动初始化
     *
     * 使用 WIFI_INIT_CONFIG_DEFAULT() 宏获取默认配置，
     * 包括缓冲区大小、并发连接数等。无需修改。
     * ------------------------------------------------------------ */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_LOGI(TAG, "WiFi 驱动初始化完成");

    /* ------------------------------------------------------------
     * 3. 注册事件处理器
     *
     * 事件组已在函数开头创建。
     * 注册两个事件回调：
     *   - WIFI_EVENT: 物理层事件（启动/连接/断开）
     *   - IP_EVENT:   网络层事件（获取 IP/丢失 IP）
     *
     * 注意：回调函数中不要做阻塞操作（延时、HTTP 请求等）。
     * 耗时操作应通过事件组通知其他任务处理。
     * ------------------------------------------------------------ */
    s_wifi_inited = true;
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                               &wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                               &wifi_event_handler, NULL);
    ESP_LOGI(TAG, "事件处理器注册完成");

    /* ------------------------------------------------------------
     * 4. 配置 AP（从 SPIFFS 加载的 AP 列表，取第一个）
     *
     * 后续版本可循环尝试列表中多个 AP，或按 priority / RSSI 排序。
     * ------------------------------------------------------------ */
    wifi_config_t wifi_cfg = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    strncpy((char *)wifi_cfg.sta.ssid,
            s_wifi_config.ap_list[0].ssid,
            sizeof(wifi_cfg.sta.ssid) - 1);
    strncpy((char *)wifi_cfg.sta.password,
            s_wifi_config.ap_list[0].password,
            sizeof(wifi_cfg.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_LOGI(TAG, "路由器配置完成 (SSID: %s)", wifi_cfg.sta.ssid);

    /* ------------------------------------------------------------
     * 5. 启动 WiFi
     *
     * 启动后，事件回调驱动后续流程：
     *   START → esp_wifi_connect() → CONNECTED → GOT_IP
     * ------------------------------------------------------------ */
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "初始化完成，等待连接...");
}

bool wifi_is_initialized(void)
{
    return s_wifi_inited;
}
