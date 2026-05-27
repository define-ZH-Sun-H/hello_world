# ESP32-S3 RMT（远程控制）外设使用指南

## 什么是 RMT

RMT（Remote Control）是 ESP32 系列芯片的硬件外设，设计初衷是**红外遥控信号**的发送与接收。但由于其灵活的脉冲生成能力，它已被广泛用于各类**时序敏感的单总线协议**：

- **WS2812/NeoPixel** RGB LED 驱动 ← 本项目实际用途
- 红外遥控（NEC、RC-5 等协议）
- DHT11/DHT22 温湿度传感器（替代位冲撞）
- 1-Wire 协议（DS18B20）
- 任意占空比 PWM 信号生成

### RMT 的工作原理

RMT 的核心是一个**符号链**（symbol chain）引擎：

```
存储区: [符号0] [符号1] [符号2] ... [符号N]
                │
         ┌──────┴──────┐
         │   level0     │  ← 电平（高/低）
         │  duration0   │  ← 持续时间（tick 数）
         │   level1     │
         │  duration1   │
         └─────────────┘
```

每个 RMT **符号**（`rmt_symbol_word_t`）包含两个段（pair）：`{电平0, 持续0}` + `{电平1, 持续1}`。RMT 硬件按顺序从内存中读取符号，自动控制 GPIO 输出电平，无需 CPU 干预。

```
RMT TX 工作流程:

  CPU 准备符号数据 → 写入 RMT 内存 → RMT 硬件自动逐符号输出波形 → 完成后产生中断
                       ↑
                 [CPU 可干别的]
```

### ESP32-S3 硬件规格

| 参数 | 值 |
|------|-----|
| RMT 组数 | 1 组 |
| 每组通道数 | 8 个（0~7） |
| TX 通道数 | 最多 8（每个通道可独立配置为 TX 或 RX） |
| RX 通道数 | 最多 8 |
| 每通道内存符号数 | 48 个（`SOC_RMT_MEM_WORDS_PER_CHANNEL=48`） |
| 时钟源 | XTAL、RC_FAST、APB（默认 `RMT_CLK_SRC_DEFAULT`） |
| 最大分辨率 | APB=80MHz → 12.5ns/tick |
| 载波调制 | 支持（红外场景常用 38kHz） |
| TX 循环计数 | 支持（硬件自动重复 N 次） |

> **注意：** ESP32-S3 没有独立的 RMT 内存块，通道共享一个内存池。虽然每个通道标称 48 个符号，但如果某个通道需要更多，可以通过 `mem_block_symbols` 配置从内存池中借用。

---

## 新旧驱动模型对比

ESP-IDF v5.x 引入了全新的 RMT 驱动架构。

| | 旧驱动（legacy） | 新驱动（recommended） |
|--|-----------------|---------------------|
| **头文件** | `driver/rmt.h` | `driver/rmt_tx.h` / `driver/rmt_rx.h` |
| **通道管理** | 静态编号（`rmt_channel_t`） | 动态分配（`rmt_channel_handle_t`） |
| **TX 发送** | `rmt_write_items()` | `rmt_transmit()` + 编码器 |
| **数据组织** | 用户手动填 `rmt_item32_t` 数组 | 编码器自动转换 |
| **灵活性** | 需手动管理符号缓冲区 | 编码器管道可组合、可复用 |
| **推荐程度** | ❌ 不推荐新项目使用 | ✅ 新项目首选 |

**本项目采用新驱动**，代码在 `components/bsp/rgb/` 中。

---

## 新驱动 API 详解

### 1. TX 通道配置与创建

#### `rmt_new_tx_channel()`

```c
esp_err_t rmt_new_tx_channel(
    const rmt_tx_channel_config_t *config,
    rmt_channel_handle_t *ret_chan
);
```

**参数说明：**

```c
typedef struct {
    gpio_num_t gpio_num;              // 输出 GPIO 引脚
    rmt_clock_source_t clk_src;       // 时钟源：RMT_CLK_SRC_DEFAULT（通常为 XTAL）
    uint32_t resolution_hz;           // 分辨率（Hz），决定每个 tick 的时间
    size_t mem_block_symbols;         // 内存符号数（默认 48，可增）
    size_t trans_queue_depth;         // 发送队列深度（允许预提交帧数）
    struct {
        uint32_t invert_out : 1;      // 输出电平翻转
        uint32_t with_dma : 1;        // 使用 DMA（大数据量推荐）
        uint32_t io_loop_back : 1;    // 回环模式（调试用）
        uint32_t io_od_mode : 1;      // 开漏输出
    } flags;
} rmt_tx_channel_config_t;
```

