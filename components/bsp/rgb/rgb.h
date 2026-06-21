#ifndef __RGB_H__
#define __RGB_H__

#include <stdint.h>
#include "esp_err.h"

#define RGB_GPIO           16
#define RGB_RESOLUTION_HZ  10000000

void rgb_init(void);
void rgb_set_brightness(uint8_t percent);    /* 0~100，默认 30 */
void rgb_set_color(uint8_t r, uint8_t g, uint8_t b);
void rgb_set_hsv(uint16_t h, uint8_t s, uint8_t v);
void rgb_clear(void);

/**
 * @brief 启动 RGB 彩虹循环任务
 *
 * 创建 FreeRTOS 任务，每隔 20ms 改变 HSV 色相值。
 * 饱和度和亮度固定为 100%。
 */
void rgb_start_rainbow(void);

#endif
