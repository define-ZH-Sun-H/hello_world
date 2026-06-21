/**
 * @file        menu.c
 * @brief       OLED 菜单核心实现 — 数据表 + 状态机 + NVS + 渲染
 *
 * ===== 整体架构说明 =====
 *
 * 菜单系统分三层：
 *
 *   ① 数据层（静态页表） ——————————————
 *      s_menu_pages[] 是一个常驻 RAM 的数组，按 menu_page_id_t 索引。
 *      每个 page 包含标题 + 菜单项列表。菜单项有三种类型：
 *        TOGGLE  — 开关项，值持久化到 NVS
 *        ACTION  — 动作项，进入子页面或启动 app
 *        INFO    — 只读信息，不交互
 *      暂未接入 app 框架的 ACTION 项目前通过 nav_push 进入无内容的占位页
 *      （BRIGHTNESS / FILE_BROWSER / RGB_EFFECTS）。
 *
 *   ② 状态层（导航栈 + 选中索引） —————
 *      s_menu_state 保存当前页、选中行、导航栈。
 *      导航栈支持分层进入（设置→亮度）和逐层返回。
 *      按 K4 返回上一级，K4 长按直接退出菜单回到主页。
 *
 *   ③ 交互层（按键处理 + 渲染） ————————
 *      menu_handle_key() 由外部调用（目前是 key.c 的 KeyInd_HandleEvents）。
 *      非菜单模式：K1-K4 短按 → 进入对应菜单页
 *      菜单模式：K1 上移 / K2 下移 / K3 确认 / K4 返回或退出
 *      menu_render() 直接操作 OLED GRAM，由 display_task 统一调度帧刷新。
 *
 * ==== 与显示系统的配合 ====
 *
 *   - 所有状态变更都设置 g_disp.dirty = true，由 display_task 的
 *     render_frame() 统一刷新屏幕。
 *   - 菜单激活时，menu_render()自行调 oled_clear_gram / oled_refresh_gram
 *     完成全帧渲染。注意：这绕过了 render_frame() 的 dirty 判断。
 *
 * ==== NVS 持久化 ====
 *
 *   TOGGLE 项的值保存在 NVS 的 "menu" 命名空间中，key 为 menu_item.nvs_key。
 *   每次切换时读→取反→写回，并同步到对应全局变量。
 *
 * @author      ZH-Sun
 * @version     v0.0.6
 */

#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "oled.h"
#include "oled_display.h"
#include "menu.h"
#include "led.h"
#include "sd.h"
#include "key.h"
#include "debug.h"

static const char *TAG = "menu";

/* ============================================================
 * 菜单状态（文件级静态变量）
 *
 * 整个菜单系统只有一个 s_menu_state 实例，
 * 外部通过 menu_is_active() / menu_handle_key() 等接口访问。
 * 页面数据表 s_menu_pages[] 也是静态的，均在链接时确定。
 * ============================================================ */
static menu_state_t s_menu_state;

/* ============================================================
 * 菜单项数据表（全部静态，编译期确定）
 *
 * === 这里的结构解释 ===
 *
 * 每页是一个 menu_item_t[] 数组，每个元素描述一行菜单。
 * 字段含义见 menu.h 中 menu_item_t 定义：
 *   label       — 显示文字
 *   type        — TOGGLE / ACTION / INFO
 *   target_page — ACTION 时导航到的页 ID（0 表示无效）
 *   nvs_key     — TOGGLE 对应的 NVS key（NULL 表示不持久化）
 *   default_val — TOGGLE 初次上电的默认值
 *   info_text   — INFO 时显示的文本（当前未使用，label 直接作显示）
 *
 * === 哪些页有子页占位 ===
 *
 * SETTINGS→Brightness，FILES→Browse，APPS→RGB 都是 MENU_ITEM_ACTION，
 * 但对应的子页（BRIGHTNESS / FILE_BROWSER / RGB_EFFECTS）目前是
 * 空页（count=0, items=NULL），进去只显示标题，没有可操作的行。
 * 未来接入 app 框架后，这些将改为启动 app。
 * ============================================================ */