**本项目实例：**

```c
rmt_tx_channel_config_t tx_chan_config = {
    .clk_src = RMT_CLK_SRC_DEFAULT,
    .gpio_num = RGB_GPIO,              /* GPIO16 */
    .mem_block_symbols = 64,           /* 64 符号（24bits × 2 + 复位码够用） */
    .resolution_hz = RGB_RESOLUTION_HZ,/* 10MHz → 0.1μs/tick */
    .trans_queue_depth = 4,            /* 队列深度 4 */
};
ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, &led_chan));
```

#### `rmt_enable()`

```c
esp_err_t rmt_enable(rmt_channel_handle_t chan);
```

启用 TX 通道，分配硬件资源。必须在首次 `rmt_transmit()` 之前调用。调用后通道进入就绪状态，可以接受发送请求。

#### `rmt_disable()`

```c
esp_err_t rmt_disable(rmt_channel_handle_t chan);
```

禁用 TX 通道，释放硬件资源。通常用于低功耗场景。

#### 初始化流程图

```
rmt_new_tx_channel()    创建通道、配置 GPIO、分配内存
        │
        ▼
rmt_enable()            使能通道、准备发送
        │
        ▼
rmt_transmit()          提交发送任务
        │
        ▼
rmt_tx_wait_all_done()  等待发送完成（阻塞）
```

---

### 2. 发送数据

#### `rmt_transmit()`

```c
esp_err_t rmt_transmit(
    rmt_channel_handle_t chan,
    rmt_encoder_handle_t encoder,
    const void *payload,
    size_t payload_bytes,
    const rmt_transmit_config_t *config
);
```

| 参数 | 说明 |
|------|------|
| `chan` | TX 通道句柄 |
| `encoder` | 编码器句柄，决定如何将 payload 转为 RMT 符号 |
| `payload` | 待发送数据缓冲区 |
| `payload_bytes` | 数据长度（字节） |
| `config` | 发送配置 |

**发送配置：**

```c
typedef struct {
    int loop_count;               // 循环次数（0=单次，-1=无限循环）
    uint32_t loop_phase_reset : 1;// 循环间是否复位相位
} rmt_transmit_config_t;
```

> **注意：** `rmt_transmit()` 是**非阻塞**的。它将编码任务提交到发送队列后立即返回。实际发送由 RMT 硬件在后台完成。

#### `rmt_tx_wait_all_done()`

```c
esp_err_t rmt_tx_wait_all_done(rmt_channel_handle_t chan, int timeout_ms);
```

阻塞等待当前通道**所有已提交**的发送任务完成。`timeout_ms` 可以传 `portMAX_DELAY` 无限等待。

本项目将 `rmt_transmit()` 和 `rmt_tx_wait_all_done()` 配对使用，实现"发送并等待完成"的同步模式：

```c
static void rgb_flush(void)
{
    rmt_transmit_config_t tx_config = { .loop_count = 0 };
    ESP_ERROR_CHECK(rmt_transmit(led_chan, led_encoder,
                                 led_pixels, sizeof(led_pixels),
                                 &tx_config));
    ESP_ERROR_CHECK(rmt_tx_wait_all_done(led_chan, portMAX_DELAY));
}
```

#### 非阻塞发送模式（高吞吐场景）

如果不需要等待完成，可以去掉 `rmt_tx_wait_all_done()`，RMT 硬件会自动从队列中取下一个任务：

```c
/* 非阻塞发送 - 适合连续帧场景（如 LED 动画） */
rmt_transmit(chan, encoder, pixels, len, &tx_config);
// 不等待，立即返回，CPU 可继续准备下一帧
```

---

### 3. 编码器系统

新驱动引入了**编码器**（encoder）的概念，将高层数据自动转换为底层的 RMT 符号。

#### 内置编码器

##### `rmt_new_bytes_encoder()`

将字节数组编码为 RMT 符号序列（每个字节的每一位映射为一个 RMT 符号）。

```c
esp_err_t rmt_new_bytes_encoder(
    const rmt_bytes_encoder_config_t *config,
    rmt_encoder_handle_t *ret_encoder
);
```

