#include "rgb.h"
#include "led_strip_encoder.h"
#include "driver/rmt_tx.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "rgb";

/*
 * —————— 全局状态 ——————
 */

/** @brief RMT TX 通道句柄 —— 经 rgb_init 创建后，rgb_flush 通过它将像素数据编码为 WS2812 波形 */
static rmt_channel_handle_t led_chan = NULL;

/** @brief LED 条带编码器句柄 —— 将像素字节数组（G-R-B）编码为 RMT 符号 + 复位码 */
static rmt_encoder_handle_t led_encoder = NULL;

/**
 * @brief 像素缓冲区（3 bytes）
 *
 * 内部存储使用 WS2812 原生顺序 —— G-R-B（而非常规 R-G-B）。
 * rgb_set_color 入参是 R-G-B 顺序，写入缓冲区时做重排：
 *   led_pixels[0] = green
 *   led_pixels[1] = red
 *   led_pixels[2] = blue
 */
static uint8_t led_pixels[3];

/**
 * @brief 全局亮度系数（0~100）
 *
 * rgb_set_color 在写入缓冲区前对每个通道做线性缩放：
 *   actual = (input × brightness) / 100
 * brightness=0 熄灭，默认 30 即入参 30% 强度。
 */
static uint8_t brightness = 30;

/**
 * @brief HSV → RGB 色彩空间转换
 *
 * === 算法说明 ===
 *
 * 基于色环 6 段扇形插值算法：
 *   1. 将色相 h 映射到 [0, 360) 范围
 *   2. 计算当前扇形编号 i = h / 60（0~5），以及扇内偏移 diff = h % 60
 *   3. 根据饱和度 s 和明度 v，推算 6 段中 R/G/B 各自的过渡值
 *
 * === 参数范围 ===
 *
 *   h     色相 0~359°   （0=红, 120=绿, 240=蓝）
 *   s     饱和度 0~100  （0=灰色, 100=纯色）
 *   v     明度 0~100    （0=黑色, 100=最亮）
 *   r/g/b 输出 0~255    （RGB 888 范围）
 *
 * @param h  色相角（0~359），函数内部取模保证安全
 * @param s  饱和度百分比（0~100）
 * @param v  明度百分比（0~100）
 * @param r  输出红色通道（0~255）
 * @param g  输出绿色通道（0~255）
 * @param b  输出蓝色通道（0~255）
 */
static void hsv2rgb(uint32_t h, uint32_t s, uint32_t v,
                    uint32_t *r, uint32_t *g, uint32_t *b)
{
    /* 防止调用方传入 >= 360 的值 */
    h %= 360;

    /*
     * rgb_max：v 等比例映射到 0~255
     *   v=0   → 0
     *   v=100 → 255
     *   系数 2.55 = 255 / 100
     */
    uint32_t rgb_max = v * 2.55f;

    /*
     * rgb_min：由饱和度决定的"衰减基线"
     *   s=0（灰色）→ rgb_min = rgb_max → R=G=B
     *   s=100（纯色）→ rgb_min = 0 → 饱和度最大
     */
    uint32_t rgb_min = rgb_max * (100 - s) / 100.0f;

    /* 色环 60° 扇形编号 */
    uint32_t i = h / 60;

    /* 扇区内偏移（0~59），用于线性插值 */
    uint32_t diff = h % 60;

    /* 扇形内渐变量 = (rgb_max - rgb_min) × (diff / 60) */
    uint32_t rgb_adj = (rgb_max - rgb_min) * diff / 60;

    /*
     * 六段扇形：每 60° 一个区间，R/G/B 分别在上升/下降/恒定之间切换
     *                    R     |     G     |     B
     *   0°- 60°  (0):   max   |   min+adj |   min
     *  60°-120°  (1): max-adj |    max    |   min
     * 120°-180°  (2):   min   |    max    |   min+adj
     * 180°-240°  (3):   min   |  max-adj  |   max
     * 240°-300°  (4): min+adj |    min    |   max
     * 300°-360°  (5):   max   |    min    | max-adj
     */
    switch (i) {
    case 0: *r = rgb_max; *g = rgb_min + rgb_adj; *b = rgb_min; break;
    case 1: *r = rgb_max - rgb_adj; *g = rgb_max; *b = rgb_min; break;
    case 2: *r = rgb_min; *g = rgb_max; *b = rgb_min + rgb_adj; break;
    case 3: *r = rgb_min; *g = rgb_max - rgb_adj; *b = rgb_max; break;
    case 4: *r = rgb_min + rgb_adj; *g = rgb_min; *b = rgb_max; break;
    default:*r = rgb_max; *g = rgb_min; *b = rgb_max - rgb_adj; break;
    }
}

