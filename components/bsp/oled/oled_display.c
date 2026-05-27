#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/queue.h"
#include "oled.h"
#include "oled_image.h"
#include "oled_display.h"

/**
 * @brief 全局显示数据缓冲区
 *
 * 各传感器/中断任务直接修改此结构体的字段（如 ds18b20_temp），
 * 并将 dirty 置 true，display_task 自动将其渲染到 OLED。
 * 任务间无需互斥锁 —— 生产者（多个写入者）和消费者（单一显示任务）
 * 通过 dirty 标志实现单次原子更新。
 */
display_t g_disp = { .dirty = false };

/**
 * @brief 显示消息队列句柄
 *
 * 供按键中断或其他事件源发送即时消息（DISP_MSG_xxx），
 * 驱动 display_task 在下一帧更新对应字段。
 * 队列长度 5，按需阻塞发送方。
 */
QueueHandle_t disp_queue = NULL;

/* 息屏定时器：5s 无按键操作自动息屏 */
static TimerHandle_t xSleepTimer = NULL;

static void sleep_timer_callback(TimerHandle_t xTimer)
{
    disp_msg_t msg = { .type = DISP_MSG_SCREEN_SLEEP };
    xQueueSend(disp_queue, &msg, 0);
}

/*
 * —————— 布局常量 ——————
 * OLED 分辨率 128×64，分为三个区域：
 *   状态栏（0-15px）：WiFi/蓝牙图标
 *   分隔线（16px）：   单像素水平线
 *   内容区（18-63px）：传感器数据文本，12 号字高 12px
 */
#define STATUS_BAR_H       16          /* 状态栏高度，与 icon_wifi_on 16x16 匹配 */
#define SEPARATOR_Y        16          /* 分隔线位于第 16 行 */
#define CONTENT_START_Y    18          /* 内容区首行起始 y 坐标 */
#define FONT_SIZE          12          /* 内容区字体大小（像素高） */
#define FONT_W             (FONT_SIZE / 2)   /* 12 号字宽 6px（等宽半角） */

/**
 * @brief 绘制水平分隔线
 *
 * 在 SEPARATOR_Y 行逐像素画点，将状态栏与内容区视觉分离。
 * 128 为 OLED 水平分辨率。
 */
static void draw_separator(void)
{
    for (uint8_t i = 0; i < 128; i++)
        oled_draw_point(i, SEPARATOR_Y, 1);
}

/**
 * @brief OLED 显示任务（50Hz 刷新）
 *
 * === 工作流程 ===
 *
 * 1. 轮询 disp_queue 接收消息，更新 g_disp 对应字段
 * 2. 检查 g_disp.dirty：
 *    - false → 跳过本帧（不操作底层 I2C）
 *    - true  → 执行完整重绘：
 *      a. oled_clear_gram() 清除显存
 *      b. 绘制状态栏图标（WiFi/BT）
 *      c. 绘制分隔线
 *      d. 绘制三行内容：DS18B20 / DHT11 / 按键计数
 *      e. oled_refresh_gram() 一次性推送到 OLED
 *      f. dirty = false
 *
 * === 设计要点 ===
 *
 * - 队列消息 + dirty 标志双重驱动：队列负责"事件驱动"更新，
 *   dirty 标志合并同一帧内的多次变更，避免重复 I2C 刷新。
 * - 刷新率 50Hz（20ms 周期），人眼感知无闪烁。
 * - 绑定到 core 1，与协议任务（core 0 的 key_task）分核运行。
 *
 * @param pv 任务参数（未使用）
 */