```c
typedef struct {
    struct {
        uint32_t level0;      // 位 0 第一段电平
        uint32_t duration0;   // 位 0 第一段持续时间（tick 数）
        uint32_t level1;      // 位 0 第二段电平
        uint32_t duration1;   // 位 0 第二段持续时间（tick 数）
    } bit0;                   // 逻辑 "0" 的波形
    struct {
        uint32_t level0;      // 位 1 第一段电平
        uint32_t duration0;   // 位 1 第一段持续时间（tick 数）
        uint32_t level1;      // 位 1 第二段电平
        uint32_t duration1;   // 位 1 第二段持续时间（tick 数）
    } bit1;                   // 逻辑 "1" 的波形
    struct {
        uint32_t msb_first : 1; // MSB 优先（默认 1）
    } flags;
} rmt_bytes_encoder_config_t;
```

**WS2812 的位编码：**

```c
/* 10MHz 分辨率下，1 tick = 0.1μs */
rmt_bytes_encoder_config_t bytes_encoder_config = {
    .bit0 = {
        .level0 = 1, .duration0 = 3,    /* 0 码：0.3μs 高 */
        .level1 = 0, .duration1 = 9,    /*       0.9μs 低 */
    },
    .bit1 = {
        .level0 = 1, .duration0 = 9,    /* 1 码：0.9μs 高 */
        .level1 = 0, .duration1 = 3,    /*       0.3μs 低 */
    },
    .flags.msb_first = 1,               /* 高位优先 */
};
```

```
WS2812 时序:

0 码:  ┌────┐                  bit0 = {1, 3, 0, 9}
       │    │                         高 0.3μs + 低 0.9μs
       │    └────────────────────
       └────┘
       │0.3μs│
       └─ 0.9μs ─┘

1 码:  ┌────────────┐           bit1 = {1, 9, 0, 3}
       │            │                 高 0.9μs + 低 0.3μs
       │            └──────
       └────────────┘
       └─ 0.9μs ─┘│0.3μs│
```

##### `rmt_new_copy_encoder()`

将原始 RMT 符号数据直接复制到发送缓冲区，适合附加复位码、帧头等固定波形。

```c
esp_err_t rmt_new_copy_encoder(
    const rmt_copy_encoder_config_t *config,
    rmt_encoder_handle_t *ret_encoder
);
```

参数 `rmt_copy_encoder_config_t` 当前为空结构体（`{}`），保留用于未来扩展。

#### 自定义编码器

除了内置编码器，用户可以组合多个编码器形成**编码管道**，或编写完全自定义的编码器。

**编码器接口：**

```c
typedef struct rmt_encoder {
    size_t (*encode)(rmt_encoder_t *encoder,
                     rmt_channel_handle_t channel,
                     const void *primary_data,
                     size_t data_size,
                     rmt_encode_state_t *ret_state);
    esp_err_t (*del)(rmt_encoder_t *encoder);
    esp_err_t (*reset)(rmt_encoder_t *encoder);
} rmt_encoder_t;
```

| 回调 | 说明 |
|------|------|
| `encode` | 编码函数，将 data 转为 RMT 符号。返回已编码符号数 |
| `del` | 删除编码器，释放内存 |
| `reset` | 复位编码器内部状态（发送下一帧前调用） |

**编码状态（`rmt_encode_state_t`）：**

```c
typedef enum {
    RMT_ENCODING_RESET      = 0,      // 初始/复位状态
    RMT_ENCODING_COMPLETE   = (1 << 0), // 编码完成
    RMT_ENCODING_MEM_FULL   = (1 << 1), // RMT 内存已满，下次继续
} rmt_encode_state_t;
```

#### 编码管道：本项目 WS2812 示例

本项目组合了两个内置编码器实现 WS2812 协议：

```
数据流:
  led_pixels[3]    bytes_encoder     copy_encoder     RMT TX → GPIO16
  (G-R-B bytes)  ─────────────►  ─────────────►
                    byte→符号序列   追加复位码

编码器切换:
  case 0: bytes_encoder.encode(...)     ← 将 3 字节展开为 24 个 RMT 符号
         如果完成 → state = 1           ← 切换到 case 1

  case 1: copy_encoder.encode(...)      ← 附加 50μs 复位码
         如果完成 → state = RESET       ← 回到初始状态
```

**复位码生成：**

```c
/* 50μs 低电平复位码（WS2812 latch 条件） */
uint32_t reset_ticks = config->resolution / 1000000 * 50 / 2;
led_encoder->reset_code = (rmt_symbol_word_t) {
    .level0 = 0,
    .duration0 = reset_ticks,
    .level1 = 0,
    .duration1 = reset_ticks,
};
```

> **为什么复位码是 50μs / 2 = 25μs 的两段？** 因为 RMT 一个符号固定有两段（pair）。用两段低电平共 50μs 等于一个完整的 RESET 脉冲。WS2812 要求数据结束后保持低电平 ≥ 50μs 锁存输出。

