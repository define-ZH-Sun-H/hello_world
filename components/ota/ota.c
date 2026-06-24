// components/ota/ota.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_wifi.h"
#include "http_dl.h"
#include "ota.h"
#include "ota_gitee.h"

static const char *TAG = "ota";

/* ================================================================
 * 固件头校验大小
 * ================================================================ */
/** 足够容纳 esp_image_header_t + esp_image_segment_header_t + esp_app_desc_t */
#define HEADER_SIZE  (sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t))

/* ================================================================
 * 版本号比较
 * ================================================================ */
/**
 * @brief 语义化版本比较，支持 "v" 前缀和 major.minor.patch
 *        "v1.10.0" > "v1.2.0" 正确排序
 * @return <0: v1 < v2, 0: 相等, >0: v1 > v2
 */
static int compare_versions(const char *v1, const char *v2)
{
    int m1 = 0, n1 = 0, p1 = 0;
    int m2 = 0, n2 = 0, p2 = 0;

    if (*v1 == 'v' || *v1 == 'V') v1++;
    if (*v2 == 'v' || *v2 == 'V') v2++;

    int a = sscanf(v1, "%d.%d.%d", &m1, &n1, &p1);
    int b = sscanf(v2, "%d.%d.%d", &m2, &n2, &p2);

    if (a < 3 || b < 3) return strcmp(v1, v2);

    if (m1 != m2) return m1 - m2;
    if (n1 != n2) return n1 - n2;
    return p1 - p2;
}

/* ================================================================
 * 内部状态
 * ================================================================ */

/** 进度回调函数指针 */
static ota_progress_cb_t s_progress_cb = NULL;

/** 是否正在升级中（防重入），由 s_ota_mutex 保护 */
static bool s_ota_running = false;
static SemaphoreHandle_t s_ota_mutex = NULL;

/* ================================================================
 * OTA 下载上下文（供 _on_data_cb 使用）
 * ================================================================ */
typedef struct {
    esp_ota_handle_t handle;
    const esp_partition_t *update_partition;
    bool                   ota_begun;
    bool                   header_fatal;
    bool                   header_checked;
    uint8_t                header_buf[HEADER_SIZE];
    size_t                 header_bytes;
    const char            *version;
} ota_dl_ctx_t;

/**
 * @brief OTA 数据回调（由 http_dl_perform 的 on_data 调用）
 *
 * 策略：先缓冲固件头，校验通过后再调用 esp_ota_begin 开始写入。
 * 返回 ESP_FAIL 会触发 http_dl 中止所有重试。
 */