/**
 * @brief 将 led_pixels 缓冲区的数据通过 RMT 发送到 WS2812
 *
 * === 发送流程 ===
 *
 *   1. 构造 tx_config（单次发送，loop_count=0）
 *   2. rmt_transmit()：启动 DMA 传输，led_pixels 经编码管道
 *      （bytes_encoder → copy_encoder）转为 RMT 符号波形
 *   3. rmt_tx_wait_all_done()：阻塞等待发送完成（portMAX_DELAY）
 *
 * === 编码管道 ===
 *
 *   led_pixels[3]  → bytes_encoder  → copy_encoder  → RMT TX
 *   (G-R-B bytes)    每位→0/1码    复位码≥50μs      GPIO16
 *
 * bytes_encoder 负责将 1byte 展开为 8 个 RMT 符号（高位优先），
 * 每个符号根据 WS2812 时序编码为 0.3μs/0.9μs（bit-0）或 0.9μs/0.3μs（bit-1）。
 * copy_encoder 附加一个 50μs 低电平复位码，触发 WS2812 锁存输出。
 *
 * @note 此函数阻塞直到发送完成，适合单 LED 场景。
 *       级联多 LED 时需注意每增加一个 LED 增加 ~30μs 传输时间。
 */
static void rgb_flush(void)
{
    if (led_chan && led_encoder) {
        rmt_transmit_config_t tx_config = { .loop_count = 0 };

        /* 将 led_pixels 提交到 RMT 发送队列 */
        ESP_ERROR_CHECK(rmt_transmit(led_chan, led_encoder,
                                     led_pixels, sizeof(led_pixels),
                                     &tx_config));

        /* 阻塞等待本次发送完成（含复位码），portMAX_DELAY 表示无限等待 */
        ESP_ERROR_CHECK(rmt_tx_wait_all_done(led_chan, portMAX_DELAY));
    }
}

/**
 * @brief 初始化 WS2812 RGB LED
 *
 * === 初始化流程 ===
 *
 *   1. 创建 RMT TX 通道（GPIO16, 10MHz 分辨率, 64 内存符号）
 *   2. 安装 LED 条带编码器（bytes_encoder + copy_encoder）
 *   3. 使能 RMT TX 通道（分配硬件资源）
 *   4. rgb_clear() 确保 LED 初始化后熄灭
 *
 * === 硬件配置说明 ===
 *
 *   resolution_hz = 10MHz  →  每个 tick = 0.1μs
 *   此分辨率下：
 *     - 0 码（0.3μs 高 + 0.9μs 低）= 3 ticks + 9 ticks
 *     - 1 码（0.9μs 高 + 0.3μs 低）= 9 ticks + 3 ticks
 *   WS2812 时序容差 ±150ns，10MHz 精度足够。
 *
 * @note 必须在调用 rgb_set_color/rgb_set_hsv 之前调用一次。
 */
void rgb_init(void)
{
    ESP_LOGI(TAG, "Create RMT TX channel");
    rmt_tx_channel_config_t tx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,   /* 使用默认时钟源（通常为 XTAL/APB） */
        .gpio_num = RGB_GPIO,              /* 数据输出引脚 GPIO16 */
        .mem_block_symbols = 64,           /* 每个通道 64 个 RMT 符号（24bits × 2 + 复位码够用） */
        .resolution_hz = RGB_RESOLUTION_HZ,/* 10MHz，每 tick = 0.1μs */
        .trans_queue_depth = 4,            /* 发送队列深度 4，允许预提交多个帧 */
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, &led_chan));

    ESP_LOGI(TAG, "Install led strip encoder");
    led_strip_encoder_config_t encoder_config = {
        .resolution = RGB_RESOLUTION_HZ,   /* 编码器需知道分辨率以生成正确时序 */
    };
    ESP_ERROR_CHECK(rmt_new_led_strip_encoder(&encoder_config, &led_encoder));

    ESP_LOGI(TAG, "Enable RMT TX channel");
    /* 使能后通道开始接受 rmt_transmit 请求 */
    ESP_ERROR_CHECK(rmt_enable(led_chan));

    /* 初始化为熄灭状态 */
    rgb_clear();
}