---

### 4. RX 通道配置（参考）

本项目未使用 RMT RX，但了解 RX 配置有助于完整理解 RMT。

#### `rmt_new_rx_channel()`

```c
esp_err_t rmt_new_rx_channel(
    const rmt_rx_channel_config_t *config,
    rmt_channel_handle_t *ret_chan
);
```

```c
typedef struct {
    gpio_num_t gpio_num;              // 输入 GPIO
    rmt_clock_source_t clk_src;       // 时钟源
    uint32_t resolution_hz;           // 分辨率
    size_t mem_block_symbols;         // 内存符号数
    struct {
        uint32_t invert_in : 1;       // 输入电平翻转
        uint32_t with_dma : 1;        // 使用 DMA
        uint32_t io_loop_back : 1;    // 回环模式
    } flags;
} rmt_rx_channel_config_t;
```

#### `rmt_receive()`

```c
esp_err_t rmt_receive(
    rmt_channel_handle_t chan,
    rmt_symbol_word_t *buffer,
    size_t buffer_size,
    const rmt_receive_config_t *config
);
```

```c
typedef struct {
    uint32_t signal_range_min_ns;    // 最短脉冲（滤除噪声）
    uint32_t signal_range_max_ns;    // 最长脉冲（判定结束）
} rmt_receive_config_t;
```

RX 模式通过**信号结束检测**自动停止接收：当总线空闲时间超过 `signal_range_max_ns` 时，RMT 认为接收完成并触发回调。

#### `rmt_rx_event_callbacks_t`

```c
typedef struct {
    rmt_rx_done_callback_t on_recv_done;  // 接收完成回调
} rmt_rx_event_callbacks_t;

esp_err_t rmt_rx_register_event_callbacks(
    rmt_channel_handle_t chan,
    const rmt_rx_event_callbacks_t *cbs,
    void *user_data
);
```

---

### 5. 其他常用函数

#### `rmt_del_encoder()`

```c
esp_err_t rmt_del_encoder(rmt_encoder_handle_t encoder);
```

删除编码器，释放内存。在自定义编码器的 `del` 回调中，通常会递归调用此函数删除子编码器。

#### `rmt_encoder_reset()`

```c
esp_err_t rmt_encoder_reset(rmt_encoder_handle_t encoder);
```

复位编码器内部状态。发送下一帧数据前应调用一次，确保编码器从初始状态开始。

#### `rmt_del_channel()`

```c
esp_err_t rmt_del_channel(rmt_channel_handle_t chan);
```

删除 RMT 通道，释放分配的硬件资源（GPIO、内存符号等）。调用前需先 `rmt_disable()`。

---

## 设计要点与常见陷阱

### 1. 分辨率（resolution_hz）的选择

分辨率决定 tick 时间和最终波形的精度：

```c
/* 不同分辨率下的 tick 时间 */
RMT_RESOLUTION_HZ = 1_000_000   // 1MHz → 1 tick = 1μs
RMT_RESOLUTION_HZ = 10_000_000  // 10MHz → 1 tick = 0.1μs
RMT_RESOLUTION_HZ = 80_000_000  // 80MHz → 1 tick = 12.5ns
```

| 场景 | 推荐分辨率 | 理由 |
|------|----------|------|
| WS2812（0.3μs 精度） | 10MHz | 0.1μs/tick，精度足够，不浪费 |
| 红外 NEC（560μs 精度） | 1MHz | 1μs/tick，节省内存 |
| 高速协议 | 40~80MHz | 需配合 DMA 使用 |

> **原则：** 在满足精度要求的前提下，**分辨率越低越好**。高分辨率意味着每个符号占用的 tick 数量越大，容易溢出 `duration` 字段（最大 32767 ticks）。例如 80MHz 下要生成 50μs 脉冲：`50μs / 12.5ns = 4000 ticks`，安全。但要生成 100ms 脉冲：`100ms / 12.5ns = 8,000,000 > 32767`，溢出！

### 2. 内存符号数（mem_block_symbols）

每通道默认 48 个符号，部分场景需要调整：

| 场景 | 所需符号数 | 建议值 |
|------|-----------|-------|
| WS2812 单灯（24 bits） | 24 × 2 + 1 = 49 | 64 |
| WS2812 级联 10 灯 | 10 × 49 = 490 | 512（需 DMA） |
| 红外 NEC 数据帧 | ~68 | 64~128 |

