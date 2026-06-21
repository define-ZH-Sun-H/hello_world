/**
 * @file lcd_test.c
 * @brief ST7789 + XPT2046 触摸测试
 *
 * Phase 1: 点亮 ST7789（纯色 + 彩条 + 文字）
 * Phase 2: 切换到触摸测试（显示触摸坐标 + 十字光标）
 *
 * XPT2046 驱动直接手写 SPI 事务，不依赖外部组件。
 */
#include "lcd_test.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_lcd_panel_ops.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* ================================================================
 * 引脚定义（普中-ESP32S3 开发板）
 * ================================================================ */
#define LCD_HOST            SPI3_HOST

/* ST7789（写操作不需要 MISO，但 XPT2046 需要） */
#define PIN_NUM_CLK         21
#define PIN_NUM_MOSI        47
#define PIN_NUM_MISO        48          /* XPT2046 读坐标需要 MISO */
#define PIN_NUM_CS          42
#define PIN_NUM_DC          41
#define PIN_NUM_BL          14

/* XPT2046 触摸 */
#define PIN_NUM_TP_CS       46          /* XPT2046 片选 */
#define PIN_NUM_TP_PEN      2           /* XPT2046 PENIRQ（低电平=有触摸） */

/* ================================================================
 * LCD 参数
 * ================================================================ */
#define LCD_WIDTH           240
#define LCD_HEIGHT          320
#define SPI_CLOCK_HZ        (27 * 1000 * 1000)     /* 27 MHz */

/* XPT2046 SPI 时钟（2.5MHz 以内即可） */
#define TP_SPI_CLOCK_HZ     (2 * 1000 * 1000)

/* 通用宏 */
#define MAX(a, b)  (((a) > (b)) ? (a) : (b))
#define MIN(a, b)  (((a) < (b)) ? (a) : (b))

/* RGB565 颜色宏 */
#define RGB565(r, g, b) ((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3))
#define COLOR_RED       RGB565(255, 0, 0)
#define COLOR_GREEN     RGB565(0, 255, 0)
#define COLOR_BLUE      RGB565(0, 0, 255)
#define COLOR_WHITE     RGB565(255, 255, 255)
#define COLOR_BLACK     RGB565(0, 0, 0)
#define COLOR_CYAN      RGB565(0, 255, 255)
#define COLOR_YELLOW    RGB565(255, 255, 0)
#define COLOR_MAGENTA   RGB565(255, 0, 255)

#define FILL_BAND_H     40      /* 绘制分块高度（字节数 = 240×40×2） */

/* ================================================================
 * 8×8 ASCII 字库（public domain）
 * 索引: font8x8[ch - 32]，ch ∈ [32, 126]
 * ================================================================ */
