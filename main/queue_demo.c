/**
 * @file    queue_demo.c
 * @brief   FreeRTOS 队列（Queue）创建与使用演示
 *
 * 本文件独立于项目主逻辑，旨在演示队列的完整用法。
 * 如果想实际运行，在 main.c 的 app_main() 末尾添加：
 *     queue_demo_main();
 * 并在 main/CMakeLists.txt 的 src_dirs 中确保本文件参与编译。
 *
 * 内容导航：
 *   1. 基础队列：单数据收发 ────────── queue_demo_basic()
 *   2. 结构体队列：发送复合数据 ─────── queue_demo_struct()
 *   3. 中断中发送队列 ──────────────── queue_demo_isr()
 *   4. 多接收者竞争 ───────────────── queue_demo_multi_recv()
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

/* ================================================================
 *  基础概念（必读）
 * ================================================================
 *   队列（Queue）是 FreeRTOS 中任务间通信的主要方式：
 *
 *   生产者（发送方）        →    [ 队列 ]    →   消费者（接收方）
 *   xQueueSend()                  buffer          xQueueReceive()
 *
 *   ● 队列是一个 FIFO（先进先出）的环形缓冲区
 *   ● 每个队列有固定的 长度 和 每项大小（创建时指定）
 *   ● 发送：数据会被 拷贝 到队列内部（不是传指针）
 *   ● 接收：数据会被 拷贝 出队列，并在队列中移除
 *   ● 队列空时接收 → 任务阻塞（可设超时）
 *   ● 队列满时发送 → 任务阻塞（可设超时）
 */

/* ================================================================
 *  演示 1：基础队列 — 整型数据的发送与接收
 * ================================================================ */

/* 队列句柄：所有队列操作都通过这个句柄进行 */
/* 句柄本质上是一个指向队列控制块的指针 */
static QueueHandle_t xQueue_int = NULL;

/* 生产者任务：每隔一段时间向队列发送一个递增的整数 */
static void vSenderTask(void *pvParameters)
{
    int32_t val = 0;              // 要发送的数据
    const char *pcTaskName = (const char *)pvParameters;  // 任务名（传参进来的）

    while (1)
    {
        val++;

        /**
         * xQueueSend() 等价于 xQueueSendToBack()
         *
         *   函数原型（简化）：
         *     BaseType_t xQueueSend(QueueHandle_t xQueue,
         *                           const void *pvItemToQueue,
         *                           TickType_t xTicksToWait);
         *
         *   参数：
         *     xQueue        ─ 队列句柄
         *     pvItemToQueue ─ 指向要发送的数据（注意：传地址，不是传值）
         *     xTicksToWait  ─ 队列满时的等待时间
         *                      pdMS_TO_TICKS(100) = 阻塞等 100ms
         *                      0                 = 队列满则直接返回，不等待
         *                      portMAX_DELAY     = 一直等到有空间为止（慎用！）
         *
         *   返回值：
         *     pdTRUE  ─ 发送成功
         *     pdFALSE ─ 发送失败（队列满且超时）
         *
         *   ★ 重要：发送的是数据的 副本，不是指针！
         *     所以 val 在发送后可以立即修改，不影响队列中的内容。
         */
        BaseType_t ret = xQueueSend(xQueue_int, &val, pdMS_TO_TICKS(100));

        if (ret == pdTRUE)
            printf("[%s] 发送: %ld\n", pcTaskName, (long)val);
        else
            printf("[%s] 发送失败（队列满）\n", pcTaskName);

        vTaskDelay(pdMS_TO_TICKS(500));  // 每 500ms 发一次
    }
}

/* 消费者任务：从队列接收数据并打印 */
static void vReceiverTask(void *pvParameters)
{
    int32_t recv_val;             // 接收缓冲区：大小必须 ≥ 队列的每项大小
    const char *pcTaskName = (const char *)pvParameters;

    while (1)
    {
        /**
         * xQueueReceive() — 从队列中取数据
         *
         *   函数原型（简化）：
         *     BaseType_t xQueueReceive(QueueHandle_t xQueue,
         *                              void *pvBuffer,
         *                              TickType_t xTicksToWait);
         *
         *   参数：
         *     xQueue       ─ 队列句柄
         *     pvBuffer     ─ 接收缓冲区地址（数据会被拷贝到这里）
         *     xTicksToWait ─ 队列空时的等待时间
         *
         *   返回值：
         *     pdTRUE  ─ 成功收到数据
         *     pdFALSE ─ 超时仍未收到数据
         *
         *   ★ 注意：接收后数据从队列中移除（一次性消费）
         *     如果想查看但不移除，用 xQueuePeek()
         */
        BaseType_t ret = xQueueReceive(xQueue_int, &recv_val, portMAX_DELAY);

        if (ret == pdTRUE)
        {
            printf("[%s] 收到: %ld\n", pcTaskName, (long)recv_val);
        }
    }
}

