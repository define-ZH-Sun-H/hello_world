/**
 * @file lv_port_indev.h
 * @brief LVGL 触摸输入移植层 — 使用 XPT2046 驱动
 */
#ifndef LV_PORT_INDEV_H
#define LV_PORT_INDEV_H

#include "driver/spi_master.h"

/**
 * @brief 初始化 LVGL 触摸输入驱动
 * @param tp_spi XPT2046 的 SPI 设备句柄
 */
void lv_port_indev_init(spi_device_handle_t tp_spi);

#endif /* LV_PORT_INDEV_H */
