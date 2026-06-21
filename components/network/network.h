/**
 * @file network.h
 * @brief 网络栈统一初始化
 *
 * 在 system_init() 之后、app_start_tasks() 之前调用。
 * 内部按顺序：WiFi → MQTT → SNTP，全部非阻塞。
 */

#ifndef __NETWORK_H__
#define __NETWORK_H__

#include "wifi.h"
#include "mqtt.h"
#include "sntp.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 统一初始化网络栈
 *
 * 流程：
 *   1. 从 SPIFFS 加载 wifi_ap.json
 *   2. ap_count > 0 → wifi_init_sta() 连接 WiFi
 *   3. mqtt_app_start() 创建 MQTT 任务（内部等 WiFi 就绪）
 *   4. sntp_start() 发起 NTP 请求
 *
 * 若 ap_count == 0，全部跳过。
 */
void network_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __NETWORK_H__ */