static void queue_demo_basic(void)
{
    printf("\n========== 演示 1：基础队列（整型收发）==========\n");

    /**
     * xQueueCreate() — 创建队列
     *
     *   函数原型：
     *     QueueHandle_t xQueueCreate(UBaseType_t uxQueueLength,
     *                                 UBaseType_t uxItemSize);
     *
     *   参数：
     *     uxQueueLength ─ 队列可容纳的最大项数（深度）
     *     uxItemSize    ─ 每项数据的字节大小
     *
     *   返回值：
     *     NULL    ─ 创建失败（通常是内存不足）
     *     非 NULL  ─ 队列句柄
     *
     *   示例：xQueueCreate(5, sizeof(int32_t))
     *     → 创建一个能存储 5 个 int32_t 的队列
     *     → 内部会分配 5 × 4 = 20 字节的缓冲区
     *
     *   ★ 本质：队列创建 = 分配 FIFO 缓冲区 + 控制块
     *     创建后立即可用，无需额外初始化。
     */
    xQueue_int = xQueueCreate(5, sizeof(int32_t));

    if (xQueue_int == NULL)
    {
        printf("队列创建失败！（堆内存不足）\n");
        return;
    }
    printf("队列创建成功：最大 5 项，每项 %d 字节\n", (int)sizeof(int32_t));

    /* 创建 2 个发送任务 + 1 个接收任务 */
    xTaskCreatePinnedToCore(vSenderTask,   "sender1", 2048, (void *)"sender1", 5, NULL, 0);
    xTaskCreatePinnedToCore(vSenderTask,   "sender2", 2048, (void *)"sender2", 5, NULL, 0);
    xTaskCreatePinnedToCore(vReceiverTask, "receiver",2048, (void *)"receiver", 3, NULL, 1);
}

/* ================================================================
 *  演示 2：结构体队列 — 发送复合数据（传感器数据）
 * ================================================================
 *  实际项目中，队列发送的往往不是单个整数，而是包含多个字段的结构体。
 *  比如发送"温度+湿度+时间戳"打包给显示任务处理。
 */

/* 定义一个传感器数据结构体 */
typedef struct {
    float       temperature;    // 温度（°C）
    float       humidity;       // 湿度（%）
    uint32_t    timestamp;      // 采集时间戳（可读 TickCount）
    uint8_t     sensor_id;      // 传感器编号
} SensorData_t;

static QueueHandle_t xQueue_sensor = NULL;

static void vSensorTask(void *pvParameters)
{
    SensorData_t data;
    uint32_t tick = 0;

    (void)pvParameters;

    while (1)
    {
        /* 模拟采集传感器数据 */
        tick++;
        data.temperature = 25.0f + (float)(rand() % 30) / 10.0f;  // 25.0~27.9°C
        data.humidity    = 60.0f + (float)(rand() % 50) / 10.0f;  // 60.0~64.9%
        data.timestamp   = xTaskGetTickCount();                    // 获取当前 Tick
        data.sensor_id   = 1;

        /**
         * ★ 发送结构体：直接传结构体变量的地址
         *   队列内部会按 sizeof(SensorData_t) 拷贝一份
         */
        if (xQueueSend(xQueue_sensor, &data, pdMS_TO_TICKS(50)) == pdTRUE)
        {
            printf("[传感器] 采集完成: %.1f°C, %.1f%%, tick=%lu\n",
                   data.temperature, data.humidity, data.timestamp);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));  // 每秒采集一次
    }
}

