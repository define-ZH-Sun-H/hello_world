#ifndef __MENU_H
#define __MENU_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ===== 菜单页 ID 枚举 ===== */
typedef enum {
    MENU_PAGE_HOME = 0,
    MENU_PAGE_SETTINGS,       /* K1 */
    MENU_PAGE_FILES,          /* K2 */
    MENU_PAGE_APPS,           /* K3 */
    MENU_PAGE_ABOUT,          /* K4 */
    /* 子页面（扩展预留） */
    MENU_PAGE_BRIGHTNESS,
    MENU_PAGE_FILE_BROWSER,
    MENU_PAGE_RGB_EFFECTS,
    MENU_PAGE_COUNT,
} menu_page_id_t;

/* ===== 菜单项类型 ===== */
typedef enum {
    MENU_ITEM_TOGGLE,         /* 开关项：●/○ */
    MENU_ITEM_ACTION,         /* 动作项：进入子页或执行操作 */
    MENU_ITEM_INFO,           /* 信息项：只读显示 */
} menu_item_type_t;

/* ===== 菜单项描述 ===== */
typedef struct {
    const char *label;              /* 显示文本 */
    menu_item_type_t type;          /* 项类型 */
    menu_page_id_t target_page;     /* ACTION 时跳转的目标页 */
    const char *nvs_key;            /* TOGGLE 对应的 NVS key，NULL 表示不持久化 */
    bool default_val;               /* TOGGLE 默认值 */
    const char *info_text;          /* INFO 类型显示的静态文本 */
} menu_item_t;

/* ===== 菜单页描述 ===== */
typedef struct {
    const char *title;              /* 标题栏文字 */
    const menu_item_t *items;       /* 菜单项数组 */
    uint8_t count;                  /* 菜单项数量 */
} menu_page_t;

/* ===== 菜单状态（嵌入 display_t） ===== */
#define MENU_HISTORY_DEPTH 8

typedef struct {
    menu_page_id_t current_page;    /* 当前页面 ID */
    uint8_t history[MENU_HISTORY_DEPTH]; /* 导航栈 */
    uint8_t history_depth;          /* 栈深度 */
    uint8_t selected_index;         /* 当前选中行 */
    bool menu_active;               /* true=菜单模式，false=正常显示 */
    uint8_t brightness;             /* 亮度值 0-100 */
} menu_state_t;

/* ===== API ===== */

/**
 * @brief 初始化 NVS 闪存存储（菜单持久化依赖）
 *
 * 必须在 menu_init() 之前调用。
 * 自动处理 NVS 分区损坏/版本变更（擦除后重试）。
 *
 * @return void
 */
void nvs_init(void);

/**
 * @brief 初始化菜单系统（注册菜单页表、加载 NVS 设置）
 *
 * @return void
 */
void menu_init(void);

/**
 * @brief 菜单按键处理
 * @param key_id 按键编号 0-3
 * @param event  按键事件类型
 * @return true=事件已由菜单消费，false=未被消费
 *
 * 在菜单非激活状态：K1-K4 的 PRESS 事件打开对应菜单页，返回 false 让现有 LED/RGB 逻辑继续
 * 在菜单激活状态：消费所有按键事件，执行导航/开关/返回
 */
bool menu_handle_key(uint8_t key_id, int event);

/**
 * @brief 获取当前菜单页面的渲染行数
 * @return 行数（用于 display_task 计算渲染位置）
 */
uint8_t menu_get_current_page_count(void);

/**
 * @brief 查询菜单当前是否激活
 * @return true=菜单模式，false=正常显示
 */
bool menu_is_active(void);

/**
 * @brief 渲染菜单到 OLED 缓冲区（由 display_task 调用）
 *
 * @return void
 */
void menu_render(void);

/**
 * @brief 持久化 TOGGLE 值到 NVS
 *
 * @param nvs_key NVS key 名称
 * @param value   true=开，false=关
 *
 * @return void
 */
void menu_save_toggle(const char *nvs_key, bool value);

/**
 * @brief 从 NVS 读取 TOGGLE 值
 *
 * @param nvs_key     NVS key 名称
 * @param default_val 默认值（首次启动或读取失败时返回此值）
 *
 * @return true=开，false=关
 */
bool menu_load_toggle(const char *nvs_key, bool default_val);

#ifdef __cplusplus
}
#endif

#endif /* __MENU_H */
