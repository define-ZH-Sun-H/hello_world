/**
 ****************************************************************************************************
 * @file        oled.h
 * @author      普中科技
 * @version     V1.0
 * @date        2024-03-01
 * @brief       OLED驱动代码
 * @license     Copyright (c) 2024-2034, 深圳市普中科技有限公司
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:普中科技 ESP32-S3 开发板
 * 在线视频:https://space.bilibili.com/2146492485
 * 技术论坛:www.prechin.net
 * 公司网址:www.prechin.cn
 * 购买地址:
 *
 ****************************************************************************************************
 */
 
#ifndef __OLED_H__
#define __OLED_H__

#include "iic.h"
#include "driver/gpio.h"



#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
  OLED_PIN_RESET = 0u,
  OLED_PIN_SET
} OLED_PinState;

#define OLED_ADDR       0X3C    /* OLED地址 */
#define OLED_CMD        0x00    /* 写命令 */
#define OLED_DATA       0x40    /* 写数据 */


/* 函数声明 */
void oled_init(i2c_obj_t self);                                                     /* 初始化OLED */
void oled_on(void);                                                                 /* 打开OLED */
void oled_off(void);                                                                /* 关闭OLED */
void oled_clear(void);                                                              /* 清屏（清缓冲+刷新） */
void oled_clear_gram(void);                                                         /* 只清显存缓冲，不刷新（避免闪烁） */
void oled_draw_point(uint8_t x, uint8_t y, uint8_t dot);                            /* OLED画点 */
void oled_fill(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, uint8_t dot);        /* OLED填充区域填充 */
void oled_show_char(uint8_t x, uint8_t y, uint8_t chr, uint8_t size, uint8_t mode); /* 在指定位置显示一个字符,包括部分字符  */
uint32_t oled_pow(uint8_t m, uint8_t n);                                     /* 平方函数, m^n */
void oled_show_num(uint8_t x, uint8_t y, uint32_t num, uint8_t len, uint8_t size);  /* 显示len个数字 */
void oled_show_string(uint8_t x, uint8_t y, const char *p, uint8_t size);           /* 显示字符串 */
void oled_refresh_gram(void);
void oled_draw_bitmap(uint8_t x, uint8_t y, uint8_t w, uint8_t h, const uint8_t *data);

#endif

#ifdef  __cplusplus
}

#endif /*  __cplusplus */
