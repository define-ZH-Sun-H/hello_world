# HTTP 下载器模块化 — 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 `components/ota/ota.c` 中的 HTTP 下载代码抽离到 `components/network/http_dl`，实现通用 HTTP 流式下载器，OTA 通过回调使用。

**Architecture:** 新建 `http_dl` 模块（~100 行），暴露 `http_dl_perform()` 一键下载 API。内部封装 `esp_http_client` 的 init/perform/cleanup 和可配置重试。调用方通过 `on_data` 回调逐块接收数据。`ota.c` 中的 HTTP 代码段替换为 `http_dl_perform(&cfg)`。

**Tech Stack:** ESP-IDF, C11, esp_http_client

## Global Constraints

- 不修改 `ota_gitee.c/h`（REST API 调用是请求-响应模式，与流式下载无关）
- 不修改 `ota.h`（对外 API 不变）
- `http_dl_t` 结构体定义隐藏在 `.c` 文件中，对调用方透明
- `on_data` 返回非 `ESP_OK` → 中止下载，不重试
- 进度百分比：优先用 Content-Length 精确计算，无可获取时粗估（假定 ~1.28MB）
- 记录改动 = 保存代码到文件 + 在报告中记录修改摘要，非 git commit

---

### Task 1: 创建 `http_dl.h` — 公开 API 和类型定义

**Files:**
- Create: `components/network/http_dl.h`

**Interfaces:**
- Produces: `http_dl_t` (opaque), `http_dl_data_cb_t`, `http_dl_status_cb_t`, `http_dl_config_t`, `http_dl_perform()`

- [ ] **Step 1: 创建 http_dl.h**

```c
// components/network/http_dl.h
#ifndef HTTP_DL_H
#define HTTP_DL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief HTTP 下载会话句柄（Opaque）
 *
 * 由 http_dl_perform 内部创建，通过回调参数暴露给调用方。
 * 结构体定义隐藏在 http_dl.c 中。
 */
typedef struct http_dl_s http_dl_t;

/**
 * @brief 数据接收回调
 *
 * 每收到一块 HTTP 响应体数据时调用。
 * @param dl      下载会话句柄
 * @param data    数据块指针
 * @param len     数据块长度
 * @param user_ctx 用户上下文（来自 http_dl_config_t::user_ctx）
 * @return ESP_OK 继续下载；其他值中止下载（不重试）
 */
typedef esp_err_t (*http_dl_data_cb_t)(http_dl_t *dl,
    const char *data, size_t len, void *user_ctx);

/**
 * @brief 进度/状态回调
 *
 * 下载进度或状态变更时调用。
 * @param dl       下载会话句柄
 * @param percent  进度百分比（0-100）
 * @param status   状态描述字符串
 * @param user_ctx 用户上下文
 */
typedef void (*http_dl_status_cb_t)(http_dl_t *dl,
    int percent, const char *status, void *user_ctx);

/**
 * @brief HTTP 下载配置
 */
typedef struct {
    const char           *url;               /**< 下载 URL（必填）*/
    int                   timeout_ms;        /**< HTTP 超时 ms，默认 60000 */
    int                   max_retries;       /**< 最大重试次数，默认 3 */
    int                   retry_interval_ms; /**< 重试间隔 ms，默认 5000 */
    bool                  crt_bundle_attach; /**< 加载 CA 证书包，默认 true */
    int                   buffer_size;       /**< 接收缓冲区大小，默认 16384 */
    int                   max_redirect;      /**< 最大重定向次数，默认 5 */
    const char           *user_agent;        /**< User-Agent 字符串 */
    http_dl_data_cb_t     on_data;           /**< 数据回调（必填）*/
    http_dl_status_cb_t   on_status;         /**< 进度回调（可选）*/
    void                 *user_ctx;          /**< 用户上下文指针 */
} http_dl_config_t;

/**
 * @brief 执行 HTTP 下载（同步，阻塞）
 *
 * 完整流程：参数校验 → 重试循环（HTTP init → perform → cleanup）。
 * 数据通过 on_data 回调逐块投递，进度通过 on_status 报告。
 *
 * @param cfg 下载配置（url 和 on_data 必须非空）
 * @return ESP_OK 下载成功；ESP_ERR_INVALID_ARG 参数无效；ESP_FAIL 下载失败
 */
esp_err_t http_dl_perform(const http_dl_config_t *cfg);

#ifdef __cplusplus
}
#endif

#endif /* HTTP_DL_H */
```

- [ ] **Step 2: 记录改动**

文件 `components/network/http_dl.h` 已创建，定义了 http_dl 模块的公开 API。

---

### Task 2: 创建 `http_dl.c` + 更新 `network/CMakeLists.txt`

**Files:**
- Create: `components/network/http_dl.c`
- Modify: `components/network/CMakeLists.txt`

**Interfaces:**
- Consumes: `http_dl_config_t`, `http_dl_data_cb_t`, `http_dl_status_cb_t` (from Task 1)
- Produces: `http_dl_perform()` implementation