static const uint8_t font8x8[95][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /*   */
    {0x08,0x08,0x08,0x08,0x08,0x00,0x08,0x00}, /* ! */
    {0x14,0x14,0x14,0x00,0x00,0x00,0x00,0x00}, /* " */
    {0x24,0x24,0x7E,0x24,0x7E,0x24,0x24,0x00}, /* # */
    {0x1C,0x2A,0x28,0x1C,0x0A,0x2A,0x1C,0x00}, /* $ */
    {0x00,0x62,0x64,0x08,0x10,0x26,0x46,0x00}, /* % */
    {0x18,0x24,0x18,0x30,0x4C,0x44,0x3A,0x00}, /* & */
    {0x08,0x08,0x08,0x00,0x00,0x00,0x00,0x00}, /* ' */
    {0x04,0x08,0x10,0x10,0x10,0x08,0x04,0x00}, /* ( */
    {0x10,0x08,0x04,0x04,0x04,0x08,0x10,0x00}, /* ) */
    {0x00,0x08,0x2A,0x1C,0x2A,0x08,0x00,0x00}, /* * */
    {0x00,0x08,0x08,0x3E,0x08,0x08,0x00,0x00}, /* + */
    {0x00,0x00,0x00,0x00,0x00,0x08,0x08,0x10}, /* , */
    {0x00,0x00,0x00,0x3E,0x00,0x00,0x00,0x00}, /* - */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x08,0x00}, /* . */
    {0x02,0x02,0x04,0x08,0x10,0x20,0x20,0x00}, /* / */
    {0x3C,0x46,0x4A,0x52,0x62,0x3C,0x00,0x00}, /* 0 */
    {0x08,0x18,0x28,0x08,0x08,0x3E,0x00,0x00}, /* 1 */
    {0x3C,0x42,0x02,0x0C,0x30,0x7E,0x00,0x00}, /* 2 */
    {0x3C,0x42,0x04,0x02,0x42,0x3C,0x00,0x00}, /* 3 */
    {0x0C,0x14,0x24,0x7E,0x04,0x04,0x00,0x00}, /* 4 */
    {0x7E,0x40,0x7C,0x02,0x42,0x3C,0x00,0x00}, /* 5 */
    {0x3C,0x40,0x7C,0x42,0x42,0x3C,0x00,0x00}, /* 6 */
    {0x7E,0x02,0x04,0x08,0x10,0x10,0x00,0x00}, /* 7 */
    {0x3C,0x42,0x3C,0x42,0x42,0x3C,0x00,0x00}, /* 8 */
    {0x3C,0x42,0x42,0x3E,0x02,0x3C,0x00,0x00}, /* 9 */
    {0x00,0x00,0x08,0x00,0x00,0x08,0x00,0x00}, /* : */
    {0x00,0x00,0x08,0x00,0x00,0x08,0x08,0x10}, /* ; */
    {0x04,0x08,0x10,0x20,0x10,0x08,0x04,0x00}, /* < */
    {0x00,0x00,0x3E,0x00,0x3E,0x00,0x00,0x00}, /* = */
    {0x10,0x08,0x04,0x02,0x04,0x08,0x10,0x00}, /* > */
    {0x3C,0x42,0x04,0x08,0x08,0x00,0x08,0x00}, /* ? */
    {0x3C,0x4A,0x56,0x5E,0x40,0x3C,0x00,0x00}, /* @ */
    {0x18,0x24,0x42,0x7E,0x42,0x42,0x00,0x00}, /* A */
    {0x7C,0x42,0x7C,0x42,0x42,0x7C,0x00,0x00}, /* B */
    {0x3C,0x42,0x40,0x40,0x42,0x3C,0x00,0x00}, /* C */
    {0x78,0x44,0x42,0x42,0x44,0x78,0x00,0x00}, /* D */
    {0x7E,0x40,0x7C,0x40,0x40,0x7E,0x00,0x00}, /* E */
    {0x7E,0x40,0x7C,0x40,0x40,0x40,0x00,0x00}, /* F */
    {0x3C,0x42,0x40,0x4E,0x42,0x3C,0x00,0x00}, /* G */
    {0x42,0x42,0x7E,0x42,0x42,0x42,0x00,0x00}, /* H */
    {0x3E,0x08,0x08,0x08,0x08,0x3E,0x00,0x00}, /* I */
    {0x02,0x02,0x02,0x42,0x42,0x3C,0x00,0x00}, /* J */
    {0x44,0x48,0x70,0x48,0x44,0x42,0x00,0x00}, /* K */
    {0x40,0x40,0x40,0x40,0x40,0x7E,0x00,0x00}, /* L */
    {0x42,0x66,0x5A,0x42,0x42,0x42,0x00,0x00}, /* M */
    {0x42,0x62,0x52,0x4A,0x46,0x42,0x00,0x00}, /* N */
    {0x3C,0x42,0x42,0x42,0x42,0x3C,0x00,0x00}, /* O */
    {0x7C,0x42,0x42,0x7C,0x40,0x40,0x00,0x00}, /* P */
    {0x3C,0x42,0x42,0x5A,0x44,0x3A,0x00,0x00}, /* Q */
    {0x7C,0x42,0x42,0x7C,0x44,0x42,0x00,0x00}, /* R */
    {0x3C,0x42,0x30,0x0C,0x42,0x3C,0x00,0x00}, /* S */
    {0x7E,0x08,0x08,0x08,0x08,0x08,0x00,0x00}, /* T */
    {0x42,0x42,0x42,0x42,0x42,0x3C,0x00,0x00}, /* U */
    {0x42,0x42,0x42,0x24,0x24,0x18,0x00,0x00}, /* V */
    {0x42,0x42,0x42,0x5A,0x66,0x42,0x00,0x00}, /* W */
    {0x42,0x24,0x18,0x18,0x24,0x42,0x00,0x00}, /* X */
    {0x42,0x24,0x18,0x08,0x08,0x08,0x00,0x00}, /* Y */
    {0x7E,0x04,0x08,0x10,0x20,0x7E,0x00,0x00}, /* Z */
    {0x1C,0x10,0x10,0x10,0x10,0x1C,0x00,0x00}, /* [ */
    {0x20,0x20,0x10,0x08,0x04,0x04,0x04,0x00}, /* \ */
    {0x38,0x08,0x08,0x08,0x08,0x38,0x00,0x00}, /* ] */
    {0x08,0x14,0x22,0x00,0x00,0x00,0x00,0x00}, /* ^ */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x7E}, /* _ */
    {0x10,0x08,0x04,0x00,0x00,0x00,0x00,0x00}, /* ` */
    {0x00,0x00,0x3C,0x02,0x3E,0x42,0x3E,0x00}, /* a */
    {0x40,0x40,0x5C,0x62,0x42,0x62,0x5C,0x00}, /* b */
    {0x00,0x00,0x3C,0x42,0x40,0x42,0x3C,0x00}, /* c */
    {0x02,0x02,0x3A,0x46,0x42,0x46,0x3A,0x00}, /* d */
    {0x00,0x00,0x3C,0x42,0x7E,0x40,0x3C,0x00}, /* e */
    {0x0C,0x12,0x10,0x7C,0x10,0x10,0x10,0x00}, /* f */
    {0x00,0x00,0x3A,0x46,0x46,0x3A,0x02,0x3C}, /* g */
    {0x40,0x40,0x5C,0x62,0x42,0x42,0x42,0x00}, /* h */
    {0x08,0x00,0x18,0x08,0x08,0x08,0x3E,0x00}, /* i */
    {0x04,0x00,0x0C,0x04,0x04,0x04,0x44,0x38}, /* j */
    {0x40,0x40,0x44,0x48,0x70,0x48,0x44,0x00}, /* k */
    {0x18,0x08,0x08,0x08,0x08,0x08,0x3E,0x00}, /* l */
    {0x00,0x00,0x76,0x4A,0x4A,0x4A,0x4A,0x00}, /* m */
    {0x00,0x00,0x5C,0x62,0x42,0x42,0x42,0x00}, /* n */
    {0x00,0x00,0x3C,0x42,0x42,0x42,0x3C,0x00}, /* o */
    {0x00,0x00,0x5C,0x62,0x62,0x5C,0x40,0x40}, /* p */
    {0x00,0x00,0x3A,0x46,0x46,0x3A,0x02,0x02}, /* q */
    {0x00,0x00,0x5C,0x62,0x40,0x40,0x40,0x00}, /* r */
    {0x00,0x00,0x3E,0x40,0x3C,0x02,0x7C,0x00}, /* s */
    {0x10,0x10,0x7C,0x10,0x10,0x12,0x0C,0x00}, /* t */
    {0x00,0x00,0x42,0x42,0x42,0x46,0x3A,0x00}, /* u */
    {0x00,0x00,0x42,0x42,0x42,0x24,0x18,0x00}, /* v */
    {0x00,0x00,0x42,0x42,0x4A,0x4A,0x36,0x00}, /* w */
    {0x00,0x00,0x42,0x24,0x18,0x24,0x42,0x00}, /* x */
    {0x00,0x00,0x42,0x42,0x46,0x3A,0x02,0x3C}, /* y */
    {0x00,0x00,0x7E,0x04,0x08,0x10,0x7E,0x00}, /* z */
    {0x06,0x08,0x08,0x30,0x08,0x08,0x06,0x00}, /* { */
    {0x08,0x08,0x08,0x00,0x08,0x08,0x08,0x00}, /* | */
    {0x30,0x08,0x08,0x06,0x08,0x08,0x30,0x00}, /* } */
    {0x00,0x00,0x32,0x4C,0x00,0x00,0x00,0x00}, /* ~ */
};