/**
 * @brief 设置 RGB 颜色（含亮度缩放）
 *
 * === 处理流程 ===
 *
 *   1. 亮度缩放：每个通道 × brightness / 100
 *      使用 uint16_t 中间变量避免 uint8_t 溢出：
 *      输入 255 × 亮度 100 / 100 = 255 ✓
 *      输入 255 × 亮度 200        → uint8_t 溢出 ✗
 *   2. 字节顺序重排：入参 R-G-B → 缓冲区 G-R-B
 *   3. 调用 rgb_flush() 通过 RMT 将三字节发送到 WS2812
 *
 * @param r 红色分量（0~255），函数内会自动叠加亮度系数
 * @param g 绿色分量（0~255）
 * @param b 蓝色分量（0~255）
 */
void rgb_set_color(uint8_t r, uint8_t g, uint8_t b)
{
    /*
     * 线性亮度缩放：先提升到 uint16_t 防止 (0xFF * 100) 溢出
     * 当 brightness=30 时，实际输出为 30% 强度
     */
    r = (uint16_t)r * brightness / 100;
    g = (uint16_t)g * brightness / 100;
    b = (uint16_t)b * brightness / 100;

    /*
     * WS2812 内部像素排列为 G-R-B 顺序
     * 入参为 R-G-B 人眼习惯顺序，在此重排
     */
    led_pixels[0] = g;      /* 绿色 */
    led_pixels[1] = r;      /* 红色 */
    led_pixels[2] = b;      /* 蓝色 */

    /* 通过 RMT 发送 24 位数据 + 复位码 */
    rgb_flush();
}

/**
 * @brief 通过 HSV 色彩模型设置颜色
 *
 * 适合色相循环/彩虹效果场景：
 *   只需递增 h（0→359）即可平滑过渡所有颜色，
 *   无需手动计算 R/G/B 配比。
 *
 * @param h 色相角（0~359°）
 * @param s 饱和度（0~100）
 * @param v 明度（0~100）
 */
void rgb_set_hsv(uint16_t h, uint8_t s, uint8_t v)
{
    uint32_t r, g, b;

    /* HSV → RGB 转换 */
    hsv2rgb(h, s, v, &r, &g, &b);

    /* 套入亮度缩放 + G-R-B 重排 + RMT 发送 */
    rgb_set_color((uint8_t)r, (uint8_t)g, (uint8_t)b);
}

/**
 * @brief 熄灭 LED
 *
 * 将三个通道全部置 0 并发送，等同于 rgb_set_color(0, 0, 0)。
 */
void rgb_clear(void)
{
    led_pixels[0] = 0;
    led_pixels[1] = 0;
    led_pixels[2] = 0;
    rgb_flush();
}

/**
 * @brief 设置全局亮度百分比
 *
 * 影响后续所有 rgb_set_color / rgb_set_hsv 调用，
 * 不影响已显示的上一帧颜色。
 *
 * @param percent 亮度百分比（0~100），超过 100 会被钳位
 */
void rgb_set_brightness(uint8_t percent)
{
    if (percent > 100) percent = 100;
    brightness = percent;
}

/* ================================================================
 * 彩虹灯任务
 * ================================================================ */

static void rgb_rainbow_task(void *pv)
{
    uint16_t hue = 0;
    while (1) {
        rgb_set_hsv(hue, 100, 100);
        hue = (hue + 1) % 360;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/* 静态创建所需内存 */
static StackType_t s_rgb_rainbow_stack[2048];
static StaticTask_t s_rgb_rainbow_tcb;

void rgb_start_rainbow(void)
{
    xTaskCreateStaticPinnedToCore(rgb_rainbow_task, "rgb_rainbow",
        2048, NULL, 3,
        s_rgb_rainbow_stack, &s_rgb_rainbow_tcb,
        1);  /* Core 1：装饰任务，不干扰其他任务 */
}
