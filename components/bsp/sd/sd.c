#include "sd.h"
#include "debug.h"                                     /* DBG_INFO / DBG_WARN */

/* SD 卡 SPI 引脚定义（ESP32-S3 普中板） */
#define SD_SPI_CLK     GPIO_NUM_39
#define SD_SPI_MOSI    GPIO_NUM_38
#define SD_SPI_MISO    GPIO_NUM_40
#define SD_SPI_CS      GPIO_NUM_1

static sdmmc_card_t *s_card = NULL;

esp_err_t sd_spi_init(void)
{
    esp_err_t ret;

    /* ================================================================
     * 1. 初始化 SPI2 总线
     * ================================================================ */
    spi_bus_config_t bus_cfg = {
        .miso_io_num     = SD_SPI_MISO,
        .mosi_io_num     = SD_SPI_MOSI,
        .sclk_io_num     = SD_SPI_CLK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = 4000,
    };
    ret = spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        DBG_WARN("SD 卡 SPI 总线初始化失败: %s\n", esp_err_to_name(ret));
        return ret;
    }

    /* ================================================================
     * 2. 挂载 FAT 文件系统
     * ================================================================ */
    esp_vfs_fat_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files              = 5,
        .allocation_unit_size   = 4 * 1024,
    };

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_PROBING;          /* 先用低速（400kHz）试探 */

    sdspi_device_config_t slot_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_cfg.gpio_cs   = SD_SPI_CS;
    slot_cfg.host_id   = host.slot;

    ret = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot_cfg, &mount_cfg, &s_card);
    if (ret == ESP_OK) {
        size_t total = 0, free = 0;
        sd_get_fatfs_usage(&total, &free);
        DBG_INFO("SD 卡挂载成功: %s, 总 %d MB, 可用 %d MB\n",
                 SD_MOUNT_POINT, (int)(total / (1024*1024)), (int)(free / (1024*1024)));
    } else {
        DBG_WARN("SD 卡挂载失败: %s（无卡不影响运行）\n", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t sd_deinit(void)
{
    esp_err_t ret = esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, s_card);
    s_card = NULL;
    return ret;
}

void sd_get_fatfs_usage(size_t *out_total_bytes, size_t *out_free_bytes)
{
    FATFS *fs;
    DWORD free_clusters;
    int res = f_getfree("0:", &free_clusters, &fs);
    if (res != FR_OK) {
        if (out_total_bytes) *out_total_bytes = 0;
        if (out_free_bytes)  *out_free_bytes  = 0;
        return;
    }

    DWORD total_sectors = (fs->n_fatent - 2) * fs->csize;
    DWORD free_sectors  = free_clusters * fs->csize;

    if (out_total_bytes) *out_total_bytes = total_sectors * fs->ssize;
    if (out_free_bytes)  *out_free_bytes  = free_sectors  * fs->ssize;
}
