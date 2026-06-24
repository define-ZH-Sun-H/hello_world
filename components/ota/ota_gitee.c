// components/ota/ota_gitee.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "ota_gitee.h"

static const char *TAG = "ota_gitee";

/* Gitee API 配置 */
#define GITEE_OWNER      "Define_ZH_S"
#define GITEE_REPO       "hello_world"
#define GITEE_API_URL    "https://gitee.com/api/v5/repos/" GITEE_OWNER "/" GITEE_REPO "/releases/latest"
#define EXPECTED_ASSET   "hello_world.bin"

/* 内部缓存，避免重复 malloc */
static char s_tag[32]   = {0};
static char s_url[512]  = {0};

/* HTTP 响应收集缓冲区 */
typedef struct {
    char *data;
    size_t len;
    size_t cap;
} resp_buf_t;

static esp_err_t _http_evt_handler(esp_http_client_event_t *evt)
{
    resp_buf_t *buf = (resp_buf_t *)evt->user_data;

    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (buf->len + evt->data_len + 1 > buf->cap) {
            size_t new_cap = buf->cap ? buf->cap * 2 : 1024;
            while (buf->len + evt->data_len + 1 > new_cap) new_cap *= 2;
            char *p = realloc(buf->data, new_cap);
            if (!p) return ESP_ERR_NO_MEM;
            buf->data = p;
            buf->cap  = new_cap;
        }
        memcpy(buf->data + buf->len, evt->data, evt->data_len);
        buf->len += evt->data_len;
        buf->data[buf->len] = '\0';
        break;

    default:
        break;
    }
    return ESP_OK;
}

bool ota_gitee_check(const char **tag_name, const char **dl_url)
{
    resp_buf_t buf = {0};

    esp_http_client_config_t cfg = {
        .url = GITEE_API_URL,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 10000,
        .user_agent = "ESP32S3-OTA/1.0",
        .event_handler = _http_evt_handler,
        .user_data = &buf,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        ESP_LOGE(TAG, "HTTP 客户端初始化失败");
        return false;
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP 请求失败: %s", esp_err_to_name(err));
        goto fail;
    }

    int status = esp_http_client_get_status_code(client);
    if (status != 200) {
        ESP_LOGE(TAG, "Gitee API 返回 %d", status);
        goto fail;
    }

    if (!buf.data || buf.len == 0) {
        ESP_LOGE(TAG, "响应为空");
        goto fail;
    }

    ESP_LOGD(TAG, "API 响应: %s", buf.data);

    /* 解析 JSON */
    cJSON *root = cJSON_Parse(buf.data);
    if (!root) {
        ESP_LOGE(TAG, "JSON 解析失败");
        goto fail;
    }

    /* 提取 tag_name */
    cJSON *tag = cJSON_GetObjectItem(root, "tag_name");
    if (!cJSON_IsString(tag)) {
        ESP_LOGE(TAG, "JSON 缺少 tag_name 字段");
        cJSON_Delete(root);
        goto fail;
    }
    strncpy(s_tag, tag->valuestring, sizeof(s_tag) - 1);
    s_tag[sizeof(s_tag) - 1] = '\0';
    ESP_LOGI(TAG, "最新版本: %s", s_tag);

    /* 提取 assets 中 name = hello_world.bin 的 download_url */
    cJSON *assets = cJSON_GetObjectItem(root, "assets");
    if (!cJSON_IsArray(assets)) {
        ESP_LOGE(TAG, "JSON 缺少 assets 数组");
        cJSON_Delete(root);
        goto fail;
    }

    bool found = false;
    cJSON *asset;
    cJSON_ArrayForEach(asset, assets) {
        cJSON *name = cJSON_GetObjectItem(asset, "name");
        if (!cJSON_IsString(name)) continue;
        if (strcmp(name->valuestring, EXPECTED_ASSET) != 0) continue;

        cJSON *url = cJSON_GetObjectItem(asset, "browser_download_url");
        if (!cJSON_IsString(url)) continue;

        strncpy(s_url, url->valuestring, sizeof(s_url) - 1);
        s_url[sizeof(s_url) - 1] = '\0';
        found = true;
        break;
    }

    cJSON_Delete(root);

    if (!found) {
        ESP_LOGE(TAG, "未找到 %s", EXPECTED_ASSET);
        goto fail;
    }

    ESP_LOGI(TAG, "固件下载 URL: %s", s_url);

    free(buf.data);
    esp_http_client_cleanup(client);

    *tag_name = s_tag;
    *dl_url   = s_url;
    return true;

fail:
    free(buf.data);
    esp_http_client_cleanup(client);
    return false;
}