static void vDisplayTask(void *pvParameters)
{
    SensorData_t data;

    (void)pvParameters;

    while (1)
    {
        /**
         * portMAX_DELAY：永久等待
         *   队列为空时任务进入阻塞态，不占 CPU
         *   一旦有数据到达，自动唤醒
         *
         *   ★  这正是队列的核心优势：
         *      生产者任务准备好数据 → 发队列
         *      消费者任务阻塞等待 → 数据来→自动唤醒→处理→继续阻塞
         *      全程无需轮询，不浪费 CPU
         */
        xQueueReceive(xQueue_sensor, &data, portMAX_DELAY);

        printf("[显示器] 更新显示: ID=%d, %d.%d°C, %d.%d%%\n",
               data.sensor_id,
               (int)data.temperature, (int)(data.temperature * 10) % 10,
               (int)data.humidity,    (int)(data.humidity * 10) % 10);
    }
}

static void queue_demo_struct(void)
{
    printf("\n========== 演示 2：结构体队列（传感器数据）==========\n");

    /**
     * 创建存储 SensorData_t 的队列
     * 最大 3 项，每项 sizeof(SensorData_t) 字节
     *
     * ★ 为什么队列长度是 3？
     *   传感器每秒采集一次，显示器处理速度跟上时，
     *   队列基本不会积压。设 3 只是缓冲峰值。
     *
     *   ★ 如果队列填满会怎样？
     *   xQueueSend() 阻塞等待 → 直到有空间或超时
     *   这形成了"背压"（backpressure）：消费者慢了生产者也会慢
     */
    xQueue_sensor = xQueueCreate(3, sizeof(SensorData_t));
    if (xQueue_sensor == NULL)
    {
        printf("传感器队列创建失败！\n");
        return;
    }
    printf("传感器队列创建成功：最大 3 项，每项 %d 字节\n", (int)sizeof(SensorData_t));

    xTaskCreatePinnedToCore(vSensorTask,  "sensor",  2048, NULL, 4, NULL, 0);
    xTaskCreatePinnedToCore(vDisplayTask, "display", 2048, NULL, 3, NULL, 1);
}

/* ================================================================
 *  演示 3：在中断服务函数（ISR）中使用队列
 * ================================================================
 *  ISR 中和任务中发队列的 API 不同：
 *     任务中 ←→ xQueueSend()
 *     ISR   ←→ xQueueSendFromISR()
 *
 *  ★ 为什么需要 FromISR 版本？
 *    1. ISR 中不能阻塞（不能用 vTaskDelay、不能设超时等待）
 *    2. FromISR 版本会用一个参数（pxHigherPriorityTaskWoken）
 *       告诉内核：我唤醒了一个高优先级任务，请在 ISR 结束后
 *       立即切换（而不等到下次 Tick 中断）
 *
 *  典型场景：按键中断 → 发送键值到队列 → 任务处理
 */

/* 保存队列句柄的全局变量（方便 ISR 访问） */
static QueueHandle_t xQueue_key = NULL;

/* ISR 中可用的发送宏：打包两个值到一个 32 位中 */
#define KEY_CODE(key_id, action)  (((key_id) << 8) | (action))
#define KEY_ID(code)              ((code) >> 8)
#define KEY_ACTION(code)          ((code) & 0xFF)

/* 模拟按键中断服务函数 */
static void IRAM_ATTR sim_key_isr(uint8_t key_id, uint8_t action)
{
    /**
     *   ★ ISR 中发队列的三个关键点 ★
     *
     *   1. 必须用 xQueueSendFromISR()，不能用 xQueueSend()
     *      因为 xQueueSend() 可能会阻塞等待，而 ISR 不能阻塞！
     *
     *   2. pxHigherPriorityTaskWoken 参数
     *      传入一个 BaseType_t 变量的地址，初始化为 pdFALSE
     *      如果 FromISR 唤醒了一个比当前任务优先级更高的任务，
     *      这个变量会被设为 pdTRUE
     *      结束时需要做上下文切换（portYIELD_FROM_ISR()）
     *
     *   3. ISR 中不能设等待时间（第三个参数不传）
     *      队列满时直接返回 errQUEUE_FULL，不能阻塞等空位
     */

    uint32_t key_code = KEY_CODE(key_id, action);
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    /* 向队列发送键值 */
    xQueueSendFromISR(xQueue_key, &key_code, &xHigherPriorityTaskWoken);

    /**
     * 如果 xQueueSendFromISR() 唤醒了一个高优先级任务，
     * 这里需要立即切换上下文，让被唤醒的任务运行。
     *
     * portYIELD_FROM_ISR() 会检查参数，只在需要时才切换，
     * 避免不必要的上下文切换开销。
     */
    if (xHigherPriorityTaskWoken)
    {
        portYIELD_FROM_ISR();
    }
}

