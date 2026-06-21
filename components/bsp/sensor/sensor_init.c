#include <stdio.h>
#include "sensor_init.h"
#include "ds18b20.h"
#include "dht11.h"
#include "debug.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"

/* 全局变量定义（供 MQTT 等模块读取最新读数） */
volatile short   g_sensor_ds18b20_temp = 0;
volatile uint8_t g_sensor_dht11_temp   = 0;
volatile uint8_t g_sensor_dht11_humi   = 0;

/* ================================================================
 * DS18B20 封装
 *
 * DS18B20 转换耗时约 750ms，通过 start + 异步等待分两步完成，
 * 避免阻塞任务循环。
 * ================================================================ */

static void sensor_update_ds18b20(void)
{
    static TickType_t last = 0;
    static uint8_t started = 0;
    const TickType_t interval = pdMS_TO_TICKS(800);

    TickType_t now = xTaskGetTickCount();

    if (started) {
        g_sensor_ds18b20_temp = ds18b20_get_temperature();
        started = 0;
    }
    if (!started && (now - last >= interval)) {
        ds18b20_start();
        last = now;
        started = 1;
    }
}

/* ================================================================
 * DHT11 封装
 *
 * DHT11 最小读取间隔 1 秒，内部自控周期。
 * ================================================================ */

static void sensor_update_dht11(void)
{
    static TickType_t last = 0;
    const TickType_t interval = pdMS_TO_TICKS(1010);

    TickType_t now = xTaskGetTickCount();

    if (now - last >= interval) {
        uint8_t t, h;
        if (dht11_read_data(&t, &h) == 0) {
            g_sensor_dht11_temp = t;
            g_sensor_dht11_humi = h;
        }
        last = now;
    }
}

/**
 * @brief 传感器采集任务（100ms 周期）
 *
 * 定时调用各传感器封装函数，读取数据存入全局变量供外部读取。
 *
 * @note DS18B20 异步 start/read 两步走，DHT11 内部自控 1s 间隔。
 */
void sensor_task(void *pv)
{
    esp_task_wdt_add(NULL);

    while (1) {
        sensor_update_ds18b20();
        sensor_update_dht11();
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

/* 静态创建所需内存 */
static StackType_t s_sensor_stack[2560];
static StaticTask_t s_sensor_tcb;

void sensor_start(void)
{
    xTaskCreateStaticPinnedToCore(sensor_task, "sensor_task",
        2560, NULL, 10,
        s_sensor_stack, &s_sensor_tcb,
        1);  /* Core 1：传感器数据给 UI 展示，和应用核同核 */
}
