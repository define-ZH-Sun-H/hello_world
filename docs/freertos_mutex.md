# FreeRTOS 互斥锁（Mutex）使用指南

## 为什么需要互斥锁

当多个任务或 ISR 访问同一块内存（全局变量、结构体、外设寄存器）时，如果**至少有一个是写操作**，就会产生**竞争条件**（Race Condition）。

典型场景：

```c
/* 任务 A 读取-修改-写入 */
g_disp.key_count++;
// 编译展开: tmp = g_disp.key_count; tmp++; g_disp.key_count = tmp;

/* 任务 B 同时读取-修改-写入 */
g_disp.key_count++;
// 如果 A 和 B 交错执行，最终 key_count 只加了 1 而非 2
```

多核芯片（ESP32-S3 双核）上，两个任务**真正并行**运行在不同核上，这种问题更容易发生。

---

## FreeRTOS 互斥锁 API

FreeRTOS 提供两种"锁"：

| 类型 | 特点 | 使用场景 |
|------|------|---------|
| **二值信号量** | 没有优先级继承，可能造成优先级反转 | 任务同步（ISR → 任务通知） |
| **互斥锁** | 有优先级继承，避免优先级反转 | 保护共享资源 |

### 优先级反转问题

```
低优先级任务 L 持有锁
        ↓
高优先级任务 H 等待锁 → 被阻塞
        ↓
中优先级任务 M 运行（不涉及该锁）→ L 无法运行 → 锁无法释放 → H 一直等
```

互斥锁的**优先级继承**机制：当 H 等待锁时，L 临时继承 H 的优先级，让 L 尽快运行释放锁。

---

## 在 ESP-IDF 中使用互斥锁

### 1. 创建锁

```c
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

SemaphoreHandle_t xMutex = NULL;

void app_main(void)
{
    xMutex = xSemaphoreCreateMutex();
    // ...
}
```

### 2. 加锁 / 解锁

```c
/* 加锁（阻塞等待，portMAX_DELAY = 一直等） */
xSemaphoreTake(xMutex, portMAX_DELAY);

g_disp.key_count++;    /* 访问共享资源 */

/* 解锁 */
xSemaphoreGive(xMutex);
```

### 3. 带超时的加锁

```c
if (xSemaphoreTake(xMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    /* 成功拿到锁，安全访问资源 */
    g_disp.key_count++;
    xSemaphoreGive(xMutex);
} else {
    /* 100ms 内未拿到锁，做超时处理 */
    printf("获取互斥锁超时\n");
}
```

---

## 在项目中的使用实例

### 实例：保护 OLED 显示数据结构体

当前 `g_disp` 被多个任务（按键任务、显示任务、可能未来添加的传感器任务）同时读写，双核环境下存在竞争风险。

```c
/* oled_display.c */
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

display_t g_disp = { .dirty = false };
SemaphoreHandle_t xDispMutex = NULL;    /* 显示数据互斥锁 */

void display_task(void *pv)
{
    disp_msg_t msg;
    while (1) {
        /* 收队列消息时不需要锁——队列本身是线程安全的 */
        while (xQueueReceive(disp_queue, &msg, 0) == pdTRUE) {
            switch (msg.type) {
            case DISP_MSG_KEY_PRESS:
                xSemaphoreTake(xDispMutex, portMAX_DELAY);
                g_disp.key_count++;
                g_disp.dirty = true;
                xSemaphoreGive(xDispMutex);
                break;
            // ...
            }
        }

        if (g_disp.dirty) {
            /* 读 dirty 和渲染时锁住整个操作 */
            xSemaphoreTake(xDispMutex, portMAX_DELAY);
            oled_clear_gram();
            // ... 绘制图标、文本 ...
            oled_refresh_gram();
            g_disp.dirty = false;
            xSemaphoreGive(xDispMutex);
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void oled_display_task_start(void)
{
    xDispMutex = xSemaphoreCreateMutex();   /* 创建互斥锁 */
    disp_queue = xQueueCreate(5, sizeof(disp_msg_t));
    xTaskCreatePinnedToCore(display_task, "oled_disp", 3072, NULL, 5, NULL, 1);
}
```

按键任务中写 `g_disp` 同样加锁：

```c
/* key.c — K3 按下 */
case KEY_IND_EVENT_PRESS:
{
    short t = ds18b20_get_temperature();
    xSemaphoreTake(xDispMutex, portMAX_DELAY);
    g_disp.ds18b20_temp = t;
    g_disp.dirty = true;
    xSemaphoreGive(xDispMutex);
    printf("K3 按下, DS18B20: %d.%d C\n", t / 10, t % 10);
}
break;
```