static void vKeyHandlerTask(void *pvParameters)
{
    uint32_t key_code;
    uint8_t  key_id, action;

    (void)pvParameters;

    while (1)
    {
        /* 阻塞等待按键事件（队列空时自动休眠） */
        xQueueReceive(xQueue_key, &key_code, portMAX_DELAY);

        key_id  = KEY_ID(key_code);
        action  = KEY_ACTION(key_code);

        printf("[按键处理] K%d ", key_id);
        switch (action)
        {
            case 0:  printf("按下\n");     break;
            case 1:  printf("释放\n");     break;
            case 2:  printf("长按\n");     break;
            default: printf("未知动作\n"); break;
        }
    }
}

/* 模拟按键产生的任务（代替真实中断） */
static void vSimulateButtonTask(void *pvParameters)
{
    (void)pvParameters;

    vTaskDelay(pdMS_TO_TICKS(2000));

    /* 模拟：K1 按下 */
    printf("\n[模拟] K1 按下\n");
    sim_key_isr(1, 0);
    vTaskDelay(pdMS_TO_TICKS(100));

    /* 模拟：K1 释放 */
    printf("[模拟] K1 释放\n");
    sim_key_isr(1, 1);
    vTaskDelay(pdMS_TO_TICKS(3000));

    /* 模拟：K2 长按 */
    printf("[模拟] K2 长按\n");
    sim_key_isr(2, 2);

    vTaskDelay(pdMS_TO_TICKS(1000));
}