/* ================================================================
 * 局部函数声明
 * ================================================================ */
static void lcd_fill_rect(esp_lcd_panel_handle_t panel, int x1, int y1,
                          int x2, int y2, uint16_t color);
static void lcd_draw_char(esp_lcd_panel_handle_t panel, int x, int y,
                          char c, uint16_t color, uint16_t bg);
static void lcd_draw_string(esp_lcd_panel_handle_t panel, int x, int y,
                            const char *str, uint16_t color, uint16_t bg);
static void lcd_draw_color_bars(esp_lcd_panel_handle_t panel);

/* XPT2046 触摸驱动 */
static void xpt2046_init(spi_device_handle_t *out_spi);
static bool xpt2046_read_xy(spi_device_handle_t tp_spi,
                            uint16_t *x, uint16_t *y,
                            uint16_t *raw_x, uint16_t *raw_y);
static bool xpt2046_is_touched(void);

/* ================================================================
 * lcd_test_start — 入口
 *
 * 独立初始化 SPI 总线 + ST7789 面板 + XPT2046 触摸。
 * 先跑一轮纯色/彩条/文字验证，然后进入触摸测试循环（永不返回）。
 * ================================================================ */
void lcd_test_start(void)
{
    esp_err_t ret;

    /* ---------------------------------------------------------------
     * 0. 触摸 PENIRQ 引脚初始化（先拉高，等 XPT2046 就绪）
     * --------------------------------------------------------------- */
    gpio_config_t pen_cfg = {
        .pin_bit_mask = (1ULL << PIN_NUM_TP_PEN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&pen_cfg);

    /* ---------------------------------------------------------------
     * 1. 背光 GPIO 初始化（高电平点亮）
     * --------------------------------------------------------------- */
    gpio_config_t bl_cfg = {
        .pin_bit_mask = (1ULL << PIN_NUM_BL),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&bl_cfg);
    gpio_set_level(PIN_NUM_BL, 0);  /* 先关，等初始化完再开 */

    /* ---------------------------------------------------------------
     * 2. SPI 总线初始化（LCD + XPT2046 共用 SPI3_HOST）
     * --------------------------------------------------------------- */
    spi_bus_config_t bus_cfg = {
        .sclk_io_num     = PIN_NUM_CLK,
        .mosi_io_num     = PIN_NUM_MOSI,
        .miso_io_num     = PIN_NUM_MISO,   /* XPT2046 需要 MISO */
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = 2 * LCD_WIDTH * FILL_BAND_H,
    };
    ret = spi_bus_initialize(LCD_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        printf("[LCD] SPI 总线初始化失败: %s\n", esp_err_to_name(ret));
        return;
    }
    printf("[LCD] SPI 总线初始化成功 (MISO=P%d)\n", PIN_NUM_MISO);

    /* ---------------------------------------------------------------
     * 3. 创建 LCD panel IO（SPI 接口）
     * --------------------------------------------------------------- */
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num       = PIN_NUM_DC,
        .cs_gpio_num       = PIN_NUM_CS,
        .pclk_hz           = SPI_CLOCK_HZ,
        .spi_mode          = 0,
        .trans_queue_depth = 10,
        .lcd_cmd_bits      = 8,
        .lcd_param_bits    = 8,
    };
    esp_lcd_panel_io_handle_t io_handle = NULL;
    ret = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST,
                                    &io_cfg, &io_handle);
    if (ret != ESP_OK) {
        printf("[LCD] Panel IO 创建失败: %s\n", esp_err_to_name(ret));
        spi_bus_free(LCD_HOST);
        return;
    }
    printf("[LCD] Panel IO 创建成功\n");

    /* ---------------------------------------------------------------
     * 4. 创建 ST7789 面板
     * --------------------------------------------------------------- */
    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num  = -1,
        .bits_per_pixel  = 16,
        .flags.reset_active_high = false,
    };
    ret = esp_lcd_new_panel_st7789(io_handle, &panel_cfg, &panel_handle);
    if (ret != ESP_OK) {
        printf("[LCD] ST7789 面板创建失败: %s\n", esp_err_to_name(ret));
        goto cleanup_io;
    }
    printf("[LCD] ST7789 面板创建成功\n");

    /* ---------------------------------------------------------------
     * 5. 初始化面板
     * --------------------------------------------------------------- */
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));

    /* BGR 颜色顺序（MADCTL bit 3 = 1） */
    {
        uint8_t madctl = 0x08;
        esp_lcd_panel_io_tx_param(io_handle, 0x36, &madctl, 1);
    }
    /* 关闭反转模式 */
    esp_lcd_panel_io_tx_param(io_handle, 0x21, NULL, 0);
    /* 间隙归零 */
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel_handle, 0, 0));
    /* 开背光 */
    gpio_set_level(PIN_NUM_BL, 1);
    printf("[LCD] 背光已打开\n");
    /* 开启显示 */
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));
    printf("[LCD] 显示已使能\n");

    vTaskDelay(pdMS_TO_TICKS(300));

    /* ---------------------------------------------------------------
     * 6. XPT2046 触摸初始化
     * --------------------------------------------------------------- */
    spi_device_handle_t tp_spi = NULL;
    xpt2046_init(&tp_spi);
    printf("[TP] XPT2046 初始化完成\n");

    /* ================================================================
     * Phase 1: 纯色/彩条/文字验证（快速展示屏幕正常）
     * ================================================================ */
    printf("\n=== Phase 1: ST7789 显示验证 ===\n");

    /* 6a. 纯色 */
    printf("[LCD] 红色\n");
    lcd_fill_rect(panel_handle, 0, 0, LCD_WIDTH, LCD_HEIGHT, COLOR_RED);
    vTaskDelay(pdMS_TO_TICKS(500));
    printf("[LCD] 绿色\n");
    lcd_fill_rect(panel_handle, 0, 0, LCD_WIDTH, LCD_HEIGHT, COLOR_GREEN);
    vTaskDelay(pdMS_TO_TICKS(500));
    printf("[LCD] 蓝色\n");
    lcd_fill_rect(panel_handle, 0, 0, LCD_WIDTH, LCD_HEIGHT, COLOR_BLUE);
    vTaskDelay(pdMS_TO_TICKS(500));

    /* 6b. 彩条 */
    printf("[LCD] 彩条\n");
    lcd_draw_color_bars(panel_handle);
    vTaskDelay(pdMS_TO_TICKS(1000));

    /* 6c. 文字信息 */
    printf("[LCD] 文字信息\n");
    lcd_fill_rect(panel_handle, 0, 0, LCD_WIDTH, LCD_HEIGHT, COLOR_BLUE);
    lcd_draw_string(panel_handle, 20, 40,
                    "ST7789 OK!", COLOR_YELLOW, COLOR_BLUE);
    lcd_draw_string(panel_handle, 20, 56,
                    "240x320 BGR", COLOR_WHITE, COLOR_BLUE);
    lcd_draw_string(panel_handle, 20, 72,
                    "SPI 27MHz", COLOR_CYAN, COLOR_BLUE);
    lcd_draw_string(panel_handle, 20, 88,
                    "P21 CLK", COLOR_GREEN, COLOR_BLUE);
    lcd_draw_string(panel_handle, 20, 104,
                    "P47 MOSI", COLOR_GREEN, COLOR_BLUE);
    lcd_draw_string(panel_handle, 20, 120,
                    "P42 CS  P41 DC", COLOR_MAGENTA, COLOR_BLUE);
    lcd_draw_string(panel_handle, 20, 136,
                    "P14 BL  P48 MISO", COLOR_WHITE, COLOR_BLUE);
    vTaskDelay(pdMS_TO_TICKS(1500));

    /* ================================================================
     * Phase 2: 触摸测试
     * ================================================================ */
    printf("\n=== Phase 2: XPT2046 触摸测试 ===\n");

    /* 清屏为深蓝色背景 */
    uint16_t bg = RGB565(0, 0, 80);  /* 深蓝 */
    lcd_fill_rect(panel_handle, 0, 0, LCD_WIDTH, LCD_HEIGHT, bg);

    /* 显示标题和操作说明 */
    uint16_t title_color = RGB565(255, 255, 100);  /* 淡黄 */
    lcd_draw_string(panel_handle, 10, 10, "Touch Test", title_color, bg);
    lcd_draw_string(panel_handle, 10, 26, "Touch screen", COLOR_CYAN, bg);
    lcd_draw_string(panel_handle, 10, 42, "X:0 Y:0", COLOR_WHITE, bg);

    /* 坐标显示区域（固定刷新位置） */
    #define COORD_LINE_Y  42

    /* 触摸循环 */
    int prev_x = -1, prev_y = -1;
    uint8_t touch_count = 0;
    char coord_buf[24];

    while (1) {
        uint16_t tx, ty;
        uint16_t raw_x, raw_y;

        if (xpt2046_read_xy(tp_spi, &tx, &ty, &raw_x, &raw_y)) {
            touch_count++;

            /* 坐标限幅 */
            if (tx >= LCD_WIDTH)  tx = LCD_WIDTH - 1;
            if (ty >= LCD_HEIGHT) ty = LCD_HEIGHT - 1;

            /* ----- 更新坐标文字（含原始值调试） ----- */
            snprintf(coord_buf, sizeof(coord_buf),
                     "X:%-3d Y:%-3d", tx, ty);
            lcd_draw_string(panel_handle, 10, COORD_LINE_Y,
                            coord_buf, COLOR_YELLOW, bg);
            /* 第二行显示原始 ADC 值 */
            snprintf(coord_buf, sizeof(coord_buf),
                     "RX:%-4d RY:%-4d", raw_x, raw_y);
            lcd_draw_string(panel_handle, 10, COORD_LINE_Y + 16,
                            coord_buf, RGB565(200, 200, 200), bg);
            printf("[TOUCH] #%d  scr:(%d,%d)  raw:(%d,%d)\n",
                   touch_count, tx, ty, raw_x, raw_y);

            /* ----- 擦除旧光标区域（33×33 方块） ----- */
            if (prev_x >= 0) {
                int ex1 = MAX(prev_x - 16, 0);
                int ey1 = MAX(prev_y - 16, 0);
                int ex2 = MIN(prev_x + 17, LCD_WIDTH);
                int ey2 = MIN(prev_y + 17, LCD_HEIGHT);
                lcd_fill_rect(panel_handle, ex1, ey1, ex2, ey2, bg);
            }

            /* ----- 画新光标（红色十字线，2 像素粗） ----- */
            #define CROSS_LEN 15
            /* BGR 模式下的红色：COLOR_RED(0xF800=R=31) 会显示为蓝色
             * 要显示红色，用 BGR=0,G=0,R=31 → 0x001F */
            #define CURSOR_COLOR  ((uint16_t)0x001F)
            /* 水平线 */
            int hx1 = MAX(tx - CROSS_LEN, 0);
            int hx2 = MIN(tx + CROSS_LEN + 1, LCD_WIDTH);
            int hy1 = MAX(ty - 1, 0);
            int hy2 = MIN(ty + 1, LCD_HEIGHT);
            lcd_fill_rect(panel_handle, hx1, hy1, hx2, hy2, CURSOR_COLOR);
            /* 垂直线 */
            int vx1 = MAX(tx - 1, 0);
            int vx2 = MIN(tx + 1, LCD_WIDTH);
            int vy1 = MAX(ty - CROSS_LEN, 0);
            int vy2 = MIN(ty + CROSS_LEN + 1, LCD_HEIGHT);
            lcd_fill_rect(panel_handle, vx1, vy1, vx2, vy2, CURSOR_COLOR);
            #undef CURSOR_COLOR
            #undef CROSS_LEN

            prev_x = tx;
            prev_y = ty;

        } else {
            /* 松开触摸：清空光标 */
            if (prev_x >= 0) {
                int ex1 = (prev_x > 8) ? prev_x - 8 : 0;
                int ey1 = (prev_y > 8) ? prev_y - 8 : 0;
                int ex2 = (prev_x < LCD_WIDTH - 9) ? prev_x + 9 : LCD_WIDTH;
                int ey2 = (prev_y < LCD_HEIGHT - 9) ? prev_y + 9 : LCD_HEIGHT;
                lcd_fill_rect(panel_handle, ex1, ey1, ex2, ey2, bg);

                /* 恢复坐标文字 */
                lcd_draw_string(panel_handle, 10, COORD_LINE_Y,
                                "Release      ", COLOR_CYAN, bg);
                printf("[TOUCH] Release\n");
                prev_x = -1;
                prev_y = -1;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(30));  /* ~33Hz 触摸轮询 */
    }

    /* 永不到达 */
cleanup_io:
    esp_lcd_panel_io_del(io_handle);
    spi_bus_free(LCD_HOST);
}

