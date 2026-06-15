/**
 * @file mqtt.c
 * @brief MQTT 客户端 — 连接 Broker + SNTP 校时 + 状态栏时间
 *
 * 流程：
 *   mqtt_app_start()
 *     └→ 创建 mqtt_time_task
 *          1. 等待 WiFi 就绪（WIFI_CONNECTED_BIT）
 *          2. 启动 SNTP（pool.ntp.org）
 *          3. 启动 MQTT 客户端（broker.emqx.io:1883）
 *          4. 等待时间同步（最多 10s）
 *          5. 每秒更新主页状态栏时间
 *
 * 外部依赖：
 *   - wifi.h / wifi_event_group（WiFi 状态通知）
 *   - oled_display.h / oled_display_set_time（刷新时间）
 *   - esp-mqtt 组件（ESP-IDF 内置）
 *   - lwip SNTP（ESP-IDF 内置）
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "mqtt_client.h"
#include "esp_sntp.h"

#include "wifi.h"
#include "oled_display.h"
#include "sensor_init.h"
#include "audio.h"

static const char *TAG = "mqtt";

/* MQTT 客户端句柄（模块内部） */
static esp_mqtt_client_handle_t s_client = NULL;

/* 时间同步标记 */
static bool s_time_synced = false;

/* ================================================================
 * MQTT 事件回调
 *
 * 所有操作必须轻量，不要做阻塞调用。
 * ================================================================ */

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {

    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "已连接 Broker");
        esp_mqtt_client_subscribe(s_client, "cmd/esp32s3/#", 1);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "与 Broker 断开");
        break;

    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "收到: %.*s = %.*s",
                 event->topic_len, event->topic,
                 event->data_len, event->data);
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT 错误");
        break;

    default:
        break;
    }
}

/* ================================================================
 * SNTP 校时初始化
 * ================================================================ */

static void initialize_sntp(void)
{
    ESP_LOGI(TAG, "启动 SNTP 校时...");

    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");

    /* 设置时区为 UTC+8 中国标准时间 */
    setenv("TZ", "CST-8", 1);
    tzset();

    sntp_init();
}

/* ================================================================
 * 传感器数据发布（每 10s 调用一次）
 * ================================================================ */

static void publish_sensor_data(void)
{
    short ds18b20 = g_sensor_ds18b20_temp;
    int int_part = ds18b20 / 10;
    int frac_part = ds18b20 % 10;
    if (frac_part < 0) frac_part = -frac_part;

    char json[128];
    snprintf(json, sizeof(json),
             "{\"ds18b20\":%d.%d,\"dht11_t\":%d,\"dht11_h\":%d}",
             int_part, frac_part,
             g_sensor_dht11_temp, g_sensor_dht11_humi);

    esp_mqtt_client_publish(s_client, "sensor/esp32s3/data",
                            json, 0, 0, 0);
    ESP_LOGI(TAG, "发布传感器数据: %s", json);
}

/* ================================================================
 * MQTT + 时间同步主任务
 *
 * 等待 WiFi 连接 → SNTP 校时 → MQTT 连接 → 刷新时间
 * ================================================================ */

