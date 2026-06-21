/**
 * @file touch_lcd_init.h
 * @brief 触控彩屏硬件初始化（ST7789 + XPT2046）
 *
 * 硬件初始化（GPIO、SPI、面板、触摸）独立为 touch_lcd_init()，
 * 在 bsp_init() 中调用，不依赖 LVGL 内核。
 * 初始化成功后，LVGL 任务通过 extern 句柄访问硬件。
 */
#ifndef TOUCH_LCD_INIT_H
#define TOUCH_LCD_INIT_H

#include "esp_lcd_panel_io.h"
#include "esp_lcd_types.h"
#include "driver/spi_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 触控彩屏硬件句柄（touch_lcd_init 设置，供 LVGL 移植层使用） */
extern esp_lcd_panel_io_handle_t touch_lcd_io_handle;
extern esp_lcd_panel_handle_t touch_lcd_panel_handle;
extern spi_device_handle_t touch_lcd_tp_spi;

/**
 * @brief 触控彩屏硬件初始化
 *
 * GPIO → SPI 总线 → ST7789 LCD → 清屏 → XPT2046 触摸
 * 初始化成功后可通过 touch_lcd_* 句柄访问硬件。
 * 必须在 lvgl_app_start() 之前调用。
 */
void touch_lcd_init(void);

#ifdef __cplusplus
}
#endif

#endif /* TOUCH_LCD_INIT_H */