/* ================================================================
 * XPT2046 触摸驱动
 *
 * XPT2046 是 SPI 接口的电阻触摸控制芯片。
 * SPI 协议：先发 1 字节命令，再读 2 字节数据（12-bit ADC 值）。
 * PENIRQ 引脚低电平 = 有触摸。
 *
 * 命令字节格式：
 *   bit 7: START (1)
 *   bit 6-4: 通道选择
 *   bit 3: MODE (0=12-bit, 1=8-bit)
 *   bit 2: SER/DFR
 *   bit 1-0: 电源模式
 *
 * 常用命令：
 *   0xD0 — 读 X 坐标（通道 0，12-bit，单端）
 *   0x90 — 读 Y 坐标（通道 1，12-bit，单端）
 *   0xB0 — 读 Z1 压力（通道 2）
 * ================================================================ */
#define XPT_CMD_X      0xD0
#define XPT_CMD_Y      0x90

static spi_device_handle_t g_tp_spi = NULL;

bool xpt2046_is_touched(void)
{
    /* PENIRQ 低电平 = 有触摸 */
    return gpio_get_level(PIN_NUM_TP_PEN) == 0;
}

/**
 * 读取 XPT2046 一个通道的 12-bit ADC 原始值。
 *
 * SPI 事务采用 3 字节（24 时钟）：
 *   字节 0: 命令（MOSI）
 *   字节 1-2: 读回 12-bit ADC 值（对齐在 bit 11-0）
 *
 * 返回 0~4095 的原始值。
 */
