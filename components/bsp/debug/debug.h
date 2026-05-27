#ifndef _DEBUG_H
#define _DEBUG_H

#include <stdio.h>

/*
 * 分级日志宏
 *
 * 日志级别（DBG_LEVEL 定义当前编译级别）：
 *   0 = NONE  — 全部关闭
 *   1 = ERR   — 仅错误
 *   2 = WARN  — 错误 + 警告
 *   3 = INFO  — 常规信息（推荐发布状态）
 *   4 = DEBUG — 全部调试输出
 *
 * 用法示例：
 *   DBG_ERR("DS18B20 初始化失败\n");
 *   DBG_INFO("温度: %d.%d°C\n", temp/10, temp%10);
 *   DBG_DEBUG("hue=%d\n", hue);
 *
 * 关闭时宏展开为 ((void)0)，编译器优化到零开销。
 */
#define DBG_LEVEL_NONE   0
#define DBG_LEVEL_ERR    1
#define DBG_LEVEL_WARN   2
#define DBG_LEVEL_INFO   3
#define DBG_LEVEL_DEBUG  4

/* ★ 主开关：修改此值控制所有打印的输出量 */
#define DBG_LEVEL  DBG_LEVEL_INFO

/* ─── 宏定义 ─── */

#if DBG_LEVEL >= DBG_LEVEL_ERR
    #define DBG_ERR(fmt, ...)   printf("[ERR] " fmt, ##__VA_ARGS__)
#else
    #define DBG_ERR(fmt, ...)   ((void)0)
#endif

#if DBG_LEVEL >= DBG_LEVEL_WARN
    #define DBG_WARN(fmt, ...)  printf("[WARN] " fmt, ##__VA_ARGS__)
#else
    #define DBG_WARN(fmt, ...)  ((void)0)
#endif

#if DBG_LEVEL >= DBG_LEVEL_INFO
    #define DBG_INFO(fmt, ...)  printf(fmt, ##__VA_ARGS__)
#else
    #define DBG_INFO(fmt, ...)  ((void)0)
#endif

#if DBG_LEVEL >= DBG_LEVEL_DEBUG
    #define DBG_DEBUG(fmt, ...) printf(fmt, ##__VA_ARGS__)
#else
    #define DBG_DEBUG(fmt, ...) ((void)0)
#endif

#endif
