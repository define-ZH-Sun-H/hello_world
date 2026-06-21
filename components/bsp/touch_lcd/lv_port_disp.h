/**
 * @file lv_port_disp.h
 * @brief LVGL 显示移植层 — 使用 esp_lcd_panel_draw_bitmap 刷屏
 *
 * 双缓冲 + DMA 完成回调：flush_ready 仅在 SPI DMA 传输完成后才发出，
 * 避免 LVGL 在 DMA 尚未读完缓冲区时就写入新数据。
 */
#ifndef LV_PORT_DISP_H
#define LV_PORT_DISP_H

#include "esp_lcd_types.h"
#include "esp_lcd_panel_io.h"

/**
 * @brief 初始化 LVGL 显示驱动
 * @param panel 已初始化的 esp_lcd_panel_handle_t
 * @param io    panel 对应的 IO 句柄（用于注册 DMA 完成回调）
 */
void lv_port_disp_init(esp_lcd_panel_handle_t panel,
                       esp_lcd_panel_io_handle_t io);

#endif /* LV_PORT_DISP_H */