static uint16_t xpt2046_read_adc(uint8_t cmd)
{
    uint8_t tx_buf[3] = { cmd, 0, 0 };
    uint8_t rx_buf[3] = { 0 };

    spi_transaction_t t = {
        .length    = 24,       /* 3 字节 */
        .tx_buffer = tx_buf,
        .rx_buffer = rx_buf,
    };

    esp_err_t ret = spi_device_polling_transmit(g_tp_spi, &t);
    if (ret != ESP_OK) {
        return 0xFFFF;
    }

    /*
     * XPT2046 时序（24 时钟）：
     *   时钟  1-8: MOSI=命令，MISO=旧数据
     *   时钟  9:   MISO=BUSY（低）
     *   时钟 10:   MISO=NULL bit（0）
     *   时钟 11-22: MISO=b11-b0（12-bit ADC 值）
     *   时钟 23-24: MISO=0
     *
     * rx_buf[1] 包含：BUSY(bit7) NULL(bit6) b11-b4(bit5-bit0)
     * rx_buf[2] 包含：b3-b0(bit7-bit4) 0(bit3-bit0)
     * 合并后右移 2 取 12-1 位，再去掉 NULL bit：
     *   最终值 = (combined >> 3) & 0x0FFF
     */
    uint16_t combined = ((uint16_t)rx_buf[1] << 8) | rx_buf[2];
    return (combined >> 3) & 0x0FFF;
}

