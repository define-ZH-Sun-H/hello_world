#include "storage_init.h"
#include "esp_spiffs.h"
#include "debug.h"
#include "sd.h"

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

void storage_sd_init(void)
{
    esp_err_t sd_ret = sd_spi_init();
    if (sd_ret == ESP_OK) {
        size_t total = 0, free = 0;
        sd_get_fatfs_usage(&total, &free);
        DBG_INFO("SD 卡挂载成功: %s, 总 %d MB, 可用 %d MB\n",
                 SD_MOUNT_POINT, (int)(total / (1024*1024)), (int)(free / (1024*1024)));
    } else {
        DBG_WARN("SD 卡挂载失败: %s（无卡不影响运行）\n", esp_err_to_name(sd_ret));
    }
}

void storage_init_all(void)
{
    storage_spiffs_init();
    storage_sd_init();
}