- [ ] **Step 1: 创建 http_dl.c**

```c
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
```

- [ ] **Step 2: 更新 network/CMakeLists.txt**

```cmake
idf_component_register(SRCS "wifi.c" "mqtt.c" "sntp.c" "network.c" "http_dl.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES bsp nvs_flash esp_wifi esp_event esp_netif mqtt json esp_http_client)
```

改动说明：
- SRCS 增加 `"http_dl.c"`
- PRIV_REQUIRES 增加 `esp_http_client`（http_dl 依赖）
- PRIV_REQUIRES 保留 `ota`（`mqtt.c` 仍直接依赖 ota 模块）

- [ ] **Step 3: 编译验证**

```bash
cd "/mnt/d/CC_AI/esp32 dome/hello_world"
idf.py build 2>&1 | tail -30
```

预期结果：编译通过，末尾输出 `Project build successful.` 或类似信息。

- [ ] **Step 4: 记录改动**

新建了 `components/network/http_dl.c`，更新了 `components/network/CMakeLists.txt`。新增模块 `http_dl` 已通过编译验证。

---

### Task 3: 重构 `ota.c` — 使用 `http_dl_perform`

**Files:**
- Modify: `components/ota/ota.c`

**Interfaces:**
- Consumes: `http_dl_perform()`, `http_dl_config_t`, `http_dl_t` (from Task 1/2)

- [ ] **Step 1: 在 ota.c 中新增 OTA 下载上下文结构体和数据回调**

替换 `struct ota_dl_ctx` 和 `ota_dl_event_handler`。

将 `components/ota/ota.c` 中第 65-192 行（`struct ota_dl_ctx` 定义 + `ota_dl_event_handler` 函数）替换为以下代码：

```c
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
```

- [ ] **Step 2: 将 ota_start() 中的 HTTP 代码段替换为 http_dl_perform**

在 `ota_start()` 中，找到以下代码段（原第 304-382 行，从 `#define MAX_DL_RETRIES 3` 到 `} report_progress(95, "校验中...");`）：

替换为：

```c
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

    esp_ota_handle_t update_handle = ota_ctx.handle;
    report_progress(95, "校验中...");
```

注意：变量 `update_handle` 已在原代码第 258 行声明（`esp_ota_handle_t update_handle = 0;`），此处使用 `ota_ctx.handle` 为其赋值。删除原第 304 行的 `#define MAX_DL_RETRIES  3` 和 `bool dl_ok = false;`。

- [ ] **Step 3: 更新 ota.c 的 #include**

在 `components/ota/ota.c` 文件头部：

去除：
```c
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
```

增加：
```c
#include "http_dl.h"
```

说明：ESP-IDF 通过 PRIV_REQUIRES network 自动将 `components/network/` 加入 include 路径，因此直接用 `"http_dl.h"` 即可。

- [ ] **Step 4: 更新 ota/CMakeLists.txt**

```cmake
idf_component_register(SRCS "ota.c" "ota_gitee.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES esp_http_client app_update json mbedtls esp_wifi network)
```

改动说明：
- PRIV_REQUIRES 增加 `network`（ota.c 新增 `#include "network/http_dl.h"`）
- `esp_http_client` 保留不动，因为 `ota_gitee.c` 仍直接使用它

- [ ] **Step 5: 删除原 ota_dl_event_handler 的引用（确认已清理）**

确认以下内容已从 `ota.c` 中删除：
- `struct ota_dl_ctx`（原第 67-81 行）
- `ota_dl_event_handler` 函数（原第 89-192 行）
- `#define MAX_DL_RETRIES  3`（原第 304 行）
- `bool dl_ok = false;`（原第 305 行）
- 原 `ota_start()` 中的 HTTP 下载重试循环（原第 307-382 行）

- [ ] **Step 6: 编译验证**

```bash
cd "/mnt/d/CC_AI/esp32 dome/hello_world"
idf.py build 2>&1 | tail -30
```

预期结果：编译通过。

- [ ] **Step 7: 记录改动**

记录了 `components/ota/ota.c` 的改动：新增 `ota_dl_ctx_t` 和 `_on_data_cb`，替换 HTTP 代码段为 `http_dl_perform` 调用，精简约 100 行。

---

### Task 4: 最终编译验证 + 完整性检查

**Files:** 无改动

- [ ] **Step 1: 完整编译**

```bash
cd "/mnt/d/CC_AI/esp32 dome/hello_world"
rm -rf build
idf.py build 2>&1 | tail -50
```

预期结果：从零编译通过，无 warning。

- [ ] **Step 2: 代码对比验证**

检查以下要点确认重构未改变行为：
- `ota.h` 对外 API 无任何修改
- `ota_gitee.c/h` 无任何修改
- 重构后 `ota.c` 不再引用 `esp_http_client` API
- 重构后 `network/http_dl.c` 独立于 OTA 逻辑，不引用任何 `esp_ota_*` API

- [ ] **Step 3: 记录改动**

在 `docs/superpowers/plans/2026-06-24-http-downloader.md` 末尾附加编译验证结果。
