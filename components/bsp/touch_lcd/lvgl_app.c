/**
 * @file lvgl_app.c
 * @brief LVGL 程序 — 面板完整初始化 + 创建 LVGL 任务
 *
 * 引脚初始化（GPIO + SPI 总线）由 touch_lcd_init() 完成。
 * 本文件负责：
 *   1. lvgl_app_start(): 面板/触摸完整初始化 → 创建 LVGL 任务
 *   2. lvgl_task(): LVGL 内核 → 移植层 → UI → 定时器循环
 *
 * 调用顺序：
 *   bsp_init() → touch_lcd_init()              （引脚 + SPI 总线）
 *   app_start_tasks() → lvgl_app_start()       （面板 + 触摸 + 任务创建）
 */
#include "lvgl_app.h"
#include "touch_lcd_init.h"
#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_lcd_panel_ops.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>

/* ================================================================
 * 引脚常量（与 touch_lcd_init.c 保持一致）
 * ================================================================ */
#define LCD_HOST            SPI3_HOST
#define PIN_NUM_BL          14
#define LCD_WIDTH           240
#define LCD_HEIGHT          320
#define SPI_CLOCK_HZ        (27 * 1000 * 1000)
#define TP_SPI_CLOCK_HZ     (2 * 1000 * 1000)
#define PIN_NUM_DC          41
#define PIN_NUM_CS          42
#define PIN_NUM_TP_CS       46

/* 前向声明 */
static void lvgl_task(void *arg);
static void create_demo_ui(void);
static void lvgl_btn_cb(lv_event_t *e);
static void lvgl_clear_screen(uint16_t color);

/* ================================================================
 * lvgl_app_start — 面板/触摸初始化 + 创建 LVGL 任务
 *
 * 非阻塞，初始化硬件后立即创建任务并返回。
 * 必须在 touch_lcd_init()（SPI 总线就绪）之后调用。
 * ================================================================ */
void lvgl_app_start(void)
{
    esp_err_t ret;

    /* ---------------------------------------------------------------
     * 1. LCD panel IO（ST7789）
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
    ret = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST,
                                    &io_cfg, &touch_lcd_io_handle);
    if (ret != ESP_OK) {
        printf("[LVGL] Panel IO 失败: %s\n", esp_err_to_name(ret));
        return;
    }
    printf("[LVGL] Panel IO OK\n");

    /* ---------------------------------------------------------------
     * 2. ST7789 面板
     * --------------------------------------------------------------- */
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num  = -1,
        .bits_per_pixel  = 16,
        .rgb_ele_order   = LCD_RGB_ELEMENT_ORDER_RGB,
        .flags.reset_active_high = false,
    };
    ret = esp_lcd_new_panel_st7789(touch_lcd_io_handle, &panel_cfg,
                                    &touch_lcd_panel_handle);
    if (ret != ESP_OK) {
        printf("[LVGL] ST7789 创建失败: %s\n", esp_err_to_name(ret));
        esp_lcd_panel_io_del(touch_lcd_io_handle);
        touch_lcd_io_handle = NULL;
        return;
    }
    printf("[LVGL] ST7789 面板 OK\n");

    /* 初始化面板 */
    ESP_ERROR_CHECK(esp_lcd_panel_reset(touch_lcd_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(touch_lcd_panel_handle));
    printf("[LVGL] 面板已初始化\n");

    /* MADCTL */
    {
        uint8_t madctl = 0x08;
        esp_lcd_panel_io_tx_param(touch_lcd_io_handle, 0x36, &madctl, 1);
    }
    /* INVON */
    esp_lcd_panel_io_tx_param(touch_lcd_io_handle, 0x21, NULL, 0);
    /* 间隙归零 */
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(touch_lcd_panel_handle, 0, 0));
    /* 开背光 */
    gpio_set_level(PIN_NUM_BL, 1);
    printf("[LVGL] 背光开\n");
    /* 开启显示 */
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(touch_lcd_panel_handle, true));
    printf("[LVGL] 显示已使能\n");

    vTaskDelay(pdMS_TO_TICKS(200));

    /* 清屏为黑色 */
    lvgl_clear_screen(0x0000);

    /* ---------------------------------------------------------------
     * 3. XPT2046 触摸 SPI 设备
     * --------------------------------------------------------------- */
    {
        spi_device_interface_config_t tp_devcfg = {
            .clock_speed_hz = TP_SPI_CLOCK_HZ,
            .mode           = 0,
            .spics_io_num   = PIN_NUM_TP_CS,
            .queue_size     = 1,
        };
        ret = spi_bus_add_device(LCD_HOST, &tp_devcfg, &touch_lcd_tp_spi);
        if (ret != ESP_OK) {
            printf("[LVGL] XPT2046 SPI 设备失败: %s\n", esp_err_to_name(ret));
            return;
        }
        printf("[LVGL] XPT2046 SPI 设备 OK\n");
    }

    /* ---------------------------------------------------------------
     * 4. 创建 LVGL 任务
     * --------------------------------------------------------------- */
    xTaskCreate(lvgl_task, "lvgl_task", 4096, NULL, 5, NULL);
    printf("[LVGL] 任务已创建\n");
}