/**
 * 读取触摸坐标（经过原始值 → 屏幕坐标转换）
 *
 * 返回 true = 有有效触摸，false = 无触摸。
 * 坐标已映射到 LCD_WIDTH × LCD_HEIGHT。
 */
bool xpt2046_read_xy(spi_device_handle_t tp_spi,
                     uint16_t *x, uint16_t *y,
                     uint16_t *raw_x, uint16_t *raw_y)
{
    if (!tp_spi) return false;

    /* 优先用 PENIRQ 判断是否有触摸 */
    if (!xpt2046_is_touched()) {
        return false;
    }

    /* 短延时等待触摸信号稳定 */
    esp_rom_delay_us(1000);

    /* 读取原始 ADC 值（多次采样取平均，抗抖动） */
    #define TP_SAMPLES  5
    uint32_t sum_x = 0, sum_y = 0;

    for (int i = 0; i < TP_SAMPLES; i++) {
        sum_x += xpt2046_read_adc(XPT_CMD_X);
        sum_y += xpt2046_read_adc(XPT_CMD_Y);
        esp_rom_delay_us(200);
    }

    uint16_t rxx = sum_x / TP_SAMPLES;
    uint16_t ryy = sum_y / TP_SAMPLES;

    if (raw_x) *raw_x = rxx;
    if (raw_y) *raw_y = ryy;

    /*
     * XPT2046 原始值范围 0~4095。
     * 简易映射（触摸 Y 方向 → 屏幕 X 方向，触摸 X 方向 → 屏幕 Y 方向）：
     *   screen_x = raw_y * LCD_WIDTH / 4096
     *   screen_y = raw_x * LCD_HEIGHT / 4096
     *
     * 如果坐标方向反了或需要镜像，改映射关系（可选方案）：
     *   方案 A: 交换 X/Y  — screen_x = rxx, screen_y = ryy
     *   方案 B: 翻转方向  — screen_x = (4096 - raw_y) * LCD_WIDTH / 4096
     *   方案 C: 调通道    — XPT_CMD_X 和 XPT_CMD_Y 对调
     */
    /* XPT2046 原始 X → 屏幕 X，原始 Y → 屏幕 Y（不再交换） */
    if (x) *x = ((uint32_t)rxx * LCD_WIDTH) / 4096;
    if (y) *y = ((uint32_t)ryy * LCD_HEIGHT) / 4096;

    return true;
}

