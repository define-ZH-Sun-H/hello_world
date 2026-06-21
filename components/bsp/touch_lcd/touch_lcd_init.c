/**
 * @file touch_lcd_init.c
 * @brief 触控彩屏引脚初始化（仅 GPIO + SPI 总线）
 *
 * 只做最基础的引脚配置和 SPI 总线初始化。
 * 面板（ST7789）和触摸（XPT2046）的完整初始化由 lvgl_app_start() 完成。
 *
 * 引脚定义与 lcd_test.c 完全一致。
 */
#include "touch_lcd_init.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include <stdio.h>

/* ================================================================
 * 引脚定义
 * ================================================================ */
#define LCD_HOST            SPI3_HOST

#define PIN_NUM_CLK         21
#define PIN_NUM_MOSI        47
#define PIN_NUM_MISO        48
#define PIN_NUM_CS          42
#define PIN_NUM_DC          41
#define PIN_NUM_BL          14

#define PIN_NUM_TP_CS       46
#define PIN_NUM_TP_PEN      2

#define LCD_WIDTH           240
#define LCD_HEIGHT          320

/* ---- 硬件句柄（供 LVGL 移植层使用，由 lvgl_app_start() 赋值） ---- */
esp_lcd_panel_io_handle_t touch_lcd_io_handle = NULL;
esp_lcd_panel_handle_t touch_lcd_panel_handle = NULL;
spi_device_handle_t touch_lcd_tp_spi = NULL;

/* ================================================================
 * touch_lcd_init — 引脚初始化
 *
 * 只做 GPIO 配置 + SPI 总线初始化。
 * ================================================================ */
void touch_lcd_init(void)
{
    /* ---------------------------------------------------------------
     * 1. 触摸 PENIRQ 输入
     * --------------------------------------------------------------- */
    gpio_config_t pen_cfg = {
        .pin_bit_mask = (1ULL << PIN_NUM_TP_PEN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&pen_cfg);

    /* ---------------------------------------------------------------
     * 2. 背光引脚（先关，lvgl_app_start 初始化完后开）
     * --------------------------------------------------------------- */
    gpio_config_t bl_cfg = {
        .pin_bit_mask = (1ULL << PIN_NUM_BL),
        .mode         = GPIO_MODE_OUTPUT,
    };
    gpio_config(&bl_cfg);
    gpio_set_level(PIN_NUM_BL, 0);

    /* ---------------------------------------------------------------
     * 3. SPI 总线初始化（LCD + XPT2046 共用）
     * --------------------------------------------------------------- */
    spi_bus_config_t bus_cfg = {
        .sclk_io_num     = PIN_NUM_CLK,
        .mosi_io_num     = PIN_NUM_MOSI,
        .miso_io_num     = PIN_NUM_MISO,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = 2 * LCD_WIDTH * 40,
    };
    esp_err_t ret = spi_bus_initialize(LCD_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        printf("[TOUCH_LCD] SPI 总线初始化失败: %s\n", esp_err_to_name(ret));
        return;
    }
    printf("[TOUCH_LCD] 引脚 + SPI 总线 OK\n");
}