/* Settings — 4 items */
static const menu_item_t items_settings[] = {
    { "WiFi",           MENU_ITEM_TOGGLE, 0,                     "menu_wifi_on",  true,  NULL },
    { "BT",             MENU_ITEM_TOGGLE, 0,                     "menu_bt_on",    false, NULL },
    { "Sleep",          MENU_ITEM_TOGGLE, 0,                     "menu_sleep",    false, NULL },
    { "Brightness",     MENU_ITEM_ACTION, MENU_PAGE_BRIGHTNESS,  NULL,           false, NULL },
};

/* Files — 4 items */
static const menu_item_t items_files[] = {
    { "SD Card",        MENU_ITEM_INFO,   0,                     NULL,           false, NULL },
    { "Free Space",     MENU_ITEM_INFO,   0,                     NULL,           false, NULL },
    { "SD",             MENU_ITEM_TOGGLE, 0,                     "menu_sd_on",   true,  NULL },
    { "Browse",         MENU_ITEM_ACTION, MENU_PAGE_FILE_BROWSER, NULL,          false, NULL },
};

/* Apps — 3 items */
static const menu_item_t items_apps[] = {
    { "LED",            MENU_ITEM_TOGGLE, 0,                     "menu_led_on",  false, NULL },
    { "RGB",            MENU_ITEM_ACTION, MENU_PAGE_RGB_EFFECTS, NULL,           false, NULL },
    { "Sensor",         MENU_ITEM_TOGGLE, 0,                     "menu_sensor",  true,  NULL },
};

/* About — 4 items */
static const menu_item_t items_about[] = {
    { "Device: ESP32-S3",   MENU_ITEM_INFO, 0,                  NULL,           false, NULL },
    { "Version: v0.0.6",    MENU_ITEM_INFO, 0,                  NULL,           false, NULL },
    { "SDK: ESP-IDF v5.5",  MENU_ITEM_INFO, 0,                  NULL,           false, NULL },
    { "By: ZH-Sun",         MENU_ITEM_INFO, 0,                  NULL,           false, NULL },
};

/* 页面总表 — 用 designated initializer 按枚举值索引，
 * 确保每个 menu_page_id_t 值都有对应项。
 * count=0 且 items=NULL 的页是空页占位（子页/app 预留）。 */
static const menu_page_t s_menu_pages[MENU_PAGE_COUNT] = {
    [MENU_PAGE_HOME]         = { "Home",         NULL,             0 },
    [MENU_PAGE_SETTINGS]     = { "Settings",     items_settings,   sizeof(items_settings) / sizeof(items_settings[0]) },
    [MENU_PAGE_FILES]        = { "Files",        items_files,      sizeof(items_files)   / sizeof(items_files[0]) },
    [MENU_PAGE_APPS]         = { "Apps",         items_apps,       sizeof(items_apps)    / sizeof(items_apps[0]) },
    [MENU_PAGE_ABOUT]        = { "About",        items_about,      sizeof(items_about)   / sizeof(items_about[0]) },
    [MENU_PAGE_BRIGHTNESS]   = { "Brightness",   NULL,             0 },
    [MENU_PAGE_FILE_BROWSER] = { "File Browser", NULL,             0 },
    [MENU_PAGE_RGB_EFFECTS]  = { "RGB Effects",  NULL,             0 },
};

/* ============================================================
 * 页表查找（按页 ID 获取页描述符）
 *
 * menu_page_id_t 枚举值作为数组索引，越界返回 NULL。
 * 调用处都需要检查返回值。
 * ============================================================ */

/**
 * @brief 按页 ID 获取页描述符
 *
 * menu_page_id_t 枚举值直接作为数组索引查表。
 *
 * @param page 页 ID
 *
 * @return 页描述符指针，越界返回 NULL
 */
static const menu_page_t *get_page(menu_page_id_t page)
{
    if ((uint8_t)page < MENU_PAGE_COUNT)
        return &s_menu_pages[page];
    return NULL;
}