/**
 * 初始化 XPT2046 SPI 设备
 *
 * 在已存在的 SPI 总线（LCD_HOST）上添加第二个 SPI 设备。
 */
void xpt2046_init(spi_device_handle_t *out_spi)
{
    if (!out_spi) return;

    spi_device_interface_config_t tp_devcfg = {
        .clock_speed_hz = TP_SPI_CLOCK_HZ,
        .mode           = 0,
        .spics_io_num   = PIN_NUM_TP_CS,
        .queue_size     = 1,
        /* 注意：XPT2046 是 4-wire SPI（全双工），不要加 SPI_DEVICE_HALFDUPLEX */
    };

    esp_err_t ret = spi_bus_add_device(LCD_HOST, &tp_devcfg, &g_tp_spi);
    if (ret != ESP_OK) {
        printf("[TP] spi_bus_add_device 失败: %s\n", esp_err_to_name(ret));
        *out_spi = NULL;
        return;
    }

    *out_spi = g_tp_spi;
    printf("[TP] SPI 设备添加成功 (CS=P%d, PEN=P%d)\n",
           PIN_NUM_TP_CS, PIN_NUM_TP_PEN);
}


/* ================================================================
 * LCD 绘制工具函数
 * ================================================================ */

static void lcd_fill_rect(esp_lcd_panel_handle_t panel, int x1, int y1,
                          int x2, int y2, uint16_t color)
{
    int w = x2 - x1;
    int h = y2 - y1;
    if (w <= 0 || h <= 0) return;

    uint16_t *band = malloc(w * FILL_BAND_H * sizeof(uint16_t));
    if (!band) return;

    for (int i = 0; i < w * FILL_BAND_H; i++) {
        band[i] = color;
    }

    for (int y = y1; y < y2; y += FILL_BAND_H) {
        int band_h = (y + FILL_BAND_H <= y2) ? FILL_BAND_H : (y2 - y);
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, x1, y,
                         x2, y + band_h, band));
    }
    free(band);
}

