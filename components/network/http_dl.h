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
