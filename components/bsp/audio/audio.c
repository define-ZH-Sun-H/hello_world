/**
 * @file audio.c
 * @brief 音频模块驱动实现
 *
 * I2S0 — PDM RX（LMD2718 数字硅麦）
 *   - ESP32-S3 硬件 PDM→PCM 转换，无需软件解码
 *   - 16kHz / 16-bit / mono PCM 输出
 *   - 帧回调每 40ms 触发一次（640 样本）
 *
 * I2S1 — STD TX（NS4168 I2S 功放）
 *   - Philips 格式
 *   - 16kHz / 16-bit / mono
 *   - audio_play() 阻塞写入
 */

#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2s_common.h"
#include "driver/i2s_pdm.h"
#include "driver/i2s_std.h"
#include "audio.h"

static const char *TAG = "audio";

/* ================================================================
 * 引脚定义
 * ================================================================ */

#define PIN_I2S0_CLK        GPIO_NUM_4       /* LMD2718 CLK */
#define PIN_I2S0_DIN        GPIO_NUM_5       /* LMD2718 DATA */

#define PIN_I2S1_BCLK       GPIO_NUM_15      /* NS4168 BCLK */
#define PIN_I2S1_WS         GPIO_NUM_6       /* NS4168 LRCLK */
#define PIN_I2S1_DOUT       GPIO_NUM_7       /* NS4168 SDATA */

/* ================================================================
 * 录音参数
 * ================================================================ */

#define RECORD_FRAME_SAMPLES    640          /* 每帧样本数 = 40ms @ 16kHz */
#define RECORD_FRAME_BYTES      (RECORD_FRAME_SAMPLES * sizeof(int16_t))

/* ================================================================
 * 内部状态
 * ================================================================ */

static i2s_chan_handle_t s_tx_handle = NULL;    /* I2S1 TX（NS4168） */
static i2s_chan_handle_t s_rx_handle = NULL;    /* I2S0 RX（LMD2718） */

static TaskHandle_t s_record_task = NULL;        /* 录音采集任务句柄 */

/* 静态创建所需内存（支持按需启停，vTaskDelete 后缓冲区保留可重用） */
static StackType_t s_audio_rec_stack[3072];
static StaticTask_t s_audio_rec_tcb;

static audio_record_cb_t s_record_cb = NULL;
static void *s_record_ctx = NULL;

static int s_rms = 0;       /* 最新帧 RMS 值，串口调试用 */

/* ================================================================
 * I2S0 — PDM RX 初始化（LMD2718 麦克风）
 * ================================================================ */

static esp_err_t i2s0_pdm_rx_init(void)
{
    esp_err_t ret;

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ret = i2s_new_channel(&chan_cfg, NULL, &s_rx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S0 通道分配失败: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "I2S0 通道已分配");

    /* ESP32-S3 PDM→PCM 硬件转换，读出的直接是 PCM */
    i2s_pdm_rx_config_t pdm_rx_cfg = {
        .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_PDM_RX_SLOT_PCM_FMT_DEFAULT_CONFIG(
                        I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .clk = PIN_I2S0_CLK,
            .din = PIN_I2S0_DIN,
            .invert_flags = { .clk_inv = false },
        },
    };
    ret = i2s_channel_init_pdm_rx_mode(s_rx_handle, &pdm_rx_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S0 PDM RX 初始化失败: %s", esp_err_to_name(ret));
        i2s_del_channel(s_rx_handle);
        s_rx_handle = NULL;
        return ret;
    }
    ESP_LOGI(TAG, "I2S0 PDM RX 初始化完成 (16kHz/16bit/mono)");
    return ESP_OK;
}

/* ================================================================
 * I2S1 — STD TX 初始化（NS4168 功放）
 * ================================================================ */

static esp_err_t i2s1_std_tx_init(void)
{
    esp_err_t ret;

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    ret = i2s_new_channel(&chan_cfg, &s_tx_handle, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S1 通道分配失败: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "I2S1 通道已分配");

    i2s_std_config_t std_tx_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                        I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = GPIO_NUM_NC,
            .bclk = PIN_I2S1_BCLK,
            .ws   = PIN_I2S1_WS,
            .dout = PIN_I2S1_DOUT,
            .din  = GPIO_NUM_NC,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    ret = i2s_channel_init_std_mode(s_tx_handle, &std_tx_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S1 STD TX 初始化失败: %s", esp_err_to_name(ret));
        i2s_del_channel(s_tx_handle);
        s_tx_handle = NULL;
        return ret;
    }
    ESP_LOGI(TAG, "I2S1 STD TX 初始化完成 (16kHz/16bit/mono Philips)");
    return ESP_OK;
}

/* ================================================================
 * 自检音 — 1kHz 正弦波 0.5 秒
 * ================================================================ */