static void queue_demo_isr(void)
{
    printf("\n========== 演示 3：中断中发送队列 ==========\n");

    xQueue_key = xQueueCreate(10, sizeof(uint32_t));
    if (xQueue_key == NULL)
    {
        printf("按键队列创建失败！\n");
        return;
    }
    printf("按键队列创建成功：最大 10 项，每项 %d 字节\n", (int)sizeof(uint32_t));
    printf("  KEY_CODE(key_id, action) 打包格式：\n");
    printf("  高 24 位 = key_id，低 8 位 = action\n");

    xTaskCreatePinnedToCore(vKeyHandlerTask,     "key_handler",  2048, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(vSimulateButtonTask, "sim_button",   2048, NULL, 3, NULL, 0);
}

/* ================================================================
 *  演示 4：多接收者与队列集（Queue Set）
 * ================================================================
 *  多个任务可以接收同一个队列，但每条消息只能被一个任务取走
 *  （谁先抢到算谁的，类似"消费者竞争"模式）
 *
 *  如果想同时等待多个队列/信号量，可以使用队列集（Queue Set）
 *  这里仅演示基本的"多接收者竞争"场景，帮助理解队列的特性。
 */

static QueueHandle_t xQueue_cmd = NULL;

static void vWorkerTask(void *pvParameters)
{
    int cmd;
    int id = (int)pvParameters;  // 工作者 ID

    while (1)
    {
        /**
         * ★ 多个任务同时 xQueueReceive() 同一个队列时：
         *   - 每条消息只会被一个任务接收
         *   - FreeRTOS 保证：消息不会重复分发
         *   - 哪个任务跑得快/优先级高，就更可能抢到
         */
        xQueueReceive(xQueue_cmd, &cmd, portMAX_DELAY);
        printf("[工人 %d] 执行命令: %d\n", id, cmd);
        vTaskDelay(pdMS_TO_TICKS(100));  // 模拟处理时间
    }
}

static void vCommanderTask(void *pvParameters)
{
    int cmd = 0;

    (void)pvParameters;

    while (1)
    {
        cmd++;
        printf("\n[指挥官] 下发命令: %d\n", cmd);

        for (int i = 0; i < 4; i++)
        {
            xQueueSend(xQueue_cmd, &cmd, pdMS_TO_TICKS(10));
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

static void queue_demo_multi_recv(void)
{
    printf("\n========== 演示 4：多接收者竞争同一队列 ==========\n");
    printf("一个命令队列，3 个工人抢活干\n");
    printf("  ★ 每条消息只能被一个工人接收\n");
    printf("  ★ 不会重复分发\n\n");

    xQueue_cmd = xQueueCreate(10, sizeof(int));
    if (xQueue_cmd == NULL) {
        printf("命令队列创建失败！\n");
        return;
    }

    xTaskCreatePinnedToCore(vCommanderTask, "commander", 2048, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(vWorkerTask, "worker1", 2048, (void *)1, 3, NULL, 0);
    xTaskCreatePinnedToCore(vWorkerTask, "worker2", 2048, (void *)2, 3, NULL, 0);
    xTaskCreatePinnedToCore(vWorkerTask, "worker3", 2048, (void *)3, 3, NULL, 0);
}

/* ================================================================
 *  入口函数：在 main.c 中调用此函数运行全部演示
 * ================================================================
 *
 *  用法：
 *  在 main.c 顶部添加：
 *     extern void queue_demo_main(void);
 *  在 app_main() 末尾添加：
 *     queue_demo_main();
 */
void queue_demo_main(void)
{
    printf("\n");
    printf("========================================\n");
    printf("  FreeRTOS 队列（Queue）演示\n");
    printf("========================================\n");
    printf("\n");
    printf("队列是任务间通信的桥梁：\n");
    printf("  生产者（发） → [队列 FIFO] → 消费者（收）\n");
    printf("  ★ 数据<拷贝>进队列，不是传指针\n");
    printf("  ★ 队列空 → 接收者阻塞；队列满 → 发送者阻塞\n");
    printf("  ★ ISR 中必须用 xQueueSendFromISR()\n");
    printf("  ★ 多接收者竞争时，每条消息只到一个接收者\n");

    /* 依次运行每个演示 */
    queue_demo_basic();
    vTaskDelay(pdMS_TO_TICKS(3000));  // 让演示 1 跑一会儿

    // queue_demo_struct();
    // vTaskDelay(pdMS_TO_TICKS(5000));

    // queue_demo_isr();
    // vTaskDelay(pdMS_TO_TICKS(6000));

    // queue_demo_multi_recv();
    // vTaskDelay(pdMS_TO_TICKS(8000));

    /**
     * ★ 为什么上面部分演示被注释了？
     *
     * 因为每个演示都会创建自己的任务，多个演示同时运行
     * 会互相干扰打印输出。建议逐个运行：
     *   1. 先运行 queue_demo_basic（默认已开）
     *   2. 注释掉 basic，取消注释 struct → 编译 → 看效果
     *   3. 以此类推，逐个体验
     *
     * 或者直接修改 main.c，在 app_main() 的 while(1) 前
     * 调用你想看的演示函数。
     */
}

/* ================================================================
 *  附录：常用队列 API 速查
 * ================================================================
 *
 *   创建与删除
 *     xQueueCreate(长度, 每项大小)         创建队列
 *     xQueueReset(队列句柄)                清空队列
 *     vQueueDelete(队列句柄)               删除队列
 *
 *   发送
 *     xQueueSend(队列, &数据, 超时)         发送到队尾（同 ToBack）
 *     xQueueSendToFront(队列, &数据, 超时)  发送到队首
 *     xQueueSendToBack(队列, &数据, 超时)   发送到队尾
 *     xQueueOverwrite(队列, &数据)          覆盖（只用于队列长度=1时）
 *
 *   接收
 *     xQueueReceive(队列, &缓冲区, 超时)    接收并移除
 *     xQueuePeek(队列, &缓冲区, 超时)       接收但不移除
 *
 *   中断安全（ISR 版本）
 *     xQueueSendFromISR(队列, &数据, &woken)
 *     xQueueReceiveFromISR(队列, &缓冲区, &woken)
 *
 *   查询
 *     uxQueueMessagesWaiting(队列)          队列中的消息数
 *     uxQueueSpacesAvailable(队列)          队列剩余空间
 *     uxQueueMessagesWaitingFromISR(队列)   ISR 中查询消息数
 *
 *   队列集（同时等待多个队列/信号量）
 *     xQueueCreateSet(总长度)              创建队列集
 *     xQueueAddToSet(队列, 队列集)         添加队列到集合
 *     xQueueSelectFromSet(队列集, 超时)    等待集合中任一队列有数据
 */
