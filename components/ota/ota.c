// components/ota/ota.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "ota.h"
#include "ota_github.h"

static const char *TAG = "ota";

/* ================================================================
 * 内部状态
 * ================================================================ */

/** 进度回调函数指针 */
static ota_progress_cb_t s_progress_cb = NULL;

/** 是否正在升级中（防重入），由 s_ota_mutex 保护 */
static bool s_ota_running = false;
static SemaphoreHandle_t s_ota_mutex = NULL;

/* ================================================================
 * 进度回调辅助
 * ================================================================ */

static void report_progress(int percent, const char *status)
{
    ESP_LOGI(TAG, "[%d%%] %s", percent, status);
    if (s_progress_cb) {
        s_progress_cb(percent, status);
    }
}

/* ================================================================
 * 版本比对
 * ================================================================ */

bool ota_check_new_version(void)
{
    const char *latest_tag;
    const char *dl_url;

    if (!ota_github_check(&latest_tag, &dl_url)) {
        ESP_LOGE(TAG, "版本检查失败");
        return false;
    }

    /* 比较版本号字符串 */
    if (strcmp(latest_tag, FIRMWARE_VERSION) == 0) {
        ESP_LOGI(TAG, "已是最新版本: %s", FIRMWARE_VERSION);
        return false;
    }

    ESP_LOGI(TAG, "发现新版本: %s (当前: %s)", latest_tag, FIRMWARE_VERSION);
    return true;
}

/* ================================================================
 * OTA 升级主流程
 * ================================================================ */

