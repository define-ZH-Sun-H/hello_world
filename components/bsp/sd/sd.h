#ifndef _SD_H
#define _SD_H

#include <unistd.h>
#include "esp_vfs_fat.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "sdmmc_cmd.h"

/* 挂载点（VFS 路径） */
#define SD_MOUNT_POINT  "/sdcard"

/* 函数声明 */
esp_err_t sd_spi_init(void);
esp_err_t sd_deinit(void);
void sd_get_fatfs_usage(size_t *out_total_bytes, size_t *out_free_bytes);

#endif