static esp_err_t _on_data_cb(http_dl_t *dl, const char *data, size_t len, void *user_ctx)
{
    ota_dl_ctx_t *ctx = (ota_dl_ctx_t *)user_ctx;

    /* ---- Phase 1：缓冲并校验固件头 ---- */
    if (!ctx->header_checked) {
        size_t room = HEADER_SIZE - ctx->header_bytes;
        size_t copy = (len < room) ? len : room;
        memcpy(ctx->header_buf + ctx->header_bytes, data, copy);
        ctx->header_bytes += copy;

        if (ctx->header_bytes >= HEADER_SIZE) {
            ctx->header_checked = true;

            const esp_app_desc_t *new_app = (const esp_app_desc_t *)
                &ctx->header_buf[sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t)];

            ESP_LOGI(TAG, "新固件: %s v%s", new_app->project_name, new_app->version);

            /* 1) 检查版本是否与当前相同 */
            const esp_partition_t *running = esp_ota_get_running_partition();
            esp_app_desc_t running_app;
            if (running && esp_ota_get_partition_description(running, &running_app) == ESP_OK) {
                ESP_LOGI(TAG, "对比版本: 新固件=\"%s\" 当前=\"%s\"",
                         new_app->version, running_app.version);
                if (memcmp(new_app->version, running_app.version,
                           sizeof(new_app->version)) == 0) {
                    ESP_LOGW(TAG, "固件版本与当前相同，跳过");
                    ctx->header_fatal = true;
                    return ESP_FAIL;
                }
            }

            /* 2) 检查是否是之前回滚过的坏版本 */
            const esp_partition_t *invalid = esp_ota_get_last_invalid_partition();
            if (invalid != NULL) {
                esp_app_desc_t invalid_app;
                if (esp_ota_get_partition_description(invalid, &invalid_app) == ESP_OK) {
                    if (memcmp(new_app->version, invalid_app.version,
                               sizeof(new_app->version)) == 0) {
                        ESP_LOGW(TAG, "此版本之前刷入失败已回滚，不再重试");
                        ctx->header_fatal = true;
                        return ESP_FAIL;
                    }
                }
            }

            /* 3) 通过校验，开始 OTA 写入 */
            esp_err_t e = esp_ota_begin(ctx->update_partition,
                                        OTA_SIZE_UNKNOWN, &ctx->handle);
            if (e != ESP_OK) {
                ESP_LOGE(TAG, "esp_ota_begin 失败: %s", esp_err_to_name(e));
                ctx->header_fatal = true;
                return e;
            }
            ctx->ota_begun = true;

            /* 写入已缓冲的头部数据 */
            e = esp_ota_write(ctx->handle, ctx->header_buf, ctx->header_bytes);
            if (e != ESP_OK) return e;
        }

        size_t consumed = copy;
        data += consumed;
        len   -= consumed;
    }

    /* ---- Phase 2：写入剩余数据 ---- */
    if (len > 0 && ctx->ota_begun) {
        esp_err_t e = esp_ota_write(ctx->handle, data, len);
        if (e != ESP_OK) return e;
    }

    return ESP_OK;
}

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

    if (!ota_gitee_check(&latest_tag, &dl_url)) {
        ESP_LOGE(TAG, "版本检查失败");
        return false;
    }

    /* 比较版本号字符串 */
    if (compare_versions(latest_tag, FIRMWARE_VERSION) <= 0) {
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

    /* 关闭 WiFi 省电模式，防止下载中断连 */
    esp_wifi_set_ps(WIFI_PS_NONE);

    esp_err_t ret = ESP_FAIL;
    esp_err_t err;
    const esp_partition_t *update_partition = NULL;
    esp_ota_handle_t update_handle = 0;

    /* ------------------------------------------------------------
     * 第 1 步：GitHub 版本检查 + 获取下载 URL
     * ------------------------------------------------------------ */
    report_progress(0, "检查新版本...");

    const char *latest_tag;
    const char *dl_url;
    if (!ota_gitee_check(&latest_tag, &dl_url)) {
        ESP_LOGE(TAG, "版本检查失败");
        report_progress(0, "版本检查失败");
        goto cleanup;
    }

    /* 比对版本号 */
    if (compare_versions(latest_tag, FIRMWARE_VERSION) <= 0) {
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
     * 第 3 步：HTTP 下载 + 固件头校验
     *
     * 下载器内置重试，调用 http_dl_perform 一键执行。
     * on_data 回调中完成头校验，校验不通过返回 ESP_FAIL，不重试。
     * ------------------------------------------------------------ */
    ota_dl_ctx_t ota_ctx = {
        .update_partition = update_partition,
        .version          = latest_tag,
    };

    http_dl_config_t dl_cfg = {
        .url               = dl_url,
        .timeout_ms        = 60000,
        .max_retries       = 3,
        .retry_interval_ms = 5000,
        .on_data           = _on_data_cb,
        .user_ctx          = &ota_ctx,
    };

    report_progress(10, "下载中...");

    esp_err_t http_err = http_dl_perform(&dl_cfg);

    if (http_err != ESP_OK || ota_ctx.header_fatal) {
        if (ota_ctx.header_fatal) {
            ESP_LOGE(TAG, "固件头校验失败，不重试");
        } else {
            ESP_LOGE(TAG, "下载失败已达最大重试次数");
        }
        if (ota_ctx.ota_begun) esp_ota_abort(ota_ctx.handle);
        goto cleanup;
    }

    update_handle = ota_ctx.handle;
    report_progress(95, "校验中...");

    /* ------------------------------------------------------------
     * 第 4 步：结束 OTA 并设置启动分区
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