static void lcd_draw_char(esp_lcd_panel_handle_t panel, int x, int y,
                          char c, uint16_t color, uint16_t bg)
{
    if (c < 32 || c > 126) c = '?';
    const uint8_t *glyph = font8x8[c - 32];
    uint16_t buf[64];

    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            buf[row * 8 + col] = (glyph[row] >> (7 - col)) & 1 ? color : bg;
        }
    }
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, x, y,
                     x + 8, y + 8, buf));
}

static void lcd_draw_string(esp_lcd_panel_handle_t panel, int x, int y,
                            const char *str, uint16_t color, uint16_t bg)
{
    while (*str) {
        lcd_draw_char(panel, x, y, *str, color, bg);
        x += 8;
        str++;
    }
}

static void lcd_draw_color_bars(esp_lcd_panel_handle_t panel)
{
    const uint16_t colors[] = {
        COLOR_RED, COLOR_YELLOW, COLOR_GREEN,
        COLOR_CYAN, COLOR_BLUE, COLOR_MAGENTA
    };
    int bar_h = LCD_HEIGHT / 6;
    for (int i = 0; i < 6; i++) {
        lcd_fill_rect(panel, 0, i * bar_h, LCD_WIDTH,
                      (i + 1) * bar_h, colors[i]);
    }
}