/* ============================================================
 * NVS 接口
 *
 * NVS（Non-Volatile Storage）是 ESP-IDF 提供的 Flash 存储抽象，
 * 适合存放少量键值对（几百字节级）。这里用于保存 TOGGLE 项的开关状态。
 *
 * 写入流程：nvs_open("menu", WRITE) → nvs_set_u8 → nvs_commit → nvs_close
 * 读取流程：nvs_open("menu", READONLY) → nvs_get_u8 → nvs_close
 *   读取失败时返回 default_val（首次启动或 NVS 被擦除时起到初始值作用）
 *
 * 注意：每次读/写都做完整的 open/close 操作，不缓存读取结果。
 * 这意味着菜单渲染时每帧都会调用 nvs_open 读取 TOGGLE 状态，
 * 但 NVS 操作走到 Flash cache 层，对 50Hz 帧率来说可以接受。
 * ============================================================ */

/**
 * @brief 持久化 TOGGLE 菜单项的值到 NVS
 *
 * 将开关值写入 "menu" 命名空间。
 * 每次写操作都完整 open → set → commit → close。
 *
 * @param nvs_key NVS key 名称
 * @param value   true=开，false=关
 *
 * @return void
 */
void menu_save_toggle(const char *nvs_key, bool value)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("menu", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open (WRITE) failed: %s", esp_err_to_name(err));
        return;
    }
    nvs_set_u8(handle, nvs_key, value ? 1 : 0);
    nvs_commit(handle);
    nvs_close(handle);
}

/**
 * @brief 从 NVS 读取 TOGGLE 菜单项的值
 *
 * 从 "menu" 命名空间读取开关值。
 * 读取失败（首次启动/NVS 被擦除）时返回 default_val。
 *
 * @param nvs_key     NVS key 名称
 * @param default_val 默认值（首次启动或读取失败时返回此值）
 *
 * @return true=开，false=关
 */
bool menu_load_toggle(const char *nvs_key, bool default_val)
{
    nvs_handle_t handle;
    uint8_t val = default_val ? 1 : 0;
    esp_err_t err = nvs_open("menu", NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return default_val;
    }
    nvs_get_u8(handle, nvs_key, &val);
    nvs_close(handle);
    return (val != 0);
}

/* ============================================================
 * 导航栈
 *
 * 导航栈是菜单系统实现"分层进入"和"逐层返回"的核心机制。
 *
 * 工作原理（类似浏览器的历史记录）：
 *
 *   nav_push(page)
 *     将当前页压入 history 数组，然后跳转到新页。
 *     例如：Settings 页（当前）→ 选中 Brightness → nav_push(BRIGHTNESS)
 *           ↓ history 中存了 Settings 页的 ID
 *
 *   nav_pop()
 *     从 history 数组弹出最近一次进入的页，回到那里。
 *     如果 history 已空（已经在首页），则 menu_active = false 退出菜单。
 *
 * MENU_HISTORY_DEPTH = 8，支持最多 8 层嵌套。
 * 目前菜单只有两层（主菜单→子页），远未达到上限。
 * ============================================================ */

/**
 * @brief 压栈导航：进入新页面
 *
 * 将当前页面 ID 压入历史栈，然后跳转到目标页并选中第一行。
 * 栈满时静默丢弃最旧记录。
 *
 * @param page 目标页 ID
 */
static void nav_push(menu_page_id_t page)
{
    /* 将当前页压栈保存 */
    if (s_menu_state.history_depth < MENU_HISTORY_DEPTH) {
        s_menu_state.history[s_menu_state.history_depth++] =
            (uint8_t)s_menu_state.current_page;
    }
    /* 跳转到目标页，选中第一行 */
    s_menu_state.current_page   = page;
    s_menu_state.selected_index = 0;
}

/**
 * @brief 弹栈导航：返回上一页
 *
 * 从历史栈弹出最近一次进入的页并跳转。
 * 如果栈已空（已经在首页），则退出菜单模式。
 */
static void nav_pop(void)
{
    if (s_menu_state.history_depth > 0) {
        /* 有历史记录 → 回到上一页 */
        s_menu_state.current_page =
            (menu_page_id_t)s_menu_state.history[--s_menu_state.history_depth];
        s_menu_state.selected_index = 0;
    } else {
        /* 没有历史记录 → 退出菜单模式 */
        s_menu_state.menu_active = false;
    }
}

