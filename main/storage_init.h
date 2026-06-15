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

/**
 * @brief 挂载 SD 卡（SPI 模式）
 *
 * 挂载点 /sdcard，用于存储大文件（录音、日志、固件等）。
 * 无卡时不阻塞，仅打印警告。
 */
void storage_sd_init(void);

/**
 * @brief 一键挂载所有存储设备
 *
 * 依次调用 SPIFFS → SD 卡，各自独立处理失败。
 * SPIFFS 优先（配置文件需要早加载），SD 卡非阻塞。
 */
void storage_init_all(void);

#ifdef __cplusplus
}
#endif

#endif /* __STORAGE_INIT_H */
