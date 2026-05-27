#include <stdio.h>
#include "sensor_init.h"
#include "ds18b20.h"
#include "dht11.h"
#include "oled_display.h"
#include "debug.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"

/**
 * @brief 传感器采集任务（100ms 周期）
 *
 * 定时读取所有已初始化的传感器数据，
 * 通过 disp_queue 发送消息驱动 OLED 刷新。
 *
 * === 传感器特性 ===
 *
 * DS18B20：每次调用 ds18b20_get_temperature() 内部触发转换+等待，
 *          单次约 750ms，任务内部通过 start + delay 异步等待。
 *
 * DHT11：  最小读取间隔 1 秒，由任务显式控制。
 */
void sensor_task(void *pv)
{
    TickType_t last_ds18b20 = 0;
    TickType_t last_dht11 = 0;
    static uint8_t ds18b20_started = 0;

    const TickType_t ds18b20_interval = pdMS_TO_TICKS(800); /* DS18B20 转换需 ~750ms */
    const TickType_t dht11_interval   = pdMS_TO_TICKS(1010); /* DHT11 最小间隔 1s */
    esp_task_wdt_add(NULL);

    while (1) {
        TickType_t now = xTaskGetTickCount();

        /*
         * DS18B20：异步转换模式
         * 上一周期 start → 等待转换完成 → 本周期读结果
         */
        if (ds18b20_started) {
            /* 读取上一周期启动的转换结果 */
            short temp = ds18b20_get_temperature();
            g_disp.ds18b20_temp = temp;
            g_disp.dirty = true;
            ds18b20_started = 0;
        }
        if (!ds18b20_started && (now - last_ds18b20 >= ds18b20_interval)) {
            /* 启动新一轮转换（非阻塞，仅发指令） */
            ds18b20_start();
            last_ds18b20 = now;
            ds18b20_started = 1;
        }

        /*
         * DHT11：显式间隔控制，≥1s 才发起读取
         * 读取成功后通过队列发送消息，由 display_task 更新屏幕。
         */
        if (now - last_dht11 >= dht11_interval) {
            uint8_t t, h;
            if (dht11_read_data(&t, &h) == 0) {
                disp_msg_t msg = {
                    .type = DISP_MSG_DHT11_DATA,
                    .val1 = (int16_t)t,
                    .val2 = (int16_t)h,
                };
                xQueueSend(disp_queue, &msg, 0);
                last_dht11 = now;
            }
        }

        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/**
 * @brief 初始化所有板载传感器
 *
 * === 初始化流程 ===
 *
 *   1. 初始化 DS18B20（1-Wire 复位 + 应答检测）
 *   2. 初始化 DHT11
 *
 * @note 只做硬件初始化，不创建任务 —— sensor_task 由 app_main 统一创建。
 */
void sensor_init(void)
{
    /* === DS18B20 === */
    if (ds18b20_init() == 0)
        DBG_INFO("DS18B20 检测成功\n");
    else
        DBG_WARN("DS18B20 未检测到\n");

    /* === DHT11 === */
    if (dht11_init() == 0)
        DBG_INFO("DHT11 检测成功\n");
    else
        DBG_WARN("DHT11 未检测到\n");

    DBG_INFO("传感器初始化完成\n");
}