/* ============================================================
 * 菜单项激活 — K3（确认键）触发
 *
 * 根据当前选中菜单项的类型执行不同操作：
 *
 *   TOGGLE
 *     读 NVS → 取反 → 写回 NVS → 同步到系统全局变量
 *     同步方式取决于 nvs_key 名称：
 *       "menu_wifi_on" → g_disp.wifi_on（状态栏显示）
 *       "menu_bt_on"   → g_disp.bt_on
 *       "menu_sleep"   → g_disp.sleep
 *       "menu_led_on"  → LED_Flag（硬件 LED）
 *       "menu_sd_on"   / "menu_sensor" → 不在这里同步，
 *        各自所属任务（SD、sensor）在初始化时自行读取 NVS
 *
 *   ACTION
 *     导航到子页面（nav_push）。这些子页目前是空占位页，
 *     未来会改为启动对应的 app。
 *
 *   INFO
 *     只读显示，无操作。
 *
 * @note 调用了 NVS 读写，不要在 ISR 中调用。
 * ============================================================ */

/**
 * @brief 激活当前选中的菜单项（K3 确认键触发）
 *
 * TOGGLE 项：读 NVS → 取反 → 写回 NVS → 同步到系统全局变量
 * ACTION 项：导航到子页面
 * INFO 项：无操作
 */
static void menu_activate_current_item(void)
{
    const menu_page_t *page = get_page(s_menu_state.current_page);
    if (!page || page->count == 0) return;
    if (s_menu_state.selected_index >= page->count) return;

    const menu_item_t *item = &page->items[s_menu_state.selected_index];

    switch (item->type) {

    case MENU_ITEM_TOGGLE: {
        /* 读当前值 → 取反 → 写回 */
        bool val = menu_load_toggle(item->nvs_key, item->default_val);
        val = !val;
        menu_save_toggle(item->nvs_key, val);

        /* 同步到系统全局状态 */
        if      (strcmp(item->nvs_key, "menu_wifi_on") == 0) oled_display_set_wifi(val);
        else if (strcmp(item->nvs_key, "menu_bt_on")   == 0) oled_display_set_bt(val);
        else if (strcmp(item->nvs_key, "menu_sleep")   == 0) oled_display_set_sleep(val);
        else if (strcmp(item->nvs_key, "menu_led_on")  == 0) LED_Flag       = val;
        /* menu_sd_on / menu_sensor 由各自任务读取 NVS，无需同步 */
        ESP_LOGI(TAG, "%s -> %s", item->label, val ? "ON" : "OFF");
        break;
    }

    case MENU_ITEM_ACTION:
        nav_push(item->target_page);
        break;

    case MENU_ITEM_INFO:
    default:
        break;
    }
}

/* ============================================================
 * 按键事件处理（核心分发入口）
 *
 * 这个函数是整个菜单系统的"交互引擎"。目前被 key.c 的
 * KeyInd_HandleEvents() 调用，未来将改为由 key_task 扫描后
 * 统一通过事件组查询 + menu.c 集中分发。
 *
 * ==== 调用时机 ====
 *
 * key_task 每 10ms 调用 KeyInd_HandleEvents() → 对有事件的按键
 * 逐一调用 menu_handle_key(key_id, event)。
 *
 * ==== 两种模式的差异 ====
 *
 * ① 非菜单模式（menu_active == false）
 *    此时的角色是"菜单启动器"：
 *      - 只有 KEY_IND_EVENT_PRESS（短按）有意义
 *      - key_id 0/1/2/3 分别映射到 SETTINGS/FILES/APPS/ABOUT 页
 *      - 清空导航栈，进入菜单模式
 *      - return false 表示"这个事件外部请也处理"（LED 演示用）
 *    长按和释放事件都被忽略（return false）。
 *
 * ② 菜单模式（menu_active == true）
 *    此时的角色是"导航控制器"：
 *      - 消费所有按键事件（return true），防止事件泄漏给系统
 *      - K1 上移选中行（不能超出第一行）
 *      - K2 下移选中行（不能超出最后一行）
 *      - K3 激活当前项（开关切换 / 进入子页 / 启动 app）
 *      - K4 短按返回上一页，长按直接退出菜单
 *      - 只有 PRESS 和 LONG_PRESS 事件有意义，
 *        RELEASE / CLICK_MULTI 在菜单模式下也被消费（return true）
 *
 * ==== 状态同步约定 ====
 *
 * 所有状态变更后都设 g_disp.dirty = true，确保显示任务在
 * 下一帧刷新屏幕。menu.c 不直接调 oled_refresh_gram ——
 * 唯一例外是 menu_render()，它在帧末尾自行调用。
 *
 * @param key_id 按键编号 0-3
 * @param event  按键事件类型（PRESS / LONG_PRESS / RELEASE / CLICK_MULTI）
 * @return true=事件已被消费，false=未被消费（外部仍可处理）
 * ============================================================ */

