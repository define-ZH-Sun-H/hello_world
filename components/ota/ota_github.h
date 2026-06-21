// components/ota/ota_github.h
#ifndef OTA_GITHUB_H
#define OTA_GITHUB_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 查询 GitHub Releases 最新版本
 * @param[out] tag_name  传出最新版本 tag（调用者无需释放，指向内部 static 字符串）
 * @param[out] dl_url    传出固件下载 URL（调用者无需释放，指向内部 static 字符串）
 * @return true=查询成功，false=查询失败
 */
bool ota_github_check(const char **tag_name, const char **dl_url);

#ifdef __cplusplus
}
#endif

#endif /* OTA_GITHUB_H */
