#include "sys_info.h"
#include "esp_chip_info.h"
#include "sdkconfig.h"
#include "debug.h"

/**
 * @brief 打印芯片信息（启动时调用一次）
 *
 * 输出芯片型号、CPU 核心数、支持的功能（WiFi/BT/BLE/802.15.4）。
 * 用于系统启动时的硬件确认。
 *
 * @return void
 */
void print_chip_info(void)
{
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    DBG_INFO("This is %s chip with %d CPU core(s), %s%s%s%s, ",
           CONFIG_IDF_TARGET,
           chip_info.cores,
           (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi/" : "",
           (chip_info.features & CHIP_FEATURE_BT) ? "BT" : "",
           (chip_info.features & CHIP_FEATURE_BLE) ? "BLE" : "",
           (chip_info.features & CHIP_FEATURE_IEEE802154) ? ", 802.15.4 (Zigbee/Thread)" : "");
}