static void mqtt_time_task(void *pv)
{
    /* ------------------------------------------------------------
     * 第 1 步：等待 WiFi 获取 IP
     * ------------------------------------------------------------ */
    ESP_LOGI(TAG, "等待 WiFi 连接...");
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT,
                        pdFALSE, pdTRUE, portMAX_DELAY);
    ESP_LOGI(TAG, "WiFi 已连接，启动 MQTT + SNTP");

    /* ------------------------------------------------------------
     * 第 2 步：启动 SNTP 校时（在 MQTT 连接前发起，并行运行）
     * ------------------------------------------------------------ */
    initialize_sntp();

    /* ------------------------------------------------------------
     * 第 3 步：启动 MQTT 客户端
     * ------------------------------------------------------------ */
    {
        esp_mqtt_client_config_t cfg = {
            .broker.address.uri = "mqtt://broker.emqx.io:1883",
            .network.timeout_ms = 5000,
            .network.disable_auto_reconnect = false,
        };

        s_client = esp_mqtt_client_init(&cfg);
        if (s_client == NULL) {
            ESP_LOGE(TAG, "MQTT 客户端创建失败");
        } else {
            esp_mqtt_client_register_event(s_client, MQTT_EVENT_ANY,
                                           mqtt_event_handler, NULL);
            esp_mqtt_client_start(s_client);
            ESP_LOGI(TAG, "MQTT 客户端已启动");
        }
    }

    /* ------------------------------------------------------------
     * 第 4 步：等待 SNTP 时间同步（最多等 10 秒）
     * ------------------------------------------------------------ */
    {
        time_t now = 0;
        struct tm timeinfo = { 0 };
        int retry = 0;
        const int max_retry = 10;

        while (timeinfo.tm_year < (2024 - 1900) && ++retry < max_retry) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            time(&now);
            localtime_r(&now, &timeinfo);
        }

        if (retry < max_retry) {
            s_time_synced = true;
            char buf[64];
            strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
            ESP_LOGI(TAG, "时间同步成功: %s", buf);
        } else {
            ESP_LOGW(TAG, "时间同步超时，时间可能不准确");
        }
    }

    /* ------------------------------------------------------------
     * 第 5 步：每秒刷新主页状态栏时间 + 每 10s 上传传感器数据
     * ------------------------------------------------------------ */
    {
        time_t now;
        struct tm timeinfo;
        char time_str[6];       /* "HH:MM\0" */
        int pub_counter = 0;    /* 10s 计数器 */

        while (1) {
            if (s_time_synced) {
                time(&now);
                localtime_r(&now, &timeinfo);
                snprintf(time_str, sizeof(time_str),
                         "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
                oled_display_set_time(time_str);
            }

            /* 每 10s 发布一次传感器数据 */
            if (++pub_counter >= 10 && s_client) {
                pub_counter = 0;
                publish_sensor_data();
            }

            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}

/* ================================================================
 * Base64 编码（轻量实现，无依赖）
 * ================================================================ */

static const char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int base64_encode(const uint8_t *in, size_t in_len, char *out, size_t out_size)
{
    size_t needed = ((in_len + 2) / 3) * 4 + 1;  /* +1 for NUL */
    if (out_size < needed) return -1;

    size_t i = 0, o = 0;
    while (i < in_len) {
        uint8_t a = in[i++];
        uint8_t b = (i < in_len) ? in[i++] : 0;
        uint8_t c = (i < in_len) ? in[i++] : 0;

        out[o++] = b64_table[a >> 2];
        out[o++] = b64_table[((a & 0x03) << 4) | (b >> 4)];
        out[o++] = b64_table[((b & 0x0F) << 2) | (c >> 6)];
        out[o++] = b64_table[c & 0x3F];
    }

    /* Padding */
    size_t rem = in_len % 3;
    if (rem == 1) out[o - 1] = '=', out[o - 2] = '=';
    if (rem == 2) out[o - 1] = '=';
    out[o] = '\0';
    return (int)o;
}

/* ================================================================
 * 公开接口
 * ================================================================ */

bool mqtt_is_connected(void)
{
    return s_client != NULL;
}

void mqtt_publish_audio(const int16_t *pcm, size_t samples)
{
    if (!s_client) return;

    size_t in_bytes = samples * sizeof(int16_t);
    size_t b64_len = ((in_bytes + 2) / 3) * 4 + 16;   /* 稍留余量 */
    char *b64 = malloc(b64_len);
    if (!b64) {
        ESP_LOGE(TAG, "音频发布: Base64 内存不足");
        return;
    }

    int len = base64_encode((const uint8_t *)pcm, in_bytes, b64, b64_len);
    if (len > 0) {
        esp_mqtt_client_publish(s_client, "sensor/esp32s3/audio",
                                b64, (int)len, 0, 0);
        ESP_LOGI(TAG, "音频已发布: %d 样本 → %d bytes Base64", (int)samples, len);
    }
    free(b64);
}

/* ================================================================
 * 音频采集 → MQTT 发布任务
 *
 * 等待 MQTT 就绪 → 启动 PDM 录音 → 每 12 帧（~480ms）累计
 * 发布一次 Base64 PCM 数据到 sensor/esp32s3/audio 主题。
 * ================================================================ */

#define AUDIO_BATCH_FRAMES  12          /* ~480ms */

struct audio_test_ctx {
    int16_t *buffer;                    /* 累计 PCM 缓冲区 */
    int      frame_count;               /* 已收帧数 */
    int      total_frames;              /* 累计总帧数 */
};

static void audio_test_cb(const int16_t *pcm, size_t samples, void *user_data)
{
    struct audio_test_ctx *ctx = (struct audio_test_ctx *)user_data;
    int rms = audio_get_rms();

    /* 串口打印 RMS（每帧都打印，方便观察声音变化） */
    printf("[AUDIO] RMS=%-5d  frame=%d\r", rms, ++ctx->total_frames);

    /* 累计到缓冲区 */
    if (ctx->buffer && samples <= 640) {
        memcpy(ctx->buffer + ctx->frame_count * 640, pcm, samples * sizeof(int16_t));
        ctx->frame_count++;
    }

    /* 累计满一批，经 MQTT 发布 */
    if (ctx->frame_count >= AUDIO_BATCH_FRAMES && mqtt_is_connected()) {
        size_t total_samples = ctx->frame_count * 640;
        printf("\n[AUDIO] 发布 %d 帧 (%d 样本), RMS(max)=%d\n",
               ctx->frame_count, (int)total_samples, rms);
        mqtt_publish_audio(ctx->buffer, total_samples);
        ctx->frame_count = 0;
    }
}

static void audio_test_task(void *pv)
{
    /* 等待 MQTT 就绪 */
    ESP_LOGI(TAG, "等待 MQTT 连接...");
    while (!mqtt_is_connected()) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    ESP_LOGI(TAG, "MQTT 已连接，启动录音");

    /* 分配累计缓冲区：12 帧 × 640 样本 × 2 字节 = 15360 字节 */
    struct audio_test_ctx ctx = {
        .buffer = malloc(AUDIO_BATCH_FRAMES * 640 * sizeof(int16_t)),
        .frame_count = 0,
        .total_frames = 0,
    };
    if (!ctx.buffer) {
        ESP_LOGE(TAG, "缓冲区分配失败");
        vTaskDelete(NULL);
        return;
    }

    /* 启动录音 */
    esp_err_t ret = audio_record_start(audio_test_cb, &ctx);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "启动录音失败: %s", esp_err_to_name(ret));
        free(ctx.buffer);
        vTaskDelete(NULL);
        return;
    }

    /* 录音任务内部循环采集，这里只保持任务存活 */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        /* 每 5 秒报个活 */
        printf("[AUDIO] 持续录音中, 总帧数=%d\n", ctx.total_frames);
    }

    free(ctx.buffer);
    vTaskDelete(NULL);
}

void mqtt_audio_test_start(void)
{
    xTaskCreatePinnedToCore(audio_test_task, "audio_test", 4096, NULL, 3, NULL, 0);
    ESP_LOGI(TAG, "audio_test 任务已创建");
}

/* ================================================================
 * 公开接口 — 启动 MQTT 客户端
 *
 * 创建 mqtt_time_task，内部依次等待 WiFi → SNTP → MQTT 连接。
 * ================================================================ */

void mqtt_app_start(void)
{
    xTaskCreatePinnedToCore(mqtt_time_task, "mqtt_time",
                            4096, NULL, 5, NULL, 0);
    ESP_LOGI(TAG, "mqtt_time_task 已创建");
}
