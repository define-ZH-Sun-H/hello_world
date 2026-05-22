#ifndef __DS18B20_H
#define __DS18B20_H

#include "driver/gpio.h"

/* 引脚定义 */
#define DS18B20_DQ_GPIO_PIN       GPIO_NUM_13

/* IO 操作宏 */
#define ds18b20_dq_in()           gpio_get_level(DS18B20_DQ_GPIO_PIN)
#define ds18b20_dq_out(x)         gpio_set_level(DS18B20_DQ_GPIO_PIN, x)

/* 函数声明 */
void ds18b20_reset(void);
uint8_t ds18b20_check(void);
uint8_t ds18b20_read_bit(void);
uint8_t ds18b20_read_byte(void);
void ds18b20_write_byte(uint8_t data);
void ds18b20_start(void);
uint8_t ds18b20_init(void);
short ds18b20_get_temperature(void);

#endif
