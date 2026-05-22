#include "dht11.h"
#include "esp_rom_sys.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// 复位 DHT11：拉低 ≥18ms → 释放 20~40μs
// DHT11 的启动信号需要毫秒级拉低（远长于 DS18B20 的 750μs）
void dht11_reset(void)
{
    dht11_dq_out(0);
    vTaskDelay(pdMS_TO_TICKS(25));      // 拉低 ≥18ms（给 25ms 余量）
    dht11_dq_out(1);
    esp_rom_delay_us(30);               // 释放后等 30μs 让 DHT11 感知
}

// 检测 DHT11 是否存在，返回 0=正常
// DHT11 应答：拉低 40~80μs → 释放拉高 40~80μs
uint8_t dht11_check(void)
{
    uint8_t retry;

    for (retry = 0; dht11_dq_in() && retry < 100; retry++)
        esp_rom_delay_us(1);
    if (retry >= 100)
        return 1;  // 无应答

    for (retry = 0; !dht11_dq_in() && retry < 100; retry++)
        esp_rom_delay_us(1);
    return (retry >= 100) ? 1 : 0;
}

// 从 DHT11 读一个位
// 每 bit：低电平 50μs → 高电平（26~28μs=0，70μs=1）
uint8_t dht11_read_bit(void)
{
    uint8_t retry;

    // 等待 DHT11 拉低（开始信号）
    for (retry = 0; dht11_dq_in() && retry < 100; retry++)
        esp_rom_delay_us(1);
    // 等待 DHT11 释放（准备数据）
    for (retry = 0; !dht11_dq_in() && retry < 100; retry++)
        esp_rom_delay_us(1);
    // 延时 40μs 后采样（避开前导低电平，在数据窗口中心采样）
    esp_rom_delay_us(40);

    return dht11_dq_in() ? 1 : 0;
}

// 从 DHT11 读一个字节（高位优先，与 DS18B20 相反）
static uint8_t dht11_read_byte(void)
{
    uint8_t data = 0;
    for (int i = 0; i < 8; i++)
    {
        data <<= 1;                 // 先左移，高位在前
        data |= dht11_read_bit();   // 当前 bit 填入最低位
    }
    return data;
}

// 读取一次温湿度数据，返回 0=正常
// 数据格式（40 bit）：湿整 + 湿小 + 温整 + 温小 + 校验
// 校验 = 前 4 字节之和的低 8 位
// DHT11 小数部分始终为 0（DHT22 才包含小数数据）
uint8_t dht11_read_data(uint8_t *temp, uint8_t *humi)
{
    uint8_t buf[5];

    dht11_reset();
    if (dht11_check())
        return 1;  // DHT11 无应答

    for (int i = 0; i < 5; i++)
        buf[i] = dht11_read_byte();

    // 校验和验证
    if ((buf[0] + buf[1] + buf[2] + buf[3]) != buf[4])
        return 1;

    *humi = buf[0];     // 湿度整数（%RH）
    *temp = buf[2];     // 温度整数（°C）
    return 0;
}

// 初始化 DHT11：GPIO 开漏模式 → 复位检测，返回 0=正常
uint8_t dht11_init(void)
{
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pin_bit_mask = 1ull << DHT11_DQ_GPIO_PIN,
    };
    gpio_config(&io_conf);

    dht11_reset();
    return dht11_check();
}
