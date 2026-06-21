#ifndef __APP_H__
#define __APP_H__

#include <stdint.h>
#include <stdbool.h>

#include "sys_ctrl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*app_lifecycle_cb)(void);
typedef bool (*app_key_cb)(uint8_t key_id, int event);

typedef struct {
    const char      *name;       /* 应用名（调试用） */
    app_lifecycle_cb on_enter;   /* 应用启动时调用 */
    app_lifecycle_cb on_exit;    /* 应用退出时调用 */
    app_lifecycle_cb on_render;  /* 每帧由 display_task 调用 */
    app_key_cb       on_key;     /* 按键路由至此 */
} app_t;

void app_launch(const app_t *app);      /* 启动 app（调 on_enter，标记活跃） */
void app_exit(void);                    /* 退出 app（调 on_exit，清活跃） */
bool app_is_active(void);               /* 判断是否有 app 运行 */
const app_t *app_get_current(void);     /* 获取当前 app 指针 */
void app_render(void);                  /* display_task 调用 */
bool app_handle_key(uint8_t key_id, int event);  /* sys_ctrl 调用 */

/**
 * @brief 集中创建所有 FreeRTOS 任务
 *
 * 在 WiFi 和 MQTT 启动之前调用。
 * 所有任务参数（栈/优先级/核绑定）在此一目了然。
 */
void app_start_tasks(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_H__ */
