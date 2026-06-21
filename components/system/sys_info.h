#ifndef __SYS_INFO_H
#define __SYS_INFO_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 打印芯片信息（启动时调用一次）
 *
 * 输出芯片型号、核心数、支持的功能（WiFi/BT/BLE/802.15.4）。
 *
 * @return void
 */
void print_chip_info(void);

#ifdef __cplusplus
}
#endif

#endif /* __SYS_INFO_H */
