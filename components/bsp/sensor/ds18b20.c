#include "ds18b20.h"
#include "esp_rom_sys.h"

// 复位 DS18B20：拉低 750μs → 释放
void ds18b20_reset(void)
{
    ds18b20_dq_out(0);
    esp_rom_delay_us(750);
    ds18b20_dq_out(1);
    esp_rom_delay_us(15);
}

// 检测 DS18B20 是否存在，返回 0=正常
uint8_t ds18b20_check(void)
{
    uint8_t retry;

    for (retry = 0; ds18b20_dq_in() && retry < 200; retry++)
        esp_rom_delay_us(1);
    if (retry >= 200)
        return 1;  // 无应答

    for (retry = 0; !ds18b20_dq_in() && retry < 240; retry++)
        esp_rom_delay_us(1);
    return (retry >= 240) ? 1 : 0;
}

// 从 DS18B20 读一个位
uint8_t ds18b20_read_bit(void)
{
    uint8_t data = 0;
    ds18b20_dq_out(0);
    esp_rom_delay_us(2);
    ds18b20_dq_out(1);
    esp_rom_delay_us(12);

    if (ds18b20_dq_in())
        data = 1;

    esp_rom_delay_us(50);
    return data;
}

// 从 DS18B20 读一个字节（低位优先）
uint8_t ds18b20_read_byte(void)
{
    uint8_t data = 0;
    for (int i = 0; i < 8; i++)
        data |= ds18b20_read_bit() << i;
    return data;
}

// 写一个字节到 DS18B20（低位优先）
void ds18b20_write_byte(uint8_t data)
{
    for (int i = 0; i < 8; i++)
    {
        if (data & 0x01)
        {
            ds18b20_dq_out(0);  esp_rom_delay_us(2);
            ds18b20_dq_out(1);  esp_rom_delay_us(60);
        }
        else
        {
            ds18b20_dq_out(0);  esp_rom_delay_us(60);
            ds18b20_dq_out(1);  esp_rom_delay_us(2);
        }
        data >>= 1;
    }
}

// 启动温度转换
void ds18b20_start(void)
{
    ds18b20_reset();
    ds18b20_check();
    ds18b20_write_byte(0xcc);  // 跳过 ROM
    ds18b20_write_byte(0x44);  // 开始转换
}

// 初始化 DS18B20：GPIO 开漏模式 → 复位检测，返回 0=正常
uint8_t ds18b20_init(void)
{
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pin_bit_mask = 1ull << DS18B20_DQ_GPIO_PIN,
    };
    gpio_config(&io_conf);

    ds18b20_reset();
    return ds18b20_check();
}

// 获取温度，返回 温度×10（如 25.5°C → 255）
short ds18b20_get_temperature(void)
{
    ds18b20_reset();
    ds18b20_check();
    ds18b20_write_byte(0xcc);           // 跳过 ROM
    ds18b20_write_byte(0xbe);           // 读暂存器

    uint8_t tl = ds18b20_read_byte();   // LSB
    uint8_t th = ds18b20_read_byte();   // MSB

    short raw = (th << 8) | tl;
    short temp;

    if (th > 7) // 负数
    {
        raw = (~raw) + 1;
        temp = -(raw * 625 / 1000);     // 精度 0.0625°C，×10 输出
    }
    else
    {
        temp = raw * 625 / 1000;
    }

    return temp;
}
