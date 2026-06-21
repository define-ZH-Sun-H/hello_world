/**
 * @file lvgl_app.h
 * @brief LVGL 程序 — 面板初始化 + 创建 LVGL 任务
 *
 * 引脚初始化（touch_lcd_init）之后调用本文件。
 * lvgl_app_start() 完成 ST7789 + XPT2046 完整初始化，然后创建 LVGL 任务。
 * 在 app_start_tasks() 中调用。
 */
#ifndef LVGL_APP_H
#define LVGL_APP_H

/**
 * @brief 触控彩屏完整初始化 + 创建 LVGL 任务
 *
 * 非阻塞，初始化面板/触摸硬件后创建 LVGL 任务并返回。
 * 必须在 touch_lcd_init()（SPI 总线就绪）之后调用。
 */
void lvgl_app_start(void);

#endif
