/**
 * @file lv_port_indev.c
 * @brief LVGL 触摸输入移植 — 封装 XPT2046 驱动为 lv_indev_drv_t
 */
#include "lv_port_indev.h"
#include "lvgl.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"

/* 引脚定义（与 lcd_test.c 保持一致） */
#define PIN_NUM_TP_CS    46
#define PIN_NUM_TP_PEN   2

#define LCD_WIDTH  240
#define LCD_HEIGHT 320

/* XPT2046 命令 */
#define XPT_CMD_X  0xD0
#define XPT_CMD_Y  0x90

/* 全局 SPI 设备句柄，在 read_cb 中使用 */
static spi_device_handle_t g_tp_spi = NULL;

/* 内部函数声明 */
static void touch_read(lv_indev_drv_t *drv, lv_indev_data_t *data);
static bool xpt2046_is_touched(void);
static uint16_t xpt2046_read_adc(uint8_t cmd);
static bool xpt2046_read_xy(uint16_t *x, uint16_t *y);

/* ================================================================
 * 公开接口
 * ================================================================ */
void lv_port_indev_init(spi_device_handle_t tp_spi)
{
    g_tp_spi = tp_spi;

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);

    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_read;

    lv_indev_drv_register(&indev_drv);
}

/* ================================================================
 * touch_read — LVGL 轮询此回调获取触摸状态
 *
 * 设置 data->state = LV_INDEV_STATE_PR（按下）或 _REL（松开）
 * 设置 data->point.x / .y 为触摸坐标
 * ================================================================ */
static void touch_read(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    uint16_t x, y;
    if (xpt2046_read_xy(&x, &y)) {
        data->point.x = x;
        data->point.y = y;
        data->state   = LV_INDEV_STATE_PR;
    } else {
        data->state   = LV_INDEV_STATE_REL;
    }
}

/* ================================================================
 * XPT2046 驱动（精简版，和 lcd_test.c 一致）
 * ================================================================ */

static bool xpt2046_is_touched(void)
{
    return gpio_get_level(PIN_NUM_TP_PEN) == 0;
}

static uint16_t xpt2046_read_adc(uint8_t cmd)
{
    uint8_t tx_buf[3] = { cmd, 0, 0 };
    uint8_t rx_buf[3] = { 0 };

    spi_transaction_t t = {
        .length    = 24,
        .tx_buffer = tx_buf,
        .rx_buffer = rx_buf,
    };

    if (spi_device_polling_transmit(g_tp_spi, &t) != ESP_OK) {
        return 0xFFFF;
    }

    uint16_t combined = ((uint16_t)rx_buf[1] << 8) | rx_buf[2];
    return (combined >> 3) & 0x0FFF;
}

/**
 * 读取触摸坐标（5 次采样平均），映射到屏幕像素
 */
static bool xpt2046_read_xy(uint16_t *x, uint16_t *y)
{
    if (!g_tp_spi || !xpt2046_is_touched()) {
        return false;
    }

    esp_rom_delay_us(1000);  /* 等待信号稳定 */

#define TP_SAMPLES  5
    uint32_t sum_x = 0, sum_y = 0;

    for (int i = 0; i < TP_SAMPLES; i++) {
        sum_x += xpt2046_read_adc(XPT_CMD_X);
        sum_y += xpt2046_read_adc(XPT_CMD_Y);
        esp_rom_delay_us(200);
    }

    uint16_t rxx = sum_x / TP_SAMPLES;
    uint16_t ryy = sum_y / TP_SAMPLES;

    /* 原始值 0~4095 → 屏幕坐标 */
    *x = ((uint32_t)rxx * LCD_WIDTH) / 4096;
    *y = ((uint32_t)ryy * LCD_HEIGHT) / 4096;

    return true;
#undef TP_SAMPLES
}
