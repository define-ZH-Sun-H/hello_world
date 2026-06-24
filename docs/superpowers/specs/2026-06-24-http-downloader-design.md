# HTTP 下载器模块化设计

## 概述

将 `components/ota/ota.c` 中与 HTTP 下载相关的代码抽离到 `components/network/` 下形成独立的 `http_dl` 模块，实现 OTA 业务逻辑与 HTTP 传输层的完全解耦。

## 动机

当前 `ota.c`（448 行）中约 200 行是 HTTP 下载代码（下载上下文、事件回调、HTTP 客户端配置/执行/重试），与 OTA 业务逻辑（版本比较、分区管理、固件写入）深度混合。这使得：

- 代码可读性差，两个关注点互相干扰
- 难以测试 HTTP 下载部分
- 未来添加其他 OTA 方式（本地文件、BLE 等）需要重复实现传输层

## 设计目标

1. **通用化** — HTTP 下载器不知道数据用途，只负责从 URL 下载内容并通过回调投递
2. **零拷贝** — 不缓存下载数据，边收边通过回调转交
3. **可重试** — 内置可配置的重试机制
4. **向后兼容** — `ota.h` 对外 API 不变

## 架构

```
┌─────────────────────────────────────┐
│  ota.c (重构后)                      │
│  _on_data_cb()                      │
│  ├── 缓冲头部 → 校验版本              │
│  ├── esp_ota_begin() + write()      │
│  └── 写回调中决定是否中止             │
├─────────────────────────────────────┤
│  http_dl_perform()  ← 调用           │
└──────────┬──────────────────────────┘
           │ on_data / on_status 回调
┌──────────▼──────────────────────────┐
│  components/network/http_dl.c       │
│  HTTP init → perform → cleanup     │
│  内置重试逻辑                       │
│  不关心数据含义                     │
└─────────────────────────────────────┘
```

## 接口设计

### http_dl.h — 公开 API

```c
// 数据回调 — 每收到一块数据调用一次
// 返回 ESP_OK 继续，其他值中止下载（不重试）
typedef esp_err_t (*http_dl_data_cb_t)(http_dl_t *dl,
    const char *data, size_t len, void *user_ctx);

// 进度回调 — 可选，由 status_cb_enable 控制
typedef void (*http_dl_status_cb_t)(http_dl_t *dl,
    int percent, const char *status, void *user_ctx);

typedef struct {
    const char           *url;                  // 下载 URL
    int                   timeout_ms;           // 超时，默认 60000
    int                   max_retries;          // 最大重试次数，默认 3
    int                   retry_interval_ms;    // 重试间隔，默认 5000
    bool                  crt_bundle_attach;    // 加载 CA 证书包，默认 true
    int                   buffer_size;          // 接收缓冲区，默认 16384
    int                   max_redirect;         // 最大重定向次数，默认 5
    const char           *user_agent;           // UA，默认 "ESP32S3-HttpDL/1.0"
    http_dl_data_cb_t     on_data;              // 必填
    http_dl_status_cb_t   on_status;            // 可选，提供即启用
    void                 *user_ctx;
} http_dl_config_t;

// 一键下载：init → perform → cleanup，内置重试
esp_err_t http_dl_perform(const http_dl_config_t *cfg);
```

### http_dl_t — 透明句柄（struct 定义隐藏在 .c）

```c
typedef struct {
    http_dl_data_cb_t     on_data;
    http_dl_status_cb_t   on_status;
    void                 *user_ctx;
    int64_t               total_read;
    int                   last_percent;
    bool                  failed;     // on_data 返回非 ESP_OK 时置 true
} http_dl_t;
```

## 下载器内部流程

```
http_dl_perform(cfg)
  ├── 校验参数 (url && on_data)
  ├── 初始化 http_dl_t 上下文 (total_read=0, last_percent=-1, failed=false)
  ├── 获取 Content-Length (有则精确算%，无则粗估)
  │
  ├── 重试循环 (attempt=0..max_retries-1)
  │   ├── attempt>0 → vTaskDelay(retry_interval_ms)
  │   │
  │   ├── esp_http_client_init(cfg)
  │   ├── esp_http_client_set_* (method=GET, timeout, UA, buffer, redirect)
  │   ├── esp_crt_bundle_attach() (if crt_bundle_attach)
  │   ├── _event_handler → 代理给 on_data
  │   │
  │   ├── err = esp_http_client_perform(client)
  │   ├── status = esp_http_client_get_status_code(client)
  │   │
  │   ├── 成功判断: err==ESP_OK && status==200 && !dl_ctx.failed
  │   │   └── esp_http_client_is_complete_data_received() 校验完整性
  │   │       └── 通过 → break 退出重试循环
  │   │
  │   ├── 失败处理:
  │   │   ├── dl_ctx.failed → break (on_data 要求中止，不重试)
  │   │   └── else → 继续下一轮重试
  │   │
  │   └── esp_http_client_cleanup(client)
  │
  └── return ESP_OK / ESP_FAIL
```

### 进度百分比算法

```c
if (content_length > 0) {
    percent = (int)(total_read * 100 / content_length);
} else {
    // 无 Content-Length，粗估（假定 ~1.28MB）
    percent = (int)(total_read / 12800);
    if (percent < 10) percent = 10;
    if (percent > 90) percent = 90;
}
```

## OTA 侧重构

### 新增 _on_data_cb

将原有的 `ota_dl_event_handler` 精简为纯 OTA 语义的数据回调：

```c
static esp_err_t _on_data_cb(http_dl_t *dl, const char *data, size_t len, void *ctx) {
    ota_ctx_t *ota_ctx = (ota_ctx_t *)ctx;

    // Phase 1: 缓冲固件头 HEADER_SIZE 字节 → 校验
    if (!ota_ctx->header_checked) {
        // 同原逻辑：缓冲 → 版本比对 → 已回滚检查
        // 通过: esp_ota_begin → 写入已缓冲头
        // 不通过: return ESP_FAIL → dl.failed=true，不重试
    }

    // Phase 2: 写入剩余数据
    // return ESP_OK 继续 / ESP_FAIL 中止
}
```

### ota_start 简化

`ota_start()` 中原本 ~70 行的 HTTP 部分替换为：

```c
http_dl_config_t cfg = {
    .url = dl_url,
    .timeout_ms = 60000,
    .max_retries = 3,
    .on_data = _on_data_cb,
    .user_ctx = &ota_ctx,
};
http_dl_perform(&cfg);
```

## 文件变更清单

| 操作 | 文件 | 说明 |
|------|------|------|
| 新建 | `components/network/http_dl.h` | 公开 API 声明 |
| 新建 | `components/network/http_dl.c` | 下载器实现 |
| 修改 | `components/ota/ota.c` | 删除 HTTP 代码，改用 http_dl_perform |
| 修改 | `components/network/CMakeLists.txt` | 加 http_dl.c，加 PRIV_REQUIRES esp_http_client，去 ota |
| 修改 | `components/ota/CMakeLists.txt` | 去 PRIV_REQUIRES esp_http_client |
| 不变 | `ota.h` | 对外 API 不改变 |
| 不变 | `ota_gitee.c/h` | 不涉及 |

## 不做的事

- 不修改 `ota_gitee.c` — REST API 调用是请求-响应模式，与流式下载不同，无共用价值
- 不引入动态内存分配策略改变 — 沿用原有 realloc/free 模式
- 不增加单元测试框架 — ESP-IDF 原生测试基础设施与此设计无关
