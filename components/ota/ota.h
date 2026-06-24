// components/ota/ota.h
#ifndef OTA_H
#define OTA_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 当前固件版本号，发版时手动更新 */
#define FIRMWARE_VERSION   "v0.1.2"

/**
 * 进度回调类型
 * @param percent  0-100
 * @param status   状态描述字符串（如 "下载中...", "校验中..."）
 */
typedef void (*ota_progress_cb_t)(int percent, const char *status);

/**
 * 检查 Gitee 最新 Release，比对版本号
 * @return true=有新版本，false=已是最新或检查失败
 */
bool ota_check_new_version(void);

/**
 * 执行 OTA 升级
 * 流程：检查版本 → 下载固件 → 写入 ota_1 → 设置启动分区 → 重启
 * 调用前需确保 WiFi 已连接
 */
esp_err_t ota_start(void);

/**
 * 启动定时检查任务（默认每周一次）
 * 自动检查 → 有新版本自动开始升级
 */
void ota_periodic_check_start(void);

/**
 * 注册进度回调（供 GUI 使用）
 */
void ota_register_progress_callback(ota_progress_cb_t cb);

#ifdef __cplusplus
}
#endif

#endif /* OTA_H */
