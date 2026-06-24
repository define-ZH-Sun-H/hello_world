// components/network/http_dl.c
#include "http_dl.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "http_dl";

/* ================================================================
 * http_dl_t 内部结构（对调用方透明）
 * ================================================================ */
struct http_dl_s {
    http_dl_data_cb_t     on_data;
    http_dl_status_cb_t   on_status;
    void                 *user_ctx;
    int64_t               total_read;
    int64_t               content_length;   /* -1 = 未知 */
    int                   last_percent;
    bool                  failed;           /* on_data 返回非 ESP_OK 时置 true */
};

/* ================================================================
 * 进度计算
 * ================================================================ */
static int _calc_percent(int64_t total_read, int64_t content_length)
{
    if (content_length > 0) {
        return (int)(total_read * 100 / content_length);
    }
    /* 无 Content-Length 时粗估（假定 ~1.28MB 固件）*/
    int pct = (int)(total_read / 12800);
    if (pct < 10) pct = 10;
    if (pct > 90) pct = 90;
    return pct;
}

static void _report_progress(http_dl_t *dl, int percent, const char *status)
{
    ESP_LOGI(TAG, "[%d%%] %s", percent, status);
    if (dl->on_status) {
        dl->on_status(dl, percent, status, dl->user_ctx);
    }
}

/* ================================================================
 * HTTP 事件回调 — 代理数据到 on_data
 * ================================================================ */
static esp_err_t _event_handler(esp_http_client_event_t *evt)
{
    http_dl_t *dl = (http_dl_t *)evt->user_data;

    /* 第一次收到数据时尝试获取 Content-Length */
    if (dl->content_length < 0) {
        dl->content_length = esp_http_client_get_content_length(evt->client);
    }

    if (evt->event_id != HTTP_EVENT_ON_DATA || evt->data_len <= 0)
        return ESP_OK;

    /* 跳过 3xx 跳转响应的 body，只处理最终 200 的数据 */
    int http_status = esp_http_client_get_status_code(evt->client);
    if (http_status >= 300)
        return ESP_OK;

    dl->total_read += evt->data_len;

    /* --- 调用数据回调 --- */
    if (dl->on_data) {
        esp_err_t ret = dl->on_data(dl, (const char *)evt->data,
                                     evt->data_len, dl->user_ctx);
        if (ret != ESP_OK) {
            dl->failed = true;
            return ret;
        }
    }

    /* --- 进度报告 --- */
    int pct = _calc_percent(dl->total_read, dl->content_length);
    if (pct != dl->last_percent) {
        dl->last_percent = pct;
        _report_progress(dl, pct, "下载中...");
    }

    return ESP_OK;
}

/* ================================================================
 * 公共 API
 * ================================================================ */
esp_err_t http_dl_perform(const http_dl_config_t *cfg)
{
    /* --- 参数校验 --- */
    if (!cfg || !cfg->url || !cfg->on_data) {
        ESP_LOGE(TAG, "无效参数: url=%p on_data=%p", cfg ? cfg->url : NULL,
                 cfg ? (void *)cfg->on_data : NULL);
        return ESP_ERR_INVALID_ARG;
    }

    int max_retries  = cfg->max_retries  > 0 ? cfg->max_retries  : 3;
    int retry_ms     = cfg->retry_interval_ms > 0 ? cfg->retry_interval_ms : 5000;
    int timeout      = cfg->timeout_ms   > 0 ? cfg->timeout_ms   : 60000;
    int buf_size     = cfg->buffer_size  > 0 ? cfg->buffer_size  : 16384;
    int max_redir    = cfg->max_redirect > 0 ? cfg->max_redirect : 5;
    const char *ua   = cfg->user_agent   ? cfg->user_agent
                                             : "ESP32S3-HttpDL/1.0";

    esp_err_t result = ESP_FAIL;

    for (int attempt = 0; attempt < max_retries; attempt++) {
        /* 每次重试重新初始化上下文 */
        http_dl_t dl_ctx = {
            .on_data        = cfg->on_data,
            .on_status      = cfg->on_status,
            .user_ctx       = cfg->user_ctx,
            .total_read     = 0,
            .content_length = -1,
            .last_percent   = -1,
            .failed         = false,
        };

        if (attempt > 0) {
            ESP_LOGW(TAG, "下载失败，第 %d/%d 次重试...", attempt + 1, max_retries);
            _report_progress(&dl_ctx, 10, "重试中...");
            vTaskDelay(pdMS_TO_TICKS(retry_ms));
        }

        /* 配置 HTTP 客户端 */
        esp_http_client_config_t http_cfg = {
            .url                   = cfg->url,
            .method                = HTTP_METHOD_GET,
            .timeout_ms            = timeout,
            .user_agent            = ua,
            .buffer_size           = buf_size,
            .buffer_size_tx        = 0,
            .event_handler         = _event_handler,
            .user_data             = &dl_ctx,
            .max_redirection_count = max_redir,
        };
        if (cfg->crt_bundle_attach) {
            http_cfg.crt_bundle_attach = esp_crt_bundle_attach;
        }

        esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
        if (!client) {
            ESP_LOGE(TAG, "HTTP 客户端初始化失败");
            continue;
        }

        /* 执行 HTTP 请求 */
        esp_err_t err = esp_http_client_perform(client);
        int status = esp_http_client_get_status_code(client);

        bool ok = (err == ESP_OK && status == 200 && !dl_ctx.failed);

        if (ok) {
            /* 完整性检查 */
            if (!esp_http_client_is_complete_data_received(client)) {
                ESP_LOGE(TAG, "下载不完整，数据缺失");
                esp_http_client_cleanup(client);
                continue;
            }
            ESP_LOGI(TAG, "下载完成: %lld bytes", dl_ctx.total_read);
            esp_http_client_cleanup(client);
            result = ESP_OK;
            break;  /* 成功退出重试循环 */
        }

        /* 失败处理 */
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "HTTP 请求失败: %s (status=%d)",
                     esp_err_to_name(err), status);
        } else if (status != 200) {
            ESP_LOGE(TAG, "HTTP 返回 %d", status);
        } else if (dl_ctx.failed) {
            ESP_LOGE(TAG, "下载被 on_data 回调中止");
        }

        if (dl_ctx.failed) {
            /* on_data 返回错误 → 不重试 */
            esp_http_client_cleanup(client);
            break;
        }

        esp_http_client_cleanup(client);
    }

    return result;
}
