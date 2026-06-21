#ifndef _BSP_H
#define _BSP_H

#include "debug.h"
#include "led.h"
#include "key.h"
// #include "oled.h"            /* 已替换为 TFT LCD */
// #include "oled_display.h"    /* 已替换为 TFT LCD */
#include "dht11.h"
#include "ds18b20.h"
#include "sensor_init.h"
#include "sd.h"
#include "touch_lcd_init.h"

/**
 * @brief 一次性初始化所有板载外设
 *
 * 调用顺序：LED → KEY → 传感器 → RGB → 音频 → SD 卡 → TFT LCD → TWDT
 * 应在 app_main 开头、任务创建之前调用。
 */
void bsp_init(void);

#endif
