#ifndef __SENSOR_INIT_H
#define __SENSOR_INIT_H

/* 初始化所有传感器硬件（DS18B20 + DHT11） */
void sensor_init(void);

/* 传感器采集任务函数（100ms 周期），由 app_main 统一创建 */
void sensor_task(void *pv);

/* 最新传感器读数（供 MQTT 等模块读取） */
extern volatile short    g_sensor_ds18b20_temp;    /* DS18B20 温度 ×10，如 255=25.5°C */
extern volatile uint8_t  g_sensor_dht11_temp;      /* DHT11 温度，整数 °C */
extern volatile uint8_t  g_sensor_dht11_humi;      /* DHT11 湿度，整数 %RH */

#endif