/* ================================================================
 * LVGL 任务 — 初始化 LVGL 内核 + UI + 定时器循环
 *
 * 初始化完成后每 ~5ms 调用 lv_timer_handler()。
 * ================================================================ */
static void lvgl_task(void *arg)
{
    /* LVGL 内核初始化 */
    lv_init();

    /* 注册显示驱动（传入硬件句柄） */
    lv_port_disp_init(touch_lcd_panel_handle, touch_lcd_io_handle);
    printf("[LVGL] 显示移植层 OK\n");

    /* 注册触摸驱动 */
    lv_port_indev_init(touch_lcd_tp_spi);
    printf("[LVGL] 触摸移植层 OK\n");

    /* 创建 UI */
    create_demo_ui();
    printf("[LVGL] UI 创建完成\n");

    /* LVGL 定时器处理循环 */
    while (1) {
        lv_tick_inc(portTICK_PERIOD_MS);    /* LVGL 心跳 */
        lv_timer_handler();                 /* 处理过期的 LVGL 定时器 */
        vTaskDelay(1);                      /* 让低优先级任务运行 */
    }
}

/* ================================================================
 * 辅助函数：用指定颜色填充全屏
 * ================================================================ */
static void lvgl_clear_screen(uint16_t color)
{
    if (!touch_lcd_panel_handle) return;

    uint16_t *buf = malloc(LCD_WIDTH * 40 * sizeof(uint16_t));
    if (!buf) return;

    for (int i = 0; i < LCD_WIDTH * 40; i++) {
        buf[i] = color;
    }

    for (int y = 0; y < LCD_HEIGHT; y += 40) {
        int h = (y + 40 <= LCD_HEIGHT) ? 40 : (LCD_HEIGHT - y);
        esp_lcd_panel_draw_bitmap(touch_lcd_panel_handle, 0, y,
                                  LCD_WIDTH, y + h, buf);
    }
    free(buf);
}

/* ================================================================
 * LVGL UI 创建
 *
 * 创建一个简单的 demo 界面：
 *   - 标题标签 "LVGL OK! :D"
 *   - 按钮，点击后计数器 +1
 *   - 下方显示触摸坐标
 * ================================================================ */
static void create_demo_ui(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000033), 0);  /* 深蓝背景 */

    /* ---- 标题 ---- */
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "LVGL OK! :D");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFF00), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);

    /* ---- 按钮 ---- */
    lv_obj_t *btn = lv_btn_create(scr);
    lv_obj_set_size(btn, 140, 50);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, -20);

    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "Press me!");
    lv_obj_center(btn_label);

    /* ---- 触摸坐标显示 ---- */
    lv_obj_t *coord = lv_label_create(scr);
    lv_label_set_text(coord, "Touch: (---, ---)");
    lv_obj_set_style_text_color(coord, lv_color_hex(0x00FFFF), 0);
    lv_obj_align(coord, LV_ALIGN_BOTTOM_MID, 0, -30);

    /* ---- 按钮点击事件 ---- */
    static int counter = 0;
    lv_obj_add_event_cb(btn, lvgl_btn_cb, LV_EVENT_CLICKED, &counter);
}

static void lvgl_btn_cb(lv_event_t *e)
{
    int *counter = (int *)lv_event_get_user_data(e);
    (*counter)++;

    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_t *label = lv_obj_get_child(btn, 0);
    char buf[32];
    snprintf(buf, sizeof(buf), "Count: %d", *counter);
    lv_label_set_text(label, buf);
}
