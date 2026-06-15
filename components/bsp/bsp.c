/**
 * @file bsp.c
 * @brief 板级支持包 — 统一硬件初始化
 *
 * 提供 bsp_init() 一次性完成所有板载外设的初始化，
 * 按依赖顺序依次调用各驱动模块的 init 函数。
 */

#include "bsp.h"
#include "iic.h"
#include "rgb.h"
#include "audio.h"
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
 *   LED → KEY → I2C → OLED → 传感器 → RGB → 音频 → WDT
 * ================================================================ */

void bsp_init(void)
{
    i2c_obj_t oled_dev;

    led_init();                                 /* LED GPIO */
    key_init();                                 /* 按键 GPIO + ISR */
    oled_dev = iic_init(I2C_NUM_1);             /* I2C 总线 */
    oled_init(oled_dev);                        /* OLED 控制器 */
    sensor_init();                              /* DS18B20 + DHT11 */
    rgb_init();                                 /* WS2812 RMT */
    audio_init();                               /* LMD2718 + NS4168 (I2S) */
    wdt_initial();                              /* TWDT 15s */
}