esp_err_t ota_start(void)
{
    /* 互斥锁防重入（MQTT 任务和定时任务可能并发） */
    if (!s_ota_mutex) {
        s_ota_mutex = xSemaphoreCreateMutex();
    }
    if (!xSemaphoreTake(s_ota_mutex, pdMS_TO_TICKS(10))) {
        ESP_LOGW(TAG, "OTA 已在执行中");
        return ESP_FAIL;
    }
    if (s_ota_running) {
        xSemaphoreGive(s_ota_mutex);
        ESP_LOGW(TAG, "OTA 已在执行中");
        return ESP_FAIL;
    }
    s_ota_running = true;
    xSemaphoreGive(s_ota_mutex);

    esp_err_t ret = ESP_FAIL;
    const esp_partition_t *update_partition = NULL;
    esp_ota_handle_t update_handle = 0;

    /* ------------------------------------------------------------
     * 第 1 步：GitHub 版本检查 + 获取下载 URL
     * ------------------------------------------------------------ */
    report_progress(0, "检查新版本...");

    const char *latest_tag;
    const char *dl_url;
    if (!ota_github_check(&latest_tag, &dl_url)) {
        ESP_LOGE(TAG, "版本检查失败");
        report_progress(0, "版本检查失败");
        goto cleanup;
    }

    /* 比对版本号 */
    if (strcmp(latest_tag, FIRMWARE_VERSION) == 0) {
        ESP_LOGI(TAG, "已是最新版本: %s", FIRMWARE_VERSION);
        report_progress(100, "已是最新版本");
        ret = ESP_OK;
        goto cleanup;
    }

    char ver_str[64];
    snprintf(ver_str, sizeof(ver_str), "正在下载: %s", latest_tag);
    report_progress(5, ver_str);
    ESP_LOGI(TAG, "新版本 %s → %s", latest_tag, dl_url);

    /* ------------------------------------------------------------
     * 第 2 步：获取 OTA 分区
     * ------------------------------------------------------------ */
    update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        ESP_LOGE(TAG, "未找到 OTA 分区");
        report_progress(0, "未找到 OTA 分区");
        goto cleanup;
    }
    ESP_LOGI(TAG, "写入分区: %s @ 0x%X",
             update_partition->label, update_partition->address);

    /* ------------------------------------------------------------
     * 第 3 步：准备 HTTP 下载
     * ------------------------------------------------------------ */
    esp_http_client_config_t cfg = {
        .url = dl_url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 30000,
        .user_agent = "ESP32S3-OTA/1.0",
    };

    esp_http_client_handle_t http = esp_http_client_init(&cfg);
    if (!http) {
        ESP_LOGE(TAG, "HTTP 客户端初始化失败");
        goto cleanup;
    }

    esp_err_t err = esp_http_client_open(http, 0);   /* GET 请求 */
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP 连接失败: %s", esp_err_to_name(err));
        esp_http_client_cleanup(http);
        goto cleanup;
    }

    int64_t content_length = esp_http_client_fetch_headers(http);
    if (content_length <= 0) {
        ESP_LOGE(TAG, "无效的 Content-Length: %lld", content_length);
        esp_http_client_close(http);
        esp_http_client_cleanup(http);
        goto cleanup;
    }

    ESP_LOGI(TAG, "固件大小: %lld bytes", content_length);

    /* ------------------------------------------------------------
     * 第 4 步：开始 OTA
     * ------------------------------------------------------------ */
    err = esp_ota_begin(update_partition, content_length, &update_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin 失败: %s", esp_err_to_name(err));
        esp_http_client_close(http);
        esp_http_client_cleanup(http);
        goto cleanup;
    }

    report_progress(10, "下载中...");

    /* 分块下载并写入 */
    char *buf = malloc(4096);
    if (!buf) {
        ESP_LOGE(TAG, "分配下载缓冲区失败");
        esp_ota_abort(update_handle);
        esp_http_client_close(http);
        esp_http_client_cleanup(http);
        goto cleanup;
    }

    int64_t total_read = 0;
    int last_percent = 10;

    while (1) {
        int read_len = esp_http_client_read(http, buf, 4096);
        if (read_len < 0) {
            ESP_LOGE(TAG, "下载错误: %s (%d)", esp_err_to_name(read_len), read_len);
            free(buf);
            esp_ota_abort(update_handle);
            esp_http_client_close(http);
            esp_http_client_cleanup(http);
            goto cleanup;
        }
        if (read_len == 0) break;   /* 下载完成 */

        err = esp_ota_write(update_handle, (const void *)buf, read_len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write 失败: %s", esp_err_to_name(err));
            free(buf);
            esp_ota_abort(update_handle);
            esp_http_client_close(http);
            esp_http_client_cleanup(http);
            goto cleanup;
        }

        total_read += read_len;
        int percent = 10 + (int)(90 * total_read / content_length);
        if (percent != last_percent) {
            report_progress(percent, "下载中...");
            last_percent = percent;
        }
    }

    free(buf);
    esp_http_client_close(http);
    esp_http_client_cleanup(http);

    if (total_read != content_length) {
        ESP_LOGE(TAG, "下载不完整: %lld / %lld", total_read, content_length);
        esp_ota_abort(update_handle);
        goto cleanup;
    }

    report_progress(95, "校验中...");

    /* ------------------------------------------------------------
     * 第 5 步：结束 OTA 并设置启动分区
     * ------------------------------------------------------------ */
    err = esp_ota_end(update_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end 失败: %s", esp_err_to_name(err));
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
            ESP_LOGE(TAG, "固件校验失败");
        }
        goto cleanup;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition 失败: %s", esp_err_to_name(err));
        goto cleanup;
    }

    report_progress(100, "升级完成，准备重启");
    ESP_LOGI(TAG, "升级成功，3 秒后重启...");

    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();
    return ESP_OK;  /* 不会执行到这里 */

cleanup:
    s_ota_running = false;
    report_progress(0, "升级失败");
    return ret;
}

/* ================================================================
 * 定时检查任务
 * ================================================================ */

/** 检查间隔：7 天（单位：滴答数） */
#define CHECK_INTERVAL_TICKS  (7 * 24 * 3600 * 1000 / portTICK_PERIOD_MS)

static void periodic_check_task(void *pv)
{
    while (1) {
        vTaskDelay(CHECK_INTERVAL_TICKS);
        ESP_LOGI(TAG, "定时检查新版本...");
        if (ota_check_new_version()) {
            ota_start();
        }
    }
}

void ota_periodic_check_start(void)
{
    /* 使用动态任务，无需静态分配 */
    xTaskCreate(periodic_check_task, "ota_check", 8192, NULL, 1, NULL);
    ESP_LOGI(TAG, "定时检查任务已启动（周期 7 天）");
}

/* ================================================================
 * 进度回调注册
 * ================================================================ */

void ota_register_progress_callback(ota_progress_cb_t cb)
{
    s_progress_cb = cb;
}
