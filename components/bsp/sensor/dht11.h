#ifndef __DHT11_H
#define __DHT11_H

#include "driver/gpio.h"

/* 引脚定义 */
#define DHT11_DQ_GPIO_PIN       GPIO_NUM_45

/* IO 操作宏 */
#define dht11_dq_in()           gpio_get_level(DHT11_DQ_GPIO_PIN)
#define dht11_dq_out(x)         gpio_set_level(DHT11_DQ_GPIO_PIN, x)

/* 函数声明 */
void dht11_reset(void);                             // 复位 DHT11
uint8_t dht11_check(void);                          // 检测 DHT11 是否存在，返回 0=正常
uint8_t dht11_init(void);                           // 初始化 DHT11，返回 0=正常
uint8_t dht11_read_data(uint8_t *temp, uint8_t *humi);  // 读取温湿度，返回 0=正常

#endif
