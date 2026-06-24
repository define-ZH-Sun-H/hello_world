/**
 * @file bsp.c
 * @brief 板级支持包 — 统一硬件初始化
 *
 * 提供 bsp_init() 一次性完成所有板载外设的初始化，
 * 按依赖顺序依次调用各驱动模块的 init 函数。
 */

#include "bsp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "iic.h"
#include "rgb.h"
#include "audio.h"
#include "touch_lcd_init.h"
#include "esp_task_wdt.h"

/* ================================================================
 * TWDT 初始化
 *
 * 在任务创建前配置看门狗，防止启动阶段长耗时操作触发复位。
 * timeout=15s，双核均需喂狗。
 * ================================================================ */

static void wdt_initial(void)
{
    esp_task_wdt_config_t twdt_config = {
        .timeout_ms = 15000,
        .idle_core_mask = (1 << 0) | (1 << 1),
        .trigger_panic = true,
    };
    esp_task_wdt_init(&twdt_config);
}

/* ================================================================
 * 公开接口 — 硬件初始化
 *
 * 调用顺序按外设启动依赖排列：
 *   LED → KEY → I2C → [OLED → 已替换为 TFT LCD] → 传感器 → RGB → 音频 → SD → TFT LCD → WDT
 * ================================================================ */

void bsp_init(void)
{
    wdt_initial();                              /* TWDT 15s（先配，防后续 init 阻塞无保护） */
    key_init();                                 /* 按键 GPIO + ISR */
    // led_init();                                 /* LED GPIO */
    // sensor_init();                              /* DS18B20 + DHT11 */
    // rgb_init();                                 /* WS2812 RMT */
    audio_init();                               /* LMD2718 + NS4168 (I2S) */
    vTaskDelay(pdMS_TO_TICKS(50));              /* 稳定电源，防止 brownout（I2S+RGB+SD 连续大电流） */
    sd_spi_init();                              /* SD 卡（SPI 模式，无卡不阻塞） */
    touch_lcd_init();                           /* 触控彩屏：引脚 + SPI 总线初始化（面板初始化在 lvgl_app_start） */
}
