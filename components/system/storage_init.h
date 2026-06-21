#ifndef __STORAGE_INIT_H
#define __STORAGE_INIT_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 挂载 SPIFFS 文件系统（storage 分区）
 *
 * 挂载点 /spiffs，用于存储 WiFi/MQTT 配置、用户设置、传感器校准数据。
 * 挂载失败时自动格式化（首次启动 / 崩溃后自救）。
 *
 * @return ESP_OK 成功，否则错误码
 */
esp_err_t storage_spiffs_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __STORAGE_INIT_H */
