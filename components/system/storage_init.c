#include "storage_init.h"
#include "esp_spiffs.h"
#include "debug.h"

/**
 * @brief 挂载 SPIFFS 文件系统（storage 分区）
 *
 * 挂载点 /spiffs，用于存储 WiFi/MQTT 配置、用户设置、传感器校准数据。
 * 挂载失败时自动格式化（首次启动 / 崩溃后自救）。
 *
 * @return ESP_OK 成功，否则错误码
 */
esp_err_t storage_spiffs_init(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "storage",
        .max_files = 5,
        .format_if_mount_failed = true,
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret == ESP_OK) {
        size_t total = 0, used = 0;
        esp_spiffs_info("storage", &total, &used);
        DBG_INFO("SPIFFS 挂载成功: %s 分区, 总 %d KB, 已用 %d KB\n",
                 conf.partition_label, total / 1024, used / 1024);
    } else {
        DBG_WARN("SPIFFS 挂载失败: %s\n", esp_err_to_name(ret));
    }

    return ret;
}