/**
 * @brief 按键事件分发处理（菜单核心交互引擎）
 *
 * 非菜单模式：K1-K4 短按进入对应菜单页，返回 false 让外部继续处理
 * 菜单模式：消费所有按键事件，执行导航/开关/返回
 *
 * @param key_id 按键编号 0-3
 * @param event  按键事件类型
 *
 * @return true 事件已消费，false 未被消费（外部仍可处理）
 */
bool menu_handle_key(uint8_t key_id, int event)
{
    /* ---- 非菜单模式：K1-K4 短按进入对应页面 ---- */
    if (!s_menu_state.menu_active) {
        /* 只有 PRESS 事件会触发进入菜单，长按/释放/连击忽略 */
        if (event == KEY_IND_EVENT_PRESS) {
            s_menu_state.history_depth = 0;      /* 清空导航栈，确保进入菜单时是"根层" */
            s_menu_state.selected_index = 0;     /* 默认选中第一行 */
            s_menu_state.menu_active = true;     /* 进入菜单模式 */
            /* K1-K4 分别打开一个菜单页，每个按键固定映射 */
            switch (key_id) {
            case 0: s_menu_state.current_page = MENU_PAGE_SETTINGS; break;
            case 1: s_menu_state.current_page = MENU_PAGE_FILES;    break;
            case 2: s_menu_state.current_page = MENU_PAGE_APPS;     break;
            case 3: s_menu_state.current_page = MENU_PAGE_ABOUT;    break;
            default: s_menu_state.menu_active = false; break;
            }
            return false;   /* 返回 false，让外部继续处理（例如 LED 翻转演示） */
        }
        return false;       /* 非 PRESS 事件，全部不消费 */
    }

    /* ---- 菜单激活模式：消费所有按键，阻止事件泄漏 ---- */
    switch (key_id) {

    case 0:     /* K1 — 上移 */
        if (event == KEY_IND_EVENT_PRESS) {
            const menu_page_t *page = get_page(s_menu_state.current_page);
            if (page && page->count > 0 && s_menu_state.selected_index > 0) {
                s_menu_state.selected_index--;
            }
        }
        break;

    case 1:     /* K2 — 下移 */
        if (event == KEY_IND_EVENT_PRESS) {
            const menu_page_t *page = get_page(s_menu_state.current_page);
            if (page && page->count > 0 &&
                s_menu_state.selected_index < page->count - 1) {
                s_menu_state.selected_index++;
            }
        }
        break;

    case 2:     /* K3 — 确认 / 激活 */
        if (event == KEY_IND_EVENT_PRESS) {
            menu_activate_current_item();
        }
        break;

    case 3:     /* K4 — 返回 / 退出 */
        if (event == KEY_IND_EVENT_PRESS) {
            nav_pop();                     /* 逐层返回 */
        } else if (event == KEY_IND_EVENT_LONG_PRESS) {
            s_menu_state.menu_active = false;  /* 长按直接退出 */
        }
        break;

    default:
        break;
    }

    return true;    /* 菜单模式下，所有按键都被"吞掉"，不落到后面的系统键处理 */
}