符号数超过 48 时，实际上是从共享内存池中借用。超过 512 建议启用 DMA（`flags.with_dma = true`）。

### 3. DMA 模式

当数据量较大（如级联多颗 WS2812）时，DMA 模式直接将 RMT 符号写入内部 SRAM，不受通道内存限制：

```c
rmt_tx_channel_config_t config = {
    .mem_block_symbols = 1024,    // 即使设置较大值，DMA 模式下也无压力
    .flags.with_dma = true,       // 启用 DMA
};
```

### 4. 编码器在 IRAM 中

`sdkconfig` 中的配置：

```
CONFIG_RMT_ENCODER_FUNC_IN_IRAM=y    // 编码器代码放在 IRAM
```

当系统 Flash Cache 繁忙时（如 OTA、Flash 操作），IRAM 中的编码器代码仍可执行，确保 RMT 波形不中断。

### 5. 循环发送

`rmt_transmit_config_t.loop_count` 可以让 RMT 硬件自动重复发送同一帧：

```c
/* 无限循环闪烁 - 不占 CPU */
rmt_transmit_config_t tx_config = { .loop_count = -1 };
rmt_transmit(chan, encoder, data, len, &tx_config);
```

停止循环：调用 `rmt_disable()` 或发送一帧 `loop_count=0` 的数据。

### 6. TX 通道的异步停止

ESP32-S3 的 RMT 支持 TX 异步停止（`SOC_RMT_SUPPORT_TX_ASYNC_STOP`），可以在发送中途停止当前传输，适合需要打断的场景。

### 7. 本项目 RMT 配置总览

```
WS2812 RGB LED
  GPIO:   16
  RMT:    TX channel 0
  时钟:   10MHz (0.1μs/tick)
  编码:   bytes_encoder(0码=3+9t, 1码=9+3t) → copy_encoder(50μs 复位)
  数据:   3 bytes (G-R-B)
  发送:   同步模式 (transmit + wait_all_done)
```

### 8. 回调函数运行上下文

RMT 的 TX 完成中断和 RX 接收完成回调在 **ISR 上下文**中运行。回调中应遵循 ISR 编程准则：

```c
/* ✅ 正确：回调中发通知唤醒任务 */
static bool IRAM_ATTR rmt_done_cb(rmt_channel_handle_t chan,
                                   const rmt_tx_done_event_data_t *edata,
                                   void *user_data)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR((TaskHandle_t)user_data,
                           &xHigherPriorityTaskWoken);
    return xHigherPriorityTaskWoken == pdTRUE;
}

/* ❌ 禁止：回调中调用阻塞 API */
// vTaskDelay(10);  // 不行！
// printf("done\n"); // 小心（可能阻塞）
```

---

## API 速查表

### TX 相关

| 函数 | 用途 | 是否阻塞 |
|------|------|---------|
| `rmt_new_tx_channel()` | 创建 TX 通道 | 否 |
| `rmt_enable()` | 使能 TX 通道 | 否 |
| `rmt_transmit()` | 提交数据到发送队列 | 否（立即返回） |
| `rmt_tx_wait_all_done()` | 等待全部发送完成 | **是** |
| `rmt_disable()` | 禁用 TX 通道 | 否 |
| `rmt_del_channel()` | 删除 TX 通道 | 否 |
| `rmt_new_led_strip_encoder()` | 创建 WS2812 编码器（自定义） | 否 |
| `rmt_new_bytes_encoder()` | 创建字节编码器 | 否 |
| `rmt_new_copy_encoder()` | 创建复制编码器 | 否 |
| `rmt_del_encoder()` | 删除编码器 | 否 |
| `rmt_encoder_reset()` | 复位编码器状态 | 否 |

### RX 相关

| 函数 | 用途 | 是否阻塞 |
|------|------|---------|
| `rmt_new_rx_channel()` | 创建 RX 通道 | 否 |
| `rmt_enable()` | 使能 RX 通道 | 否 |
| `rmt_receive()` | 启动接收 | 否（回调异步返回） |
| `rmt_rx_register_event_callbacks()` | 注册接收完成回调 | 否 |

### 发送配置参数

```c
rmt_transmit_config_t {
    int loop_count;              // 0=单次, >0=循环N次, -1=无限循环
    uint32_t loop_phase_reset;   // 循环间是否复位(1=复位)
}
```

### 接收配置参数

```c
rmt_receive_config_t {
    uint32_t signal_range_min_ns;  // 最短有效脉冲(ns)
    uint32_t signal_range_max_ns;  // 最长有效脉冲(ns)，超时即结束
}
```