void display_task(void *pv)
{
    char buf[24];
    disp_msg_t msg;

    while (1) {
        /*
         * 阶段一：消费队列消息 —— 非阻塞接收所有积压消息，
         *         按类型更新 g_disp 字段并置 dirty 标志。
         */
        while (xQueueReceive(disp_queue, &msg, 0) == pdTRUE) {
            switch (msg.type) {
            case DISP_MSG_KEY_PRESS:
                g_disp.key_count++;
                g_disp.dirty = true;
                break;

            case DISP_MSG_DS18B20_TEMP:
                /* val1 为 DS18B20 温度值（单位 0.1°C，如 285 = 28.5°C） */
                g_disp.ds18b20_temp = msg.val1;
                g_disp.dirty = true;
                break;

            case DISP_MSG_DHT11_DATA:
                /* val1 = 温度整数值，val2 = 湿度整数值 */
                g_disp.dht11_temp = (int8_t)msg.val1;
                g_disp.dht11_humi = (uint8_t)msg.val2;
                g_disp.dirty = true;
                break;

            case DISP_MSG_SCREEN_SLEEP:
                g_disp.sleep = true;
                oled_off();                /* 发 3 字节命令关显示，硬件自动熄屏 */
                g_disp.dirty = false;      /* 已物理熄屏，不需要刷新 */
                break;

            case DISP_MSG_SCREEN_WAKE:
                g_disp.sleep = false;
                oled_on();                 /* 开显示，内容从 GRAM 恢复 */
                g_disp.dirty = true;       /* 触发下一帧重绘 */
                xTimerReset(xSleepTimer, 0);
                break;

            default:
                break;
            }
        }

        /*
         * 阶段二：按 dirty 标志决定是否重绘
         *         无变更时不刷屏，降低 I2C 总线占用。
         */
        if (g_disp.dirty && !g_disp.sleep) {
            /* 清空显存（仅操作内部缓冲区，不涉及 I2C） */
            oled_clear_gram();

            /* ==================== 状态栏（y=0~15） ==================== */

            /* WiFi 图标（16x16），x=0 左对齐 */
            if (g_disp.wifi_on)
                oled_draw_bitmap(0, 0, 16, 16, icon_wifi_on);

            /* 蓝牙图标（16x16），紧接 WiFi 图标右侧 x=18（+2px 间距） */
            if (g_disp.bt_on)
                oled_draw_bitmap(18, 0, 16, 16, icon_bt_on);

            /* 时间占位：预留 x=128-52=76px 区域，后续右对齐显示 */

            /* ==================== 分隔线（y=16） ==================== */
            draw_separator();

            /* ==================== 内容区（y=18~63） ==================== */

                /*
                 * 第 1 行：DS18B20 温度
                 * ds18b20_temp 为 int16_t，单位 0.1°C
                 * 负温度处理：取绝对值后拆出整数/小数部分分别显示
                 * 例：-105 → "-10.5 C"
                 */
                {
                    int16_t t = g_disp.ds18b20_temp;
                    uint8_t neg = 0;
                    if (t < 0) { neg = 1; t = -t; }
                    snprintf(buf, sizeof(buf), "DS18B20: %s%d.%d C",
                             neg ? "-" : "", t / 10, t % 10);
                    oled_show_string(0, CONTENT_START_Y, buf, FONT_SIZE);
                }

                /*
                 * 第 2 行：DHT11 温湿度
                 * dht11 精度为整数，直接拼接温度 + 湿度百分比
                 * 例："DHT11:   27 C 65%RH"
                 */
                snprintf(buf, sizeof(buf), "DHT11:   %d C %d%%RH",
                         g_disp.dht11_temp, g_disp.dht11_humi);
                oled_show_string(0, CONTENT_START_Y + FONT_SIZE, buf, FONT_SIZE);

                /*
                 * 第 3 行：按键总次数
                 * 由 DISP_MSG_KEY_PRESS 驱动递增，反映中断/扫描的响应状态
                 */
                snprintf(buf, sizeof(buf), "Key: %lu",
                         (unsigned long)g_disp.key_count);
                oled_show_string(0, CONTENT_START_Y + FONT_SIZE * 2, buf, FONT_SIZE);

            /*
             * 一次性将显存内容通过 I2C 推送到 OLED
             * oled_clear_gram() + 多次 oled_show_string()
             * 都是在 RAM 缓冲区操作，只有这里产生真实的 I2C 传输。
             */
            oled_refresh_gram();

            /* 清除脏标志，等待下一次数据变更 */
            g_disp.dirty = false;
        }

        /* 50Hz 帧率：每帧间隔 20ms */
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/**
 * @brief 初始化 OLED 显示系统
 *
 * 创建 disp_queue（长度 5），供各任务发送 DISP_MSG_xxx 消息。
 * 置 dirty = true 确保首帧立即渲染。
 *
 * @note 只做数据初始化，不创建任务 —— display_task 由 app_main 统一创建。
 */
void oled_display_init(void)
{
    /* 创建消息队列，最多缓存 5 条 disp_msg_t 消息 */
    disp_queue = xQueueCreate(5, sizeof(disp_msg_t));

    /* 设置初始显示数据 */
    g_disp.wifi_on = 0;
    g_disp.bt_on   = 0;
    g_disp.sleep   = 0;
    g_disp.ds18b20_temp = 0;
    g_disp.dht11_temp   = 0;
    g_disp.dht11_humi   = 0;
    g_disp.key_count    = 0;

    /* 创建息屏定时器：5s 自动重载，到点时发 SLEEP 消息 */
    xSleepTimer = xTimerCreate("scr_sleep",
                                pdMS_TO_TICKS(5000),
                                pdTRUE,                 /* 自动重载 */
                                NULL,
                                sleep_timer_callback);
    xTimerStart(xSleepTimer, 0);

    /* 标记脏页，保证首次进入 display_task 即触发全屏刷新 */
    g_disp.dirty = true;
}
