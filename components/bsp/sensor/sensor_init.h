#ifndef __SENSOR_INIT_H
#define __SENSOR_INIT_H

/* 初始化所有传感器硬件（DS18B20 + DHT11） */
void sensor_init(void);

/* 传感器采集任务函数（100ms 周期） */
void sensor_task(void *pv);

/**
 * @brief 启动传感器采集任务
 *
 * 创建 sensor_task，100ms 周期，core 0，优先级 10。
 * 必须在传感器硬件初始化（sensor_init）之后调用。
 */
void sensor_start(void);

/* 最新传感器读数（供 MQTT 等模块读取） */
extern volatile short    g_sensor_ds18b20_temp;    /* DS18B20 温度 ×10，如 255=25.5°C */
extern volatile uint8_t  g_sensor_dht11_temp;      /* DHT11 温度，整数 °C */
extern volatile uint8_t  g_sensor_dht11_humi;      /* DHT11 湿度，整数 %RH */

#endif
