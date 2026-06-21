#ifndef __SYSTEM_H__
#define __SYSTEM_H__

#include "storage_init.h"
#include "sys_info.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t _reserved;      /* 壳子，后续扩展 */
} sys_config_t;

extern sys_config_t g_sys_config;

/**
 * @brief 系统层统一初始化
 *
 * 在 bsp_init() 之后、network_init() 之前调用。
 * 包含：NVS 初始化 + SPIFFS 挂载。
 */
void system_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __SYSTEM_H__ */