/* ============================================================
 * 菜单渲染（直接操作 OLED GRAM 缓冲区）
 *
 * 每次调用都会：
 *   1. oled_clear_gram() — 清空 GRAM 缓冲区（内存操作，不发 I2C）
 *   2. 绘制标题栏 + 分隔线 + 列表项
 *   3. oled_refresh_gram() — 将整个 GRAM 刷到屏幕（触发 I2C 传输 ~4KB）
 *
 * ==== 渲染布局 ====
 *
 *   128 × 64 OLED 的布局：
 *   ┌──────────────────────┐  y=0
 *   │   标题（16px 字体）   │
 *   ├──────────────────────┤  y=16
 *   │ > 选项3（高亮反白）   │  y=18
 *   │   选项2              │  y=32
 *   │   选项3              │  y=46
 *   │   选项4（勉强可见）   │  y=60
 *   └──────────────────────┘  y=63
 *
 * 每行：12px 字体 + 2px 间距 = 14px，最多显示 3 行半。
 *
 * ==== 选中行高亮 ====
 *
 * 用 fill_gram 将整行填充为白色（全白背景），再用黑字显示标签。
 * 高亮行的"选中标志"是 '>' 字符在行首。
 *
 * ==== TOGGLE 项显示 ====
 *
 * 在行右侧显示 ON / OFF 标签（x=110 处右对齐）。
 * 注意：每次渲染都调 menu_load_toggle() 读 NVS，确保显示与实际存储一致。
 *
 * @note 本函数直接调用了 oled_clear_gram + oled_refresh_gram，
 *       绕过了 display_task 的 dirty 标志判断。这意味着即使
 *       render_frame() 中不设 dirty 条件，menu_render() 也总是
 *       执行完整的清屏→绘制→刷新流程。
 * ============================================================ */

/**
 * @brief 在 GRAM 中填充矩形区域（不触发 I2C refresh）
 *
 * 直接操作 OLED GRAM 缓冲区，将指定矩形内所有像素设为 dot 值。
 * 与 oled_fill() 不同，此函数只写缓冲区，配合全局 refresh。
 *
 * @param x1  左上角 X 坐标（0-127）
 * @param y1  左上角 Y 坐标（0-63）
 * @param x2  右下角 X 坐标（0-127）
 * @param y2  右下角 Y 坐标（0-63）
 * @param dot 像素值（0=灭，1=亮）
 */
static void fill_gram(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, uint8_t dot)
{
    for (uint8_t x = x1; x <= x2; x++)
        for (uint8_t y = y1; y <= y2; y++)
            oled_draw_point(x, y, dot);
}

/**
 * @brief 渲染当前菜单页到 OLED 屏幕
 *
 * 每次调用执行完整流程：清 GRAM → 绘制标题栏/分隔线/列表项 → 刷新屏幕。
 * 支持滚动显示（选中项进入第 3 行后窗口跟随下移）和 TOGGLE 项的 ON/OFF 标签。
 *
 * @note 本函数直接调 oled_clear_gram + oled_refresh_gram，
 *       绕过了 display_task 的 dirty 标志判断。
 *
 * @return void
 */