static void audio_self_test(void)
{
    const int samples = AUDIO_SAMPLE_RATE / 2;    /* 0.5s @ 16kHz */
    int16_t *tone = malloc(samples * sizeof(int16_t));
    if (!tone) {
        ESP_LOGE(TAG, "自检音内存分配失败");
        return;
    }

    const float two_pi_1000_over_fs = 2.0f * 3.14159265f * 1000.0f / AUDIO_SAMPLE_RATE;
    for (int i = 0; i < samples; i++) {
        tone[i] = (int16_t)(50.0f * sinf(two_pi_1000_over_fs * i));
    }

    ESP_LOGI(TAG, "播放自检音 (1kHz/0.5s)...");
    audio_play(tone, samples);
    free(tone);
}

/* ================================================================
 * 内部录音采集任务
 * ================================================================ */

static void audio_record_task(void *pv)
{
    int16_t *frame = malloc(RECORD_FRAME_BYTES);
    if (!frame) {
        ESP_LOGE(TAG, "录音帧内存分配失败");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "录音采集任务已启动");

    while (1) {
        size_t bytes_read = 0;
        esp_err_t ret = i2s_channel_read(s_rx_handle, frame, RECORD_FRAME_BYTES,
                                          &bytes_read, portMAX_DELAY);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "录音读取失败: %s", esp_err_to_name(ret));
            continue;
        }

        size_t samples_read = bytes_read / sizeof(int16_t);

        /* 计算 RMS */
        int sum_sq = 0;
        for (size_t i = 0; i < samples_read; i++) {
            sum_sq += (frame[i] * frame[i]) >> 8;
        }
        s_rms = (int)sqrtf((float)sum_sq / samples_read) << 4;

        /* 回调 */
        if (s_record_cb) {
            s_record_cb(frame, samples_read, s_record_ctx);
        }
    }

    free(frame);
    vTaskDelete(NULL);
}

/* ================================================================
 * 公开接口
 * ================================================================ */

esp_err_t audio_init(void)
{
    esp_err_t ret;

    ret = i2s1_std_tx_init();
    if (ret != ESP_OK) return ret;

    /* 使能 TX 通道，否则 i2s_channel_write 会永久阻塞 */
    ret = i2s_channel_enable(s_tx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S1 TX 使能失败: %s", esp_err_to_name(ret));
        i2s_del_channel(s_tx_handle);
        s_tx_handle = NULL;
        return ret;
    }

    ret = i2s0_pdm_rx_init();
    if (ret != ESP_OK) {
        i2s_channel_disable(s_tx_handle);
        i2s_del_channel(s_tx_handle);
        s_tx_handle = NULL;
        return ret;
    }

    ESP_LOGI(TAG, "音频模块初始化完成");

    /* （已屏蔽）播放自检音确认功放工作 */
    //audio_self_test();

    return ESP_OK;
}

esp_err_t audio_play(const int16_t *data, size_t samples)
{
    size_t bytes_written = 0;
    size_t bytes = samples * sizeof(int16_t);
    esp_err_t ret = i2s_channel_write(s_tx_handle, data, bytes, &bytes_written, portMAX_DELAY);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "播放失败: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t audio_record_start(audio_record_cb_t cb, void *ctx)
{
    if (!s_rx_handle) {
        ESP_LOGE(TAG, "I2S0 未初始化");
        return ESP_ERR_INVALID_STATE;
    }
    if (s_record_task) {
        ESP_LOGW(TAG, "录音已在运行");
        return ESP_OK;
    }

    s_record_cb = cb;
    s_record_ctx = ctx;

    esp_err_t ret = i2s_channel_enable(s_rx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S0 使能失败: %s", esp_err_to_name(ret));
        return ret;
    }

    TaskHandle_t h = xTaskCreateStaticPinnedToCore(audio_record_task, "audio_rec",
        3072, NULL, 6,
        s_audio_rec_stack, &s_audio_rec_tcb,
        0);  /* Core 0：音频数据最终走 MQTT→网络栈 */
    if (h != NULL) {
        s_record_task = h;
    } else {
        i2s_channel_disable(s_rx_handle);
        ESP_LOGE(TAG, "录音任务创建失败");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "录音已启动 (任务 %d 栈)", 3072);
    return ESP_OK;
}

esp_err_t audio_record_stop(void)
{
    if (s_record_task) {
        vTaskDelete(s_record_task);
        s_record_task = NULL;
    }
    s_record_cb = NULL;
    s_record_ctx = NULL;

    if (s_rx_handle) {
        i2s_channel_disable(s_rx_handle);
    }
    ESP_LOGI(TAG, "录音已停止");
    return ESP_OK;
}

int audio_get_rms(void)
{
    return s_rms;
}
