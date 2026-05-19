# FreeRTOS API 参考（ESP-IDF 环境）

按类别组织，摘自学计划涉及的全部函数。每个函数标注参数类型、ESP-IDF 下的注意事项。

---

## 目录

- [任务管理](#任务管理)
- [队列](#队列)
- [信号量与互斥锁](#信号量与互斥锁)
- [事件组](#事件组)
- [任务通知](#任务通知)
- [软件定时器](#软件定时器)
- [工具宏](#工具宏)
- [调试辅助](#调试辅助)

---

## 任务管理

### `xTaskCreatePinnedToCore()`

```c
BaseType_t xTaskCreatePinnedToCore(
    TaskFunction_t       pvTaskCode,      // 任务函数指针 void func(void *pvParameters)
    const char * const   pcName,          // 任务名，调试用，最大 configMAX_TASK_NAME_LEN
    const uint32_t       usStackDepth,    // 栈大小，单位 bytes（ESP-IDF，非原生 FreeRTOS 的 words）
    void * const         pvParameters,    // 传给任务函数的参数指针
    UBaseType_t          uxPriority,      // 优先级，0~configMAX_PRIORITIES-1，数字越大优先级越高
    TaskHandle_t * const pvCreatedTask,   // 出参，接收任务句柄，不需要可传 NULL
    const BaseType_t     xCoreID          // 核心 ID: 0 (PRO_CPU), 1 (APP_CPU), tskNO_AFFINITY
);
```

- **返回**: `pdPASS` 或 `errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY`
- **注意**: 这是 ESP-IDF 扩展，原生 FreeRTOS 只有 `xTaskCreate()`
- **注意**: 栈大小不能用 `configMINIMAL_STACK_SIZE` 直接套，ESP32-S3 建议至少 2048 bytes 起步

### `vTaskDelay()`

```c
void vTaskDelay(const TickType_t xTicksToDelay);
```

- 使当前任务进入阻塞态，等待指定 tick 数
- 相对延时：从调用时刻开始计时
- 常用：`vTaskDelay(pdMS_TO_TICKS(1000))` — 延时 1 秒
- **注意**: 调度器 tick 默认 100Hz（10ms），`pdMS_TO_TICKS(9)` 会得到 0

### `vTaskDelayUntil()`

```c
void vTaskDelayUntil(
    TickType_t * const pxPreviousWakeTime,  // 上次唤醒时刻，首次需初始化为当前 tick
    const TickType_t   xTimeIncrement       // 周期 tick 数
);
```

- 绝对延时，用于固定周期执行
- 相比 `vTaskDelay()` 不会累积误差
- 用法：
```c
TickType_t xLastWakeTime = xTaskGetTickCount();
while (1) {
    // do work
    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(100));
}
```

### `xTaskGetTickCount()`

```c
TickType_t xTaskGetTickCount(void);
```

- 返回自调度器启动以来的 tick 数
- 配合 `vTaskDelayUntil()` 使用

---

## 队列

### `xQueueCreate()`

```c
QueueHandle_t xQueueCreate(
    UBaseType_t uxQueueLength,  // 队列可容纳的元素数量
    UBaseType_t uxItemSize      // 每个元素的大小（bytes）
);
```

- **返回**: 队列句柄，内存不足返回 NULL
- 示例：`xQueueCreate(10, sizeof(sensor_data_t))`

### `xQueueSend()`

```c
BaseType_t xQueueSend(
    QueueHandle_t xQueue,         // 队列句柄
    const void *  pvItemToQueue,  // 指向要发送的数据的指针
    TickType_t    xTicksToWait    // 队列满时的等待时间
);
```

- **返回**: `pdPASS` 或 `errQUEUE_FULL`
- **注意**: 在 ISR 中必须用 `xQueueSendFromISR()`
- 等同于 `xQueueGenericSend()`，将数据**拷贝**到队列（不传指针）

### `xQueueReceive()`

```c
BaseType_t xQueueReceive(
    QueueHandle_t xQueue,        // 队列句柄
    void *        pvBuffer,      // 接收缓冲区指针
    TickType_t    xTicksToWait   // 队列空时的等待时间
);
```

- **返回**: `pdPASS` 或 `errQUEUE_EMPTY`
- 数据从队列中移除（不是 peek）
- **注意**: 缓冲区大小必须 ≥ 队列创建时的 `uxItemSize`

### `xQueueSendFromISR()`

```c
BaseType_t xQueueSendFromISR(
    QueueHandle_t xQueue,
    const void *  pvItemToQueue,
    BaseType_t *  pxHigherPriorityTaskWoken  // 出参，pdTRUE 表示需要 context switch
);
```

- ISR 专用版本
- 如果 `*pxHigherPriorityTaskWoken == pdTRUE`，退出 ISR 后应执行 `portYIELD_FROM_ISR()`

---

## 信号量与互斥锁

### `xSemaphoreCreateBinary()`

```c
SemaphoreHandle_t xSemaphoreCreateBinary(void);
```

- 创建二值信号量，初始为 0（未给出）
- **返回**: 信号量句柄，内存不足返回 NULL

### `xSemaphoreCreateMutex()`

```c
SemaphoreHandle_t xSemaphoreCreateMutex(void);
```

- 创建互斥锁，初始为 1（可用）
- 支持优先级继承，防优先级反转
- **返回**: 信号量句柄，内存不足返回 NULL

### `xSemaphoreTake()`

```c
BaseType_t xSemaphoreTake(
    SemaphoreHandle_t xSemaphore,  // 信号量/互斥锁句柄
    TickType_t        xTicksToWait // 等待时间，portMAX_DELAY 表示永久等待
);
```

- **返回**: `pdPASS` 或 `pdFALSE`
- 对二值信号量：等待信号量变为可用（非 0）
- 对互斥锁：等待锁被释放

### `xSemaphoreGive()`

```c
BaseType_t xSemaphoreGive(SemaphoreHandle_t xSemaphore);
```

- 给出信号量/释放互斥锁
- **返回**: `pdPASS` 或 `pdFALSE`
- **注意**: 互斥锁必须在获取它的同一任务中释放

### `xSemaphoreGiveFromISR()`

```c
BaseType_t xSemaphoreGiveFromISR(
    SemaphoreHandle_t xSemaphore,
    BaseType_t *      pxHigherPriorityTaskWoken
);
```

- ISR 中给出信号量
- 典型按键中断用法：
```c
static BaseType_t xHigherPriorityTaskWoken = pdFALSE;
xSemaphoreGiveFromISR(xSemaphore, &xHigherPriorityTaskWoken);
portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
```

---

## 事件组

### `xEventGroupCreate()`

```c
EventGroupHandle_t xEventGroupCreate(void);
```

- 创建事件组
- **返回**: 事件组句柄，内存不足返回 NULL

### `xEventGroupSetBits()`

```c
EventBits_t xEventGroupSetBits(
    EventGroupHandle_t xEventGroup,  // 事件组句柄
    const EventBits_t  uxBitsToSet   // 要置 1 的事件位（多位可 OR）
);
```

- 在任务上下文中设置事件位
- ISR 中需用 `xEventGroupSetBitsFromISR()`

### `xEventGroupWaitBits()`

```c
EventBits_t xEventGroupWaitBits(
    EventGroupHandle_t xEventGroup,      // 事件组句柄
    const EventBits_t  uxBitsToWaitFor,  // 要等待的事件位
    const BaseType_t   xClearOnExit,     // pdTRUE: 满足条件后自动清除这些位
    const BaseType_t   xWaitForAllBits,  // pdTRUE: 等待所有位；pdFALSE: 任一即可
    TickType_t         xTicksToWait      // 超时
);
```

- **返回**: 满足条件时的事件组状态
- 示例：等待 KEY1_PRESS(0x01) 且 TEMP_READY(0x04)：
```c
xEventGroupWaitBits(eg, 0x01 | 0x04, pdTRUE, pdTRUE, pdMS_TO_TICKS(1000));
```

### `xEventGroupSetBitsFromISR()`

```c
BaseType_t xEventGroupSetBitsFromISR(
    EventGroupHandle_t xEventGroup,
    const EventBits_t  uxBitsToSet,
    BaseType_t *       pxHigherPriorityTaskWoken
);
```

- ISR 中设置事件位
- 实际通过定时器命令队列异步完成

---

## 任务通知

任务通知比二值信号量快约 45%，且省内存（每个任务已有现成的通知状态）。

### `vTaskNotifyGiveFromISR()`

```c
void vTaskNotifyGiveFromISR(
    TaskHandle_t xTaskToNotify,         // 目标任务句柄
    BaseType_t * pxHigherPriorityTaskWoken
);
```

- ISR 中发送通知，递增目标任务的通知值
- 轻量级替代 `xSemaphoreGiveFromISR()`

### `ulTaskNotifyTake()`

```c
uint32_t ulTaskNotifyTake(
    BaseType_t xClearCountOnExit,  // pdTRUE: 获取后清零；pdFALSE: 减 1
    TickType_t xTicksToWait
);
```

- 等待并消费通知
- 返回通知值（清零前或减 1 前）
- 典型按键模式：
```c
// ISR
vTaskNotifyGiveFromISR(xHandle, &xHigherPriorityTaskWoken);

// Task
ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
```

### `xTaskNotifyWait()`

```c
BaseType_t xTaskNotifyWait(
    uint32_t  ulBitsToClearOnEntry,  // 进入时清除的位
    uint32_t  ulBitsToClearOnExit,   // 退出时清除的位
    uint32_t * pulNotificationValue, // 出参，通知值
    TickType_t xTicksToWait
);
```

- 更灵活的通知等待（可传值、可操作特定位）
- `ulBitsToClearOnEntry`：等待前先将通知值的指定位清零
- `ulBitsToClearOnExit`：退出时清零通知值的指定位

### `xTaskNotify()`

```c
BaseType_t xTaskNotify(
    TaskHandle_t xTaskToNotify,
    uint32_t     ulValue,          // 通知值
    eNotifyAction eAction          // 动作：eNoAction/eSetBits/eIncrement/eSetValueWithoutOverwrite/eSetValueWithOverwrite
);
```

- 任务上下文中发送通知
- `eAction = eIncrement` 等效于 `vTaskNotifyGiveFromISR()`

---

## 软件定时器

### `xTimerCreate()`

```c
TimerHandle_t xTimerCreate(
    const char * const pcName,           // 定时器名（调试用）
    const TickType_t   xTimerPeriod,     // 周期 tick 数
    const UBaseType_t  uxAutoReload,     // pdTRUE: 自动重载；pdFALSE: 一次性
    void * const       pvTimerID,        // 定时器 ID（可传结构体指针）
    TimerCallbackFunction_t pxCallbackFunction  // 回调函数 void func(TimerHandle_t xTimer)
);
```

- **返回**: 定时器句柄，内存不足返回 NULL
- **注意**: 回调运行在定时器服务任务（`tmr Svc`）上下文中，**禁止阻塞**

### `xTimerStart()`

```c
BaseType_t xTimerStart(
    TimerHandle_t xTimer,
    TickType_t    xTicksToWait  // 等待命令队列的 timeout
);
```

- 启动定时器（从调用到首次触发等待一个周期）
- **返回**: `pdPASS` 或 `pdFALSE`

### `xTimerStop()`

```c
BaseType_t xTimerStop(
    TimerHandle_t xTimer,
    TickType_t    xTicksToWait
);
```

- 停止定时器

### `xTimerReset()`

```c
BaseType_t xTimerReset(
    TimerHandle_t xTimer,
    TickType_t    xTicksToWait
);
```

- 复位定时器（重新从当前时刻开始计时）

### `xTimerChangePeriod()`

```c
BaseType_t xTimerChangePeriod(
    TimerHandle_t xTimer,
    TickType_t    xNewPeriod,
    TickType_t    xTicksToWait
);
```

- 动态修改定时器周期

---

## 工具宏

### `pdMS_TO_TICKS()`

```c
TickType_t pdMS_TO_TICKS(uint32_t xTimeInMs);
```

- 将毫秒转换为 tick 数
- **关键陷阱**: `pdMS_TO_TICKS(9)` 在 100Hz tick 下为 0！确保值 ≥ `pdMS_TO_TICKS(10)`

### `portYIELD_FROM_ISR()`

```c
void portYIELD_FROM_ISR(BaseType_t xHigherPriorityTaskWoken);
```

- ISR 末尾调用，如果 `xHigherPriorityTaskWoken == pdTRUE`，触发上下文切换
- 通常在 `FromISR` API 调用之后

### `portMAX_DELAY`

```c
const TickType_t portMAX_DELAY;
```

- 阻塞等待的最大值，表示"永久等待"
- 仅在中断使能时有效

---

## 调试辅助

### `uxTaskGetStackHighWaterMark()`

```c
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t xTask);
```

- 返回任务栈剩余的最小空间（bytes，ESP-IDF 单位）
- 传 NULL 查自己
- 调试用，监控栈溢出风险

### `vTaskList()`

```c
void vTaskList(char *pcWriteBuffer);
```

- 将当前所有任务的状态信息写入缓冲区（人类可读表格）
- 需要 `configUSE_TRACE_FACILITY` 和 `configUSE_STATS_FORMATTING_FUNCTIONS` 使能
- 缓冲区需足够大

### `vTaskGetRunTimeStats()`

```c
void vTaskGetRunTimeStats(char *pcWriteBuffer);
```

- 写入各任务占用 CPU 时间的百分比
- 需要 `configGENERATE_RUN_TIME_STATS` 使能 + 提供时基

### `esp_task_wdt_add()`

```c
esp_err_t esp_task_wdt_add(TaskHandle_t task);
```

- ESP-IDF 扩展（非原生 FreeRTOS）
- 将任务加入 TWDT（任务看门狗）监控
- 高优先级任务死循环会触发 TWDT 复位

### `heap_caps_malloc()`

```c
void *heap_caps_malloc(size_t size, uint32_t caps);
```

- ESP-IDF 扩展，从指定内存分配
- 大缓冲区用 `MALLOC_CAP_SPIRAM` 分配到 PSRAM
- 非原生 FreeRTOS，但实际开发中与 FreeRTOS 任务配合使用

---

## ESP-IDF 特殊说明汇总

| 差异项 | 说明 |
|--------|------|
| 栈大小单位 | **bytes**（原生 FreeRTOS 是 words = 4 bytes） |
| `vTaskStartScheduler()` | **禁止调用**，ESP-IDF 自动启动调度器 |
| 临界区 | 用 `portMUX_TYPE` 自旋锁（`portENTER_CRITICAL`/`portEXIT_CRITICAL`），非全局关中断 |
| `pdMS_TO_TICKS(9)` | 在 100Hz (10ms/tick) 下得到 **0**，导致 `vTaskDelay(0)` 空转 |
| 看门狗 | 高优先级任务死循环会触发 TWDT |
| 核绑定 | 必须用 `xTaskCreatePinnedToCore()` 指定运行核心 |
| 任务回收 | 删除任务后需让空闲任务运行，才能回收内存 |
| BLE 协议栈 | ESP32-S3 仅支持 BLE（NimBLE），不支持经典蓝牙 |
