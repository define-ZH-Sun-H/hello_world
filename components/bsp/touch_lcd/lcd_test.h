/**
 * @file lcd_test.h
 * @brief ST7789 最小点亮测试
 *
 * 使用 ESP-IDF 内置 esp_lcd_new_panel_st7789() 驱动，
 * SPI3_HOST（独立于 SD 卡的 SPI2_HOST）。
 * 验证通过后此文件可删除，由正式 lcd_display 模块替代。
 */
#ifndef LCD_TEST_H
#define LCD_TEST_H

void lcd_test_start(void);

#endif /* LCD_TEST_H */
