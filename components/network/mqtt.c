/**
 * @file mqtt.c
 * @brief MQTT 客户端 — 连接 Broker + SNTP 校时 + 状态栏时间
 *
 * 流程：
 *   mqtt_app_start()
 *     └→ 创建 mqtt_time_task
 *          1. 等待 WiFi 就绪（WIFI_CONNECTED_BIT）
 *          2. 启动 SNTP（pool.ntp.org）+ MQTT 客户端（并行）
 *          3. 每秒更新主页状态栏时间 + 每 10s 发布传感器数据
 *
 * 外部依赖：
 *   - wifi.h / wifi_event_group（WiFi 状态通知）
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

#include "wifi.h"
#include "sntp.h"
#include "sensor_init.h"
#include "audio.h"
#include "ota.h"

/* ================================================================
 * 静态创建所需内存
 * ================================================================ */
static StackType_t s_mqtt_time_stack[4096];
static StaticTask_t s_mqtt_time_tcb;
static StackType_t s_audio_test_stack[4096];
static StaticTask_t s_audio_test_tcb;

static const char *TAG = "mqtt";

/* MQTT 客户端句柄（模块内部） */
static esp_mqtt_client_handle_t s_client = NULL;

/* ================================================================
 * MQTT 事件回调
 *
 * 所有操作必须轻量，不要做阻塞调用。
 * ================================================================ */

/**
 * @brief MQTT 事件回调
 *
 * 处理 MQTT 客户端生命周期中的各类事件。
 * 所有操作必须轻量，不做阻塞调用。
 *
 * @param handler_args 用户注册时传入的参数（未使用）
 * @param base         事件基类
 * @param event_id     事件 ID（CONNECTED / DISCONNECTED / DATA / ERROR 等）
 * @param event_data   事件数据指针
 */
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
        printf("[MQTT] 收到消息: %.*s = %.*s\n",
               event->topic_len, event->topic,
               event->data_len, event->data);

        /* OTA 升级触发 */
        if (event->topic_len == sizeof("cmd/esp32s3/ota") - 1
            && memcmp(event->topic, "cmd/esp32s3/ota", sizeof("cmd/esp32s3/ota") - 1) == 0) {
            ESP_LOGI(TAG, "收到 OTA 指令，开始升级");
            ota_start();
        }
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT 错误");
        break;

    default:
        break;
    }
}

/* ================================================================
 * 传感器数据发布（每 10s 调用一次）
 * ================================================================ */

/**
 * @brief 发布传感器数据到 MQTT
 *
 * 将 DS18B20 温度、DHT11 温湿度打包为 JSON，
 * 发布到 sensor/esp32s3/data 主题。
 *
 * @return void
 */
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
 * MQTT 客户端初始化（封装）
 * ================================================================ */

/**
 * @brief 初始化并启动 MQTT 客户端
 *
 * 创建 MQTT 客户端实例、注册事件回调、启动连接。
 * 连接过程在 esp-mqtt 内部任务中进行，本函数不阻塞。
 *
 * @return esp_mqtt_client_handle_t  成功返回客户端句柄，失败返回 NULL
 */
static esp_mqtt_client_handle_t mqtt_client_startup(void)
{
    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = "mqtt://broker.emqx.io:1883",
        .network.timeout_ms = 5000,
        .network.disable_auto_reconnect = false,
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&cfg);
    if (client == NULL) {
        ESP_LOGE(TAG, "MQTT 客户端创建失败");
        return NULL;
    }

    esp_mqtt_client_register_event(client, MQTT_EVENT_ANY,
                                   mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
    ESP_LOGI(TAG, "MQTT 客户端已启动");
    return client;
}

/**
 * @brief MQTT 主任务
 *
 * 等待 WiFi → 启动 MQTT → 每秒刷新主页时间 + 每 10s 发布传感器。
 * SNTP 校时由 main.c 在 wifi_init_sta() 之后单独启动，不在此处阻塞。
 *
 * @param pv 未使用
 */
