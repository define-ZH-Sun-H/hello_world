/**
 * @file lv_port_disp.c
 * @brief LVGL 显示移植 — DMA 同步双缓冲 flush
 *
 * flush_cb 通过 esp_lcd_panel_draw_bitmap 将 LVGL 绘制缓冲区
 * 发送到屏幕。设置 DMA 完成回调，等 SPI 传输真正结束后才调用
 * lv_disp_flush_ready，防止 LVGL 覆写仍在 DMA 读取中的缓冲区。
 */
#include "lv_port_disp.h"
#include "lvgl.h"
#include "esp_lcd_panel_ops.h"
#include <stdlib.h>

/* 分辨率常量 */
#define LCD_HOR_RES  240
#define LCD_VER_RES  320

/* 显示缓冲区大小：水平 240px × 40 行 = 9600 像素，约 1/8 屏 */
#define DISP_BUF_ROWS    40
#define DISP_BUF_SIZE    (LCD_HOR_RES * DISP_BUF_ROWS)

/* flush_cb 中存下的 disp_drv 指针，用于 DMA 完成回调 */
static lv_disp_drv_t *g_disp_drv = NULL;

/* 前向声明 */
static void lv_port_disp_flush(lv_disp_drv_t *disp_drv,
                               const lv_area_t *area,
                               lv_color_t *color_p);

/**
 * DMA 完成回调 — SPI 传输结束后由 IO 驱动调用
 *
 * 这里才通知 LVGL 缓冲区可复用，避免数据竞争。
 */
static bool disp_flush_done_cb(esp_lcd_panel_io_handle_t io,
                               esp_lcd_panel_io_event_data_t *edata,
                               void *user_ctx)
{
    if (g_disp_drv) {
        lv_disp_flush_ready(g_disp_drv);
    }
    return false;
}

void lv_port_disp_init(esp_lcd_panel_handle_t panel,
                       esp_lcd_panel_io_handle_t io)
{
    /* ---------- 双缓冲 ---------- */
    lv_color_t *buf1 = malloc(DISP_BUF_SIZE * sizeof(lv_color_t));
    lv_color_t *buf2 = malloc(DISP_BUF_SIZE * sizeof(lv_color_t));
    if (!buf1 || !buf2) {
        abort();
    }

    static lv_disp_draw_buf_t draw_buf;
    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, DISP_BUF_SIZE);

    /* ---------- 注册 DMA 完成回调 ---------- */
    esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = disp_flush_done_cb,
    };
    esp_lcd_panel_io_register_event_callbacks(io, &cbs, NULL);

    /* ---------- 注册 LVGL 显示驱动 ---------- */
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);

    disp_drv.hor_res      = LCD_HOR_RES;
    disp_drv.ver_res      = LCD_VER_RES;
    disp_drv.draw_buf     = &draw_buf;
    disp_drv.flush_cb     = lv_port_disp_flush;
    disp_drv.user_data    = panel;

    lv_disp_drv_register(&disp_drv);

    g_disp_drv = &disp_drv;  /* 供 DMA 回调使用 */
}

/* ================================================================
 * flush_cb 内部实现
 * ================================================================ */
static void lv_port_disp_flush(lv_disp_drv_t *disp_drv,
                               const lv_area_t *area,
                               lv_color_t *color_p)
{
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)disp_drv->user_data;

    esp_lcd_panel_draw_bitmap(
        panel,
        area->x1, area->y1,
        area->x2 + 1, area->y2 + 1,
        (uint16_t *)color_p
    );

    /* 注意：这里不调 lv_disp_flush_ready！
     * 由 disp_flush_done_cb 在 SPI DMA 完成后才调。 */
}