### 实例：保护 UART 打印不交错

多个任务同时 `printf` 可能导致字符交错。用互斥锁可以让每个任务的打印保持完整：

```c
static SemaphoreHandle_t xPrintMutex = NULL;

void safe_printf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    xSemaphoreTake(xPrintMutex, portMAX_DELAY);
    vprintf(fmt, args);
    xSemaphoreGive(xPrintMutex);

    va_end(args);
}

// 初始化
xPrintMutex = xSemaphoreCreateMutex();
```

---

## 使用注意事项

### 1. 持有时间越短越好

互斥锁应当在**持有时间最短**的原则下使用。不要在持有锁时做延时、I2C 传输、串口打印等耗时操作：

```c
/* ❌ 错误：持有锁期间做耗时操作 */
xSemaphoreTake(xMutex, portMAX_DELAY);
oled_refresh_gram();    /* I2C 传输可能数毫秒 */
vTaskDelay(10);         /* 阻塞其他任务 */
xSemaphoreGive(xMutex);

/* ✅ 正确：快读快写，耗时操作在外面 */
xSemaphoreTake(xMutex, portMAX_DELAY);
memcpy(&local_copy, &shared_data, sizeof(shared_data));
xSemaphoreGive(xMutex);
// 然后用 local_copy 去做耗时操作
```

### 2. ISR 中不能使用

中断服务程序（ISR）中不能用 `xSemaphoreTake`（它会阻塞）。ISR 中要传递数据，应该用**队列**（`xQueueSendFromISR`）：

```c
/* ❌ 错误：ISR 中不能 take 互斥锁 */
void IRAM_ATTR isr_handler(void *arg)
{
    xSemaphoreTake(xMutex, portMAX_DELAY);  // 崩溃！
}

/* ✅ 正确：ISR 中用队列发送，任务中处理 */
void IRAM_ATTR isr_handler(void *arg)
{
    disp_msg_t msg = { .type = DISP_MSG_KEY_PRESS };
    xQueueSendFromISR(disp_queue, &msg, NULL);
}
```

### 3. 不要忘记释放

`xSemaphoreTake` 和 `xSemaphoreGive` 必须成对出现。函数中间 `return` 或 `break` 容易遗漏，可以用 `goto` 统一出口：

```c
xSemaphoreTake(xMutex, portMAX_DELAY);
if (condition) {
    xSemaphoreGive(xMutex);
    return;     /* 每个 return 前都要 give */
}
xSemaphoreGive(xMutex);

/* 更好的方式：集中出口 */
xSemaphoreTake(xMutex, portMAX_DELAY);
{
    // 逻辑
    if (error) goto out;
    // 更多逻辑
}
out:
xSemaphoreGive(xMutex);
```

### 4. 全局变量 vs 队列 vs 互斥锁

| 方式 | 适用场景 | 优势 | 劣势 |
|------|---------|------|------|
| **全局变量 + dirty** | 单核，低频写入 | 最简单，零开销 | 多核下需要锁 |
| **队列** | ISR → 任务，生产者-消费者 | 天生线程安全，解耦 | 不适合大块数据 |
| **互斥锁** | 保护共享结构体 | 灵活，适合复杂数据 | 可能死锁，有开销 |

### 5. 是否需要加锁的判断标准

问自己三个问题：

1. **是否多核访问？** ESP32-S3 双核上不同核的任务才真正并行，同核任务会被调度器打断但不同时运行
2. **数据类型是否原子？** `uint8_t` `bool` 等在 ESP32 上是单次内存访问，天然安全；`uint32_t` `struct` 可能分多次读写
3. **写操作是否非原子？** `g_disp.key_count++` 是读-改-写三步，不是原子的

即使单核，如果**写操作中间被更高优先级任务抢占**，也可能读到不一致的数据。

---

## 总结

| 原则 | 说明 |
|------|------|
| **最小化持有时间** | 只包裹对共享资源的直接读写，耗时操作放在外面 |
| **避免 ISR 中加锁** | ISR 中只用队列传递数据 |
| **成对使用** | 用统一出口避免遗漏 `Give` |
| **按需使用** | 小数据类型且单核时，加锁可能多余；结构体或多核时必须有锁 |
| **优先用队列** | 数据流传递优先用队列，互斥锁保护的是"静止的共享状态" |