static void mqtt_time_task(void *pv)
{
    /* ------------------------------------------------------------
     * 第 1 步：等待 WiFi 获取 IP（最多等 15 秒，无 WiFi 则跳过）
     * ------------------------------------------------------------ */
    ESP_LOGI(TAG, "等待 WiFi 连接...");
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT,
                                           pdFALSE, pdTRUE, pdMS_TO_TICKS(15000));
    if (!(bits & WIFI_CONNECTED_BIT)) {
        ESP_LOGW(TAG, "WiFi 未就绪，跳过 MQTT 连接");
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "WiFi 已连接，启动 MQTT");

    /* ------------------------------------------------------------
     * 第 2 步：启动 MQTT 客户端（SNTP 已在 main.c 中启动）
     * ------------------------------------------------------------ */
    s_client = mqtt_client_startup();

    /* ------------------------------------------------------------
     * 第 3 步：每秒刷新主页状态栏时间 + 每 10s 上传传感器数据
     * ------------------------------------------------------------ */
    {
        time_t now;
        struct tm timeinfo;
        char time_str[6];       /* "HH:MM\0" */
        int pub_counter = 0;    /* 10s 计数器 */

        while (1) {
            if (sntp_is_synced()) {
                time(&now);
                localtime_r(&now, &timeinfo);
                snprintf(time_str, sizeof(time_str),
                         "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
                // oled_display_set_time(time_str);  /* TODO: 新 LVGL 显示系统接入后恢复 */
            }

            /* 每 10s 发布一次传感器数据（当前已暂停） */
            if (++pub_counter >= 10 && s_client) {
                pub_counter = 0;
                // publish_sensor_data();
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

/**
 * @brief Base64 编码（轻量实现，无外部依赖）
 *
 * 将二进制数据编码为 Base64 字符串。
 * 输出末尾自动添加 NUL 终止符。
 *
 * @param in       输入数据缓冲区
 * @param in_len   输入数据长度（字节）
 * @param out      输出字符串缓冲区
 * @param out_size 输出缓冲区大小（字节）
 *
 * @return 编码后字符串长度（不含 NUL），-1 表示缓冲区不足
 */
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

/**
 * @brief 检查 MQTT 客户端是否已连接
 *
 * @return true 已连接，false 未连接
 */
bool mqtt_is_connected(void)
{
    return s_client != NULL;
}

/**
 * @brief 发布音频 PCM 数据到 MQTT
 *
 * 将 PCM 样本数据进行 Base64 编码后，发布到 sensor/esp32s3/audio 主题。
 * 内部动态分配编码缓冲区，使用后自动释放。
 *
 * @param pcm     PCM 数据（16-bit signed mono）
 * @param samples 样本数
 *
 * @return void
 */
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

/**
 * @brief 音频采集回调函数
 *
 * 每帧 PCM 数据到达时由 audio_record_start 调用。
 * 累计帧数据到缓冲区，攒满 AUDIO_BATCH_FRAMES 帧后经 MQTT 发布。
 *
 * @param pcm       PCM 帧数据（16-bit signed mono）
 * @param samples   本帧样本数（通常 640）
 * @param user_data 指向 audio_test_ctx 结构体的指针
 */
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

/**
 * @brief 音频采集 → MQTT 发布任务
 *
 * 等待 MQTT 就绪后启动 PDM 录音，每 12 帧（~480ms）累计
 * 发布一次 Base64 PCM 数据到 sensor/esp32s3/audio 主题。
 *
 * @param pv 未使用
 */
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

/**
 * @brief 启动音频采集 → MQTT 发布任务
 *
 * 创建 audio_test_task，等待 MQTT 连接就绪后自动启动 PDM 麦克风录音。
 *
 * @return void
 */
void mqtt_audio_test_start(void)
{
    xTaskCreateStaticPinnedToCore(audio_test_task, "audio_test",
        4096, NULL, 3,
        s_audio_test_stack, &s_audio_test_tcb,
        0);  /* Core 0：音频数据最终走 MQTT→网络栈 */
    ESP_LOGI(TAG, "audio_test 任务已创建");
}

/* ================================================================
 * 公开接口 — 启动 MQTT 客户端
 *
 * 创建 mqtt_time_task，内部依次等待 WiFi → SNTP + MQTT。
 * ================================================================ */

/**
 * @brief 启动 MQTT 客户端
 *
 * 创建 mqtt_time_task，内部依次等待 WiFi → 启动 SNTP + MQTT → 刷新时间。
 * 时间同步由 SNTP 回调异步通知，不再阻塞等待。
 * 调用本函数前必须已初始化 WiFi。
 *
 * @return void
 */
void mqtt_app_start(void)
{
    xTaskCreateStaticPinnedToCore(mqtt_time_task, "mqtt_time",
        4096, NULL, 5,
        s_mqtt_time_stack, &s_mqtt_time_tcb,
        0);  /* Core 0：网络协议任务，和 WiFi 栈同核 */
    ESP_LOGI(TAG, "mqtt_time_task 已创建");
}
