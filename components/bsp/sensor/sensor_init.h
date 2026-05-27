#ifndef __SENSOR_INIT_H
#define __SENSOR_INIT_H

/* 初始化所有传感器硬件（DS18B20 + DHT11） */
void sensor_init(void);

/* 传感器采集任务函数（100ms 周期），由 app_main 统一创建 */
void sensor_task(void *pv);

#endif
