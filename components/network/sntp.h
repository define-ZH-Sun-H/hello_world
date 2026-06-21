/**
 * @file sntp.h
 * @brief SNTP 校时模块
 *
 * 在 WiFi 获取到 IP 后调用 sntp_start() 启动 NTP 时间同步。
 * 通过 sntp_is_synced() 查询同步状态，无需阻塞等待。
 */

#ifndef SNTP_H
#define SNTP_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 启动 SNTP 校时
 *
 * 设置 NTP 服务器（pool.ntp.org）和时区（UTC+8 中国标准时间）。
 * 注册内部回调，同步完成后 sntp_is_synced() 返回 true。
 * 必须在 WiFi 连接成功后调用。
 */
void sntp_start(void);

/**
 * @brief 查询 NTP 时间是否已同步完成
 *
 * @return true  时间已同步
 * @return false 尚未同步
 */
bool sntp_is_synced(void);

#ifdef __cplusplus
}
#endif

#endif /* SNTP_H */