void menu_render(void)
{
    const menu_page_t *page = get_page(s_menu_state.current_page);
    if (!page) return;

    oled_clear_gram();

    /* ---- 标题栏（y=0, 16px 字体） ---- */
    oled_show_string(0, 0, page->title, 16);

    /* ---- 分隔线（y=16） ---- */
    oled_draw_point(0, 16, 1);
    for (uint8_t x = 1; x < 128; x++)
        oled_draw_point(x, 16, 1);

    /* ---- 列表项（带滚动） ---- */
    const uint8_t item_height = 14;              /* 12px 字体 + 2px 间距 */
    const uint8_t max_visible = 3;               /* (64 - 18) / 14 = 3 行 */

    /* 计算滚动偏移：选中项进入第 3 行后，窗口跟随下移 */
    uint8_t start = 0;
    if (page->count > max_visible && s_menu_state.selected_index >= max_visible) {
        start = s_menu_state.selected_index - (max_visible - 1);
    }

    uint8_t y = 18;
    for (uint8_t i = start; i < page->count; i++) {
        if (y + 12 > 64) break;

        const menu_item_t *item = &page->items[i];

        if (i == s_menu_state.selected_index) {
            /* 选中行：全宽高亮 */
            fill_gram(0, y, 127, y + 11, 1);

            /* ">" 指示器 */
            oled_show_char(2, y, '>', 12, 0);

            /* 标签（白色字，画在黑色底色上） */
            oled_show_string(14, y, item->label, 12);
        } else {
            /* 未选中行：正常显示 */
            oled_show_string(4, y, item->label, 12);
        }

        /* TOGGLE 项：右侧显示 ON/OFF */
        if (item->type == MENU_ITEM_TOGGLE) {
            bool val = menu_load_toggle(item->nvs_key, item->default_val);
            const char *tag = val ? "ON" : "OFF";
            oled_show_string(110, y, tag, 12);
        }

        y += item_height;
    }

    /* 还有更多项时，右下角画小三角指示器 */
    if (start + max_visible < page->count) {
        oled_draw_point(124, 55, 1);
        oled_draw_point(123, 56, 1);
        oled_draw_point(124, 56, 1);
        oled_draw_point(122, 57, 1);
        oled_draw_point(123, 57, 1);
        oled_draw_point(124, 57, 1);
    }

    oled_refresh_gram();
}

/* ============================================================
 * 初始化
 *
 * 由 app_main 在 Phase 2 中调用（硬件初始化之后、任务创建之前）。
 *
 * 执行顺序：
 *   1. 从 NVS 加载 TOGGLE 项的断电恢复值到 g_disp（状态栏显示用）
 *   2. s_menu_state 全清零
 *   3. 设置默认值（menu_active = false, current_page = HOME, ...）
 *   4. g_disp.dirty = true 让首帧显示
 *
 * @note menu_init() 不创建任何队列/任务/信号量，纯数据初始化。
 *       显示任务（display_task）在 app_main Phase 3 中创建。
 * ============================================================ */

/**
 * @brief 初始化 NVS 闪存存储
 *
 * 自动处理 NVS 分区损坏/版本变更（擦除后重试）。
 * 必须在 menu_init() 之前调用。
 *
 * @return void
 */
void nvs_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    DBG_INFO("NVS 初始化成功\n");
}

/**
 * @brief 初始化菜单系统
 *
 * 从 NVS 加载 TOGGLE 项的断电恢复值到 g_disp，清零菜单状态，
 * 设置默认值（menu_active = false, current_page = HOME），
 * 标记 dirty 触发首帧显示。
 *
 * @note 不创建任何队列/任务/信号量，纯数据初始化。
 *
 * @return void
 */
void menu_init(void)
{
    oled_display_set_wifi(menu_load_toggle("menu_wifi_on", true));
    oled_display_set_bt(menu_load_toggle("menu_bt_on",   false));
    oled_display_set_sleep(menu_load_toggle("menu_sleep",    false));

    s_menu_state.menu_active    = false;
    s_menu_state.current_page   = MENU_PAGE_HOME;
    s_menu_state.selected_index = 0;
    s_menu_state.history_depth  = 0;
    s_menu_state.brightness     = 100;

    oled_display_mark_dirty();

    ESP_LOGI(TAG, "menu system initialized");
}

/* ============================================================
 * 公共查询接口（供 oled_display.c 和未来的事件分发层调用）
 *
 * menu_is_active()
 *   被 oled_display.c 的 render_frame() 查询，决定渲染菜单还是主页。
 *   也被 key.c 的 KeyInd_HandleEvents() 查询，决定按键路由。
 *
 * menu_get_current_page_count()
 *   当前页面的菜单项数量，供外部计算显示布局用（目前未被调用）。
 * ============================================================ */

/**
 * @brief 获取当前菜单页的项数
 *
 * @return 菜单项数量，当前页无效时返回 0
 */
uint8_t menu_get_current_page_count(void)
{
    const menu_page_t *page = get_page(s_menu_state.current_page);
    return page ? page->count : 0;
}

/**
 * @brief 查询菜单当前是否激活
 *
 * @return true 菜单模式，false 正常显示
 */
bool menu_is_active(void)
{
    return s_menu_state.menu_active;
}
