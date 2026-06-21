#include "system.h"
#include "nvs_flash.h"
#include "storage_init.h"
#include "esp_log.h"

static const char *TAG = "system";

sys_config_t g_sys_config = {0};

void system_init(void)
{
    ESP_LOGI(TAG, "=== 系统层初始化 ===");

    /* Phase 1: NVS 初始化（WiFi 驱动依赖，分区损坏自动擦除重试） */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS 分区需要擦除，正在重试...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS 初始化成功");

    /* Phase 2: SPIFFS 挂载（WiFi AP 列表存放于此） */
    storage_spiffs_init();

    /* sys_config_t 壳子 —— 后续从 NVS 加载配置时再扩展 */

    ESP_LOGI(TAG, "=== 系统层初始化完成 ===");
}
