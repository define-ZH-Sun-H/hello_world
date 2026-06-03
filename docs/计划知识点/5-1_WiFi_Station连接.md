# 5-1 WiFi Station 连接

> ESP32-S3 双核内置 2.4GHz WiFi（802.11 b/g/n），支持 Station、AP、Station+AP 混合模式。
> 本文聚焦 **Station 模式** — 连接路由器上网。

---

## 1. WiFi 架构概览

ESP-IDF 的 WiFi 栈分三层：

```
┌─────────────────────────────────────────────┐
│               应用层（你的代码）               │
│  app_main → 初始化 WiFi → 注册事件 → 连接   │
├─────────────────────────────────────────────┤
│              esp_netif 抽象层                │
│  统一管理网络接口（WiFi Station / AP / Eth） │
├─────────────────────────────────────────────┤
│              WiFi 驱动层（底层）              │
│  管理 MAC 层、扫描、连接、功率、省电等       │
├─────────────────────────────────────────────┤
│              物理层（硬件）                   │
│  ESP32-S3 内置 2.4GHz 射频 + 基带           │
└─────────────────────────────────────────────┘
```

### 各层职责

| 层 | 你直接打交道的 API | 做什么 |
|---|-------------------|--------|
| 应用层 | `app_main()` 中的代码 | 注册事件处理器、决定什么时候连接/断连 |
| `esp_netif` | `esp_netif_create_default_wifi_sta()` | 创建网络接口、关联 IP 协议栈（LwIP） |
| WiFi 驱动 | `esp_wifi_start()` / `esp_wifi_connect()` | 底层扫描、认证、关联 |
| 事件循环 | `esp_event_handler_register()` | 所有 WiFi/IP 状态变化通过事件通知 |

**核心思路：** 你不需要控制 WiFi 驱动的每一个细节，而是注册事件处理器，在状态变化时做出响应。

---

## 2. Station 模式的完整工作流程

```
你的代码                   WiFi 驱动                  路由器
  │                          │                        │
  │  ① esp_netif_init()      │                        │
  │  ② esp_event_loop_create()│                       │
  │  ③ esp_netif_create_     │                        │
  │     default_wifi_sta()    │                        │
  │  ④ wifi_init_config_t    │                        │
  │     = WIFI_INIT_CONFIG_DEFAULT()                  │
  │  ⑤ esp_wifi_init(&cfg)   │                        │
  │  ⑥ esp_wifi_set_mode(WIFI_MODE_STA)               │
  │  ⑦ esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg)   │
  │  ⑧ esp_wifi_start() ────→│ 启动射频               │
  │                          │ 扫描信道               │
  │ ← WIFI_EVENT_STA_START   │                        │
  │  ⑨ esp_wifi_connect() ──→│───────── 连接请求 ────→│
  │                          │  认证/关联              │
  │ ← WIFI_EVENT_STA_CONNECTED│←────── 关联成功 ───── │
  │                          │  DHCP 开始              │
  │ ← IP_EVENT_STA_GOT_IP    │←────── IP 分配 ────── │
  │  ⑩ 连接完成，开始通信     │                        │
```

**几个关键信号：**

| 事件 | 含义 | 接下来做什么 |
|------|------|-------------|
| `WIFI_EVENT_STA_START` | WiFi 驱动已启动，但还没连接 | 调用 `esp_wifi_connect()` |
| `WIFI_EVENT_STA_CONNECTED` | 已连接到路由器（MAC 层关联成功） | 等 DHCP 获取 IP（什么也不用做） |
| `IP_EVENT_STA_GOT_IP` | **拿到了 IP 地址** | 可以开始 TCP/UDP/MQTT 通信了 |
| `WIFI_EVENT_STA_DISCONNECTED` | 断开了（原因可能是信号弱、路由器重启等） | 调用 `esp_wifi_connect()` 重连 |

**最重要的理解：** `WIFI_EVENT_STA_CONNECTED` 不等于"能上网了"。它只是说"你的板子和路由器握手成功了"，但可能还没 IP。真正能通信的标志是 `IP_EVENT_STA_GOT_IP`。

---

## 3. 核心数据结构

### 3.1 wifi_config_t — 要连哪个路由器

```c
wifi_config_t wifi_config = {
    .sta = {
        .ssid = "MyHomeWiFi",         // 路由器名字
        .password = "12345678",       // 密码
        .threshold.authmode = WIFI_AUTH_WPA2_PSK,  // 最低接受的安全等级
    },
};
```

| 字段 | 说明 | 注意 |
|------|------|------|
| `ssid` | WiFi 名称，最长 32 字节 | 区分大小写 |
| `password` | WiFi 密码，最长 64 字节 | 没有密码就留空字符串 `""` |
| `threshold.authmode` | 最低接受的安全等级 | `WIFI_AUTH_OPEN` / `WIFI_AUTH_WPA_WPA2_PSK` 等 |
| `bssid` | 指定路由器的 MAC（可选） | 多个路由器同名时有用，通常不管 |

### 3.2 esp_netif_config_t — 网络接口配置

网络接口配置结构体，定义了"这张网卡连哪个协议栈、用什么驱动"。但 **初学者不需要手动构造它**，直接用下面的函数即可：

---

#### 核心函数：`esp_netif_create_default_wifi_sta()`

**函数签名：**
```c
esp_netif_t *esp_netif_create_default_wifi_sta(void);
```

| 项目 | 说明 |
|------|------|
| **参数** | 无（`void`） |
| **返回值** | `esp_netif_t *` — 指向网络接口对象的指针 |
|  | 失败返回 `NULL`（通常不会失败） |
| **头文件** | `esp_netif.h` |

这个函数**不需要任何参数**，所有配置都用默认值。它是整个 WiFi 编程中最常用的初始化函数之一，负责把 ESP32 的 WiFi 硬件变成一个能用的网络接口。

> 如果是初学者，记住**只需要调用这一行就够了**，不需要碰下面的结构体。

---

#### 背后的结构体：`esp_netif_config_t`

如果你以后需要自定义网络接口（例如同时开 STA + AP，或者自定义协议栈参数），才会用到这个结构体：

```c
typedef struct esp_netif_config {
    esp_netif_t **base;                       // 已有 netif 对象（通常填 NULL 新建）
    const esp_netif_netstack_config_t *stack; // 协议栈配置（LwIP）
    esp_netif_driver_base_t *driver;          // 网卡驱动句柄
} esp_netif_config_t;
```

| 字段 | 说明 | 初学者要不要管 |
|------|------|---------------|
| `base` | 指向已有 netif 的指针，`NULL` 表示新建 | ❌ 不用管 |
| `stack` | TCP/IP 协议栈配置，决定走 LwIP 哪种模式 | ❌ 默认的 `ESP_NETIF_NETSTACK_DEFAULT_WIFI_STA` 就够了 |
| `driver` | 网卡驱动句柄，绑定到具体的 WiFi 硬件 | ❌ 不用管 |

**不需要记这个结构体。** 记住一句话：`esp_netif_create_default_wifi_sta()` 帮你搞定全部。

---

#### 它帮你做了三件事

这个函数是**从"WiFi 硬件通电"到"可以写网络代码"的关键桥梁**，做了三件事：

#### ① 创建 `esp_netif` 对象 — 注册一张网卡

```c
/* 系统原来不知道你有 WiFi，这行代码告诉它："我有一个网卡叫 STA" */
```

**这是什么意思：**
ESP32 芯片集成了 WiFi 硬件，但系统（FreeRTOS 框架）不知道它的存在。这行代码在系统里注册一个"网络接口"对象，相当于告诉操作系统：

> "报告，我有一张无线网卡，名字叫 wifi_sta，可以用了。"

**类比：** 你给电脑插上一张新的无线网卡。光插上还不够，得在"网络和共享中心"里看到这张网卡的图标，系统才算认了它。这一步就是在系统里注册那个图标。

**如果少了这一步：** WiFi 驱动不知道自己归谁管，后续所有操作都没有落脚点。

---

#### ② 关联 LwIP 协议栈 — 给网卡装 TCP/IP 驱动

```c
/* WiFi 硬件收到的是电磁波比特流，LwIP 帮你翻译成 TCP/IP 数据包 */
```

**这是什么意思：**
WiFi 硬件收发的是原始无线电信号（一堆 0 和 1）。但你在代码里想用的是：

```c
send(socket, "hello", 5, 0);  /* 发送一段数据 */
recv(socket, buf, len, 0);    /* 接收一段数据 */
```

这两件事之间隔着一整个 **TCP/IP 协议栈**——负责把数据包拆分、寻址、组装的软件层。ESP32 内置的轻量协议栈叫 **LwIP**（Lightweight IP）。

```
你写的 send() / recv()
       ↓
LwIP 协议栈（拆包、拼包、校验）
       ↓
WiFi 驱动程序（把数据变成无线电波发出去）
       ↓
电磁波 → 路由器
```

这一步就是把 WiFi 驱动收到的原始比特流"接上" LwIP，让它们能互相传输数据。

**类比：** 电脑上装好网卡驱动后，还得勾上"Internet 协议版本 4 (TCP/IPv4)"才能真正上网。这一步就是打上那个勾。

**如果少了这一步：** WiFi 能连上路由器（物理层通），但收到的数据没有"翻译官"，你的程序收不到任何数据。

---

#### ③ 注册 DHCP 客户端 — 自动向路由器要 IP 地址

```c
/* 连上 WiFi 后自动喊一声："路由器，给我个 IP！" */
```

**这是什么意思：**
一个设备要上网，必须有一个唯一的 **IP 地址**——相当于互联网上的门牌号。谁给你分配门牌号？你家里的路由器（扮演 DHCP 服务器的角色）。

这一步注册了一个 DHCP 客户端，它会：

```
ESP32 连上路由器 WiFi
  → 自动发请求："我来了，给我一个可用的 IP"
  → 路由器响应："你用 192.168.1.105"
  → ESP32 收下这个 IP，之后所有数据都标上这个地址
```

**全过程发生在后台，不需要你写一行代码。**

**类比：** 你搬进一栋公寓。你得先去物业登记领一个门牌号（比如 305 室），别人才能给你寄快递。DHCP 就是自动去物业登记拿号的过程。

**注意区分 WiFi 连接和拿到 IP：**

```
WiFi 连上了（ESP32 和路由器之间的无线电通了）      ← 第①②步
   +
DHCP 拿到了 IP（系统有了门牌号）                    ← 第③步
   =
✅ 可以开始编程了（socket / HTTP / MQTT）
```

**如果少了这一步：** ESP32 能连上 WiFi（信号满格），但没有 IP 地址。路由器知道"有个设备连上来了"，但不知道数据该发给谁。现象就是——WiFi 图标亮了但上不了网。

---

#### 三件事总结

| # | 代码层面的作用 | 大白话类比 | 少了它 |
|---|---------------|-----------|--------|
| ① | 创建 `esp_netif` 对象 | 给系统注册"我有一个无线网卡" | 系统不知道有 WiFi 硬件 |
| ② | 关联 LwIP 协议栈 | 给网卡勾上"Internet 协议 (TCP/IP)" | 收到比特流但翻译不了，程序拿不到数据 |
| ③ | 注册 DHCP 客户端 | 自动跟物业（路由器）要门牌号 | 信号满格但没有 IP，上不了网 |

**所以这一行代码的本质：** 把 ESP32 的 WiFi 硬件，变成一个操作系统能认、TCP/IP 能用、有 IP 地址的完整网络接口。在这行之后，你写的所有 MQTT / HTTP / socket 代码才有地方落脚。

---

### 3.3 完整初始化流程中的其他函数

以下按 `wifi_init_sta()` 中出现的顺序，逐一说明每个函数的用途和参数。

#### `nvs_flash_init()` — 初始化 NVS 存储

```c
esp_err_t nvs_flash_init(void);
```

| 项目 | 说明 |
|------|------|
| **参数** | 无 |
| **返回值** | `ESP_OK` 成功 / 其他表示错误 |
| **头文件** | `nvs_flash.h` |

**做什么：** WiFi 驱动内部需要保存一些参数（比如上次连过的路由器的 MAC、校准数据等），这些存在 NVS（Non-Volatile Storage）分区中。在 WiFi 初始化之前必须先调用它。

**注意：** 开发过程中频繁烧录可能导致 NVS 分区格式变了，第一次会失败。标准写法是：

```c
esp_err_t ret = nvs_flash_init();
if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
}
ESP_ERROR_CHECK(ret);
```

---

#### `esp_netif_init()` — 启动网络接口层

```c
esp_err_t esp_netif_init(void);
```

| 项目 | 说明 |
|------|------|
| **参数** | 无 |
| **返回值** | `ESP_OK` 成功 |
| **头文件** | `esp_netif.h` |

**做什么：** 初始化 LwIP 协议栈，启动 TCP/IP 处理的核心服务。这是所有网络功能（WiFi / Ethernet / BLE 网络）的前提。

**什么时候调用：** 在任何 `esp_netif_create_*()` 和 `esp_wifi_*()` 之前。

---

#### `esp_event_loop_create_default()` — 创建默认事件循环

```c
esp_err_t esp_event_loop_create_default(void);
```

| 项目 | 说明 |
|------|------|
| **参数** | 无 |
| **返回值** | `ESP_OK` 成功 / `ESP_ERR_NO_MEM` 内存不足 |
| **头文件** | `esp_event.h` |

**做什么：** 创建一个后台任务，负责接收和分发 WiFi / IP 事件。没有它，你注册的事件回调 `wifi_event_handler()` 永远不会被调用。

**什么时候调用：** 在任何 `esp_event_handler_register()` 之前。

---

#### `esp_wifi_init()` — 初始化 WiFi 驱动

```c
esp_err_t esp_wifi_init(const wifi_init_config_t *config);
```

| 项目 | 说明 |
|------|------|
| **参数** | `wifi_init_config_t *` — WiFi 配置 |
| **返回值** | `ESP_OK` 成功 / `ESP_ERR_WIFI_NOT_INIT` 未调用 |
| **头文件** | `esp_wifi.h` |

**做什么：** 初始化 WiFi 驱动，分配内部资源（如 MAC 层缓冲区、控制结构体等）。

**参数构造：** 几乎永远使用默认配置：

```c
wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();  // 宏，直接赋值
esp_wifi_init(&cfg);
```

`WIFI_INIT_CONFIG_DEFAULT()` 展开后是一个配置结构体，包含默认的静态 RX/TX 缓冲区大小、动态 RX/TX 缓冲区数量、AMPDU 缓存等。**不需要看懂这些配置，所有默认值都已经调好了。**

---

#### `esp_wifi_set_mode()` — 设置 WiFi 模式

```c
esp_err_t esp_wifi_set_mode(wifi_mode_t mode);
```

| 项目 | 说明 |
|------|------|
| **参数** | `wifi_mode_t mode` — 模式选择 |
| **返回值** | `ESP_OK` 成功 / 配置错误 |
| **头文件** | `esp_wifi.h` |

**参数取值：**

| 值 | 含义 |
|----|------|
| `WIFI_MODE_STA` | Station（连别人路由器） |
| `WIFI_MODE_AP` | AP（自己做热点） |
| `WIFI_MODE_APSTA` | 同时当 STA 和 AP |
| `WIFI_MODE_NULL` | 不启用 WiFi |

**Station 模式就填 `WIFI_MODE_STA`。**

---

#### `esp_wifi_set_config()` — 配置目标路由器

```c
esp_err_t esp_wifi_set_config(wifi_interface_t interface, wifi_config_t *conf);
```

| 项目 | 说明 |
|------|------|
| **参数①** | `wifi_interface_t` — 接口类型：`WIFI_IF_STA` 或 `WIFI_IF_AP` |
| **参数②** | `wifi_config_t *` — 3.1 节中说的 SSID/密码结构体 |
| **返回值** | `ESP_OK` 成功 |
| **头文件** | `esp_wifi.h` |

**注意调用顺序：** 必须在 `esp_wifi_start()` **之前**调用。如果先 `start()` 再 `set_config()`，会返回 `ESP_ERR_WIFI_NOT_INIT`。

```c
// 正确顺序：
esp_wifi_set_mode(WIFI_MODE_STA);
esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
esp_wifi_start();

// ❌ 错误顺序：
esp_wifi_start();
esp_wifi_set_config(WIFI_IF_STA, &wifi_config);  // 可能失败
```

---

#### `esp_wifi_start()` — 启动 WiFi

```c
esp_err_t esp_wifi_start(void);
```

| 项目 | 说明 |
|------|------|
| **参数** | 无 |
| **返回值** | `ESP_OK` 成功 |
| **头文件** | `esp_wifi.h` |

**做什么：** 打开 WiFi 射频，开始扫描信道。这是 WiFi 从"配置状态"进入"运行状态"的开关。

**调用后：** 会触发 `WIFI_EVENT_STA_START` 事件，你的事件回调收到这个事件后应该调用 `esp_wifi_connect()`。

---

#### `esp_wifi_connect()` — 发起连接

```c
esp_err_t esp_wifi_connect(void);
```

| 项目 | 说明 |
|------|------|
| **参数** | 无 |
| **返回值** | `ESP_OK` 成功 / `ESP_ERR_WIFI_CONN` 连接中或未配置 |
| **头文件** | `esp_wifi.h` |

**做什么：** 根据之前 `esp_wifi_set_config()` 设置的路由器信息，发起连接尝试。

**什么时候调用：** 在 `WIFI_EVENT_STA_START` 事件回调中调用。如果断线了（`WIFI_EVENT_STA_DISCONNECTED`），需要重新调用它来重连。

**注意：** 每个 `WIFI_EVENT_STA_START` 事件只调用一次 `esp_wifi_connect()`。不要反复调用。

---

#### `esp_event_handler_register()` — 注册事件处理器

```c
esp_err_t esp_event_handler_register(
    esp_event_base_t      event_base,
    int32_t               event_id,
    esp_event_handler_t   event_handler,
    void                 *event_arg
);
```

| 项目 | 说明 |
|------|------|
| **参数①** | `event_base` — 事件基：`WIFI_EVENT` 或 `IP_EVENT` |
| **参数②** | `event_id` — 具体事件 ID，如 `WIFI_EVENT_STA_START`；`ESP_EVENT_ANY_ID` = 接所有该基下的事件 |
| **参数③** | `event_handler` — 回调函数指针 |
| **参数④** | `event_arg` — 传给回调的上下文参数，不用就填 `NULL` |
| **返回值** | `ESP_OK` 成功 |
| **头文件** | `esp_event.h` |

**示例：**

```c
// 接收 WIFI_EVENT 下的所有事件
esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &my_handler, NULL);

// 只接收 IP_EVENT 中的 GOT_IP 一个事件
esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &my_handler, NULL);
```

---

#### 函数调用顺序速查

```c
nvs_flash_init();                         // ① NVS 必须先开
esp_netif_init();                         // ② 网络接口层
esp_event_loop_create_default();          // ③ 事件循环
esp_netif_create_default_wifi_sta();      // ④ 创建 STA 网卡
WIFI_INIT_CONFIG_DEFAULT() + esp_wifi_init(&cfg);  // ⑤ WiFi 驱动
esp_event_handler_register(...);          // ⑥ 注册事件
esp_wifi_set_mode(WIFI_MODE_STA);         // ⑦ 设模式
wifi_config_t + esp_wifi_set_config(...); // ⑧ 配置路由器
esp_wifi_start();                         // ⑨ 启动 WiFi
// → 事件回调中 esp_wifi_connect()        // ⑩ 触发连接
```

> **不需要背这些顺序。** 写代码时照着第 6 节的完整代码骨架抄就行。

---

## 4. 事件驱动模型详解

### 4.1 什么是事件驱动

传统的编程方式是"你做一步、我做一步"（轮询）：

```c
while (1) {
    if (wifi_is_connected()) {
        // 连上了，干活
    }
    vTaskDelay(100);
}
```

事件驱动反过来：**状态变了通知你，你不需要反复问"现在怎么样了"**。

```c
// 注册好处理器，然后该干嘛干嘛
esp_event_handler_register(WIFI_EVENT, ...);
esp_event_handler_register(IP_EVENT, ...);

// 事情发生了，系统自动调你这个函数
static void wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();  // ← 系统告诉你"可以开始连了"
    }
}
```

### 4.2 两种事件类型

| 事件基 | 前缀 | 典型事件 |
|--------|------|---------|
| `WIFI_EVENT` | `WIFI_EVENT_STA_*` | START / CONNECTED / DISCONNECTED |
| `IP_EVENT` | `IP_EVENT_STA_*` | GOT_IP / LOST_IP |

它们是两组完全不同的事件系统：
- `WIFI_EVENT` 来自 WiFi 驱动层 → 告诉你"连上了路由器"、"掉线了"
- `IP_EVENT` 来自 LwIP 协议栈 → 告诉你"拿到 IP 了"、"IP 丢了"

### 4.3 事件处理器的注册方式

```c
// 在 app_main 中注册
esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &got_ip_event_handler, NULL);
```

- 第一个参数：事件基（决定"哪一类事件"）
- 第二个参数：事件 ID（具体事件，`ESP_EVENT_ANY_ID` = 所有）
- 第三个参数：回调函数
- 第四个参数：上下文（指向任意数据，处理器里通过 `void *arg` 拿到）

### 4.4 事件回调 vs 任务

**事件回调里不能做的事：**
- ❌ 阻塞（`vTaskDelay()`、等待信号量）
- ❌ 调用可能阻塞的网络 API
- ❌ 做耗时操作（例如 HTTP 请求）

**事件回调里应该做的事：**
- ✅ 设置标志位
- ✅ 发送队列/信号量/事件组
- ✅ 调用 `esp_wifi_connect()`（这个是特例，很快）

耗时操作放到任务中处理：

```c
static void wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    if (event_id == WIFI_EVENT_STA_CONNECTED) {
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        // ← 别在这里开 HTTP 连接，发个标志就完事
    }
}
```

---

## 5. 自动重连

### 5.1 ESP-IDF 内置重连 vs 手动重连

| 方法 | 做法 | 适用场景 |
|------|------|---------|
| 内置重连 | `esp_wifi_set_auto_connect(true)` | 简单场景，断线后自动尝试连回去 |
| 手动重连 | 在 `DISCONNECTED` 事件中调 `esp_wifi_connect()` | 需要控制重连间隔/次数/策略 |

### 5.2 内置重连

```c
esp_wifi_set_auto_connect(true);  // 默认就是 true
```

掉线后 WiFi 驱动会自动重连，你什么也不用做。但缺点是你无法控制"隔多久重连一次"、"最多重连几次"。

### 5.3 手动重连（推荐学习）

手动重连让你能控制重连行为：

```c
static void wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    if (event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();                          // 启动后立即连接
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "WiFi 断开，3 秒后重连...");
        vTaskDelay(pdMS_TO_TICKS(3000));             // 等待 3 秒再重连
        esp_wifi_connect();
    }
}
```

**注意：** 事件回调中调 `vTaskDelay()` **不是好做法**（上面说过了回调中不能阻塞）。正确做法是在任务中处理重连逻辑，这里只是为展示"手动重连"的概念。实际代码会把重连逻辑移到独立任务中。

---

## 6. 完整代码骨架

这是一个最小可运行的 WiFi Station 示例，去掉细节只保留核心流程：

```c
/* ===============================================================
 * WiFi Station 连接 · 最小示例
 *
 * 流程：初始化 → 连接 → 等 IP → 打印 IP
 * =============================================================== */

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"

static EventGroupHandle_t wifi_event_group;
const int WIFI_CONNECTED_BIT = BIT0;     // 拿到 IP 时置位

static void wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    if (base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                esp_wifi_connect();                         // ① 驱动就绪，发起连接
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                // WiFi 连接断了 — 可能是信号差、路由器重启等
                ESP_LOGI(TAG, "WiFi 断连，准备重连...");
                xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
                esp_wifi_connect();                         // ② 立即尝试重连
                break;
        }
    } else if (base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            ESP_LOGI(TAG, "已获取 IP: " IPSTR, IP2STR(&event->ip_info.ip));
            xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);  // ③ 标记就绪
        }
    }
}

void wifi_init_sta(void)
{
    // 1. NVS 初始化（WiFi 驱动需要 NVS 存储配置）
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. 初始化网络接口
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();                    // 创建 Station 网卡

    // 3. 初始化 WiFi 驱动
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // 4. 注册事件处理器
    wifi_event_group = xEventGroupCreate();
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL);

    // 5. 配置要连接的路由器
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "MyHomeWiFi",
            .password = "12345678",
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    // 6. 启动 WiFi
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi 初始化完成，等待连接...");
}
```

### 这个代码里的"套路"

初学者照着这个模式写就行，不用每行都搞懂：

| 步骤 | 函数/操作 | 说明 |
|------|----------|------|
| ① | `nvs_flash_init()` | WiFi 驱动内部要用 NVS 存配置 |
| ② | `esp_netif_init()` | 启动网络接口层 |
| ③ | `esp_event_loop_create_default()` | 启动事件循环 |
| ④ | `esp_netif_create_default_wifi_sta()` | 创建 Station 网卡 |
| ⑤ | `WIFI_INIT_CONFIG_DEFAULT()` + `esp_wifi_init()` | 初始化 WiFi 驱动 |
| ⑥ | `esp_event_handler_register()` | 注册事件回调 |
| ⑦ | `wifi_config_t` + `esp_wifi_set_config()` | 设置要连的路由器 |
| ⑧ | `esp_wifi_start()` | 启动 WiFi |
| ⑨ | 事件回调中 `esp_wifi_connect()` | 触发连接 |

---

## 7. 常见的坑

### 7.1 NVS 空间满了

```c
nvs_flash_init() 返回 ESP_ERR_NVS_NO_FREE_PAGES
```

**原因：** 开发过程中反复烧录不同版本的固件，NVS 分区布局变了，导致空间耗尽。

**解法：**
```c
// 首次失败时擦除重试（每个工程都该这么写）
esp_err_t ret = nvs_flash_init();
if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
}
ESP_ERROR_CHECK(ret);
```

### 7.2 连上但没网（没拿到 IP）

```
WIFI_EVENT_STA_CONNECTED 触发了，但 IP_EVENT_STA_GOT_IP 没来
```

**排查步骤：**
1. 检查路由器 DHCP 功能是否开启
2. 检查路由器是否启用了 MAC 地址过滤
3. 检查路由器是否限制了连接设备数
4. 等了多久？DHCP 通常 1-3 秒，有时要 5-10 秒

### 7.3 事件回调里做耗时操作

```c
// ❌ 千万别这么干
static void event_handler(...) {
    if (event_id == WIFI_EVENT_STA_CONNECTED) {
        http_request_send();      // 阻塞，会卡住整个事件循环
        vTaskDelay(5000);         // 超时，看门狗可能会复位
    }
}

// ✅ 正确做法：发个标志，让任务处理
static void event_handler(...) {
    if (event_id == WIFI_EVENT_STA_CONNECTED) {
        xEventGroupSetBits(evg, CONNECTED_BIT);  // ← 就做这一件事
    }
}
```

### 7.4 忘了调用 `esp_wifi_connect()`

`esp_wifi_start()` 只是启动 WiFi 射频，**不会自动连接**。必须在收到 `WIFI_EVENT_STA_START` 事件后调用 `esp_wifi_connect()`。

### 7.5 CONFIG_ESP_WIFI_REMOTE_ENABLED 默认问题

```c
esp_wifi_set_config(WIFI_IF_STA, &wifi_config);  // 返回 ESP_ERR_WIFI_NOT_INIT
```

如果在 `esp_wifi_start()` **之后**才调用 `esp_wifi_set_config()`，可能会失败。正确的顺序是：
```
esp_wifi_set_mode() → esp_wifi_set_config() → esp_wifi_start()
```

---

## 8. 如何验证 WiFi 连接成功

烧录后通过串口监视器观察日志：

### 成功输出
```
I (1234) wifi: State: init -> auth (bssid: aa:bb:cc:dd:ee:ff)
I (1235) wifi: State: auth -> assoc (0)
I (1240) wifi: State: assoc -> run (10)
I (1250) wifi: connected with MyHomeWiFi, channel 6, auth WPA2_PSK
I (1250) wifi_event: WiFi 已连接到路由器
I (3250) wifi_event: 已获取 IP: 192.168.1.100
```

关键行：
- `wifi: connected with MyHomeWiFi` — 物理层连接成功
- `已获取 IP: 192.168.1.100` — 网络层就绪，可以开始通信了

### 失败输出
```
I (1234) wifi: State: init -> auth (bssid: ...)
I (1235) wifi: State: auth -> assoc (0)
I (1236) wifi: State: assoc -> run (10)
I (1250) wifi: connected with MyHomeWiFi, channel 6, auth WPA2_PSK
I (1250) wifi_event: WiFi 已连接到路由器
... (等 10 秒) ...
I (11250) wifi: disconnect reason 201
I (11250) wifi_event: WiFi 断开，重连...
```

`reason 201` = DHCP 协商失败（没拿到 IP）。检查路由器 DHCP 设置。

---

## 9. CMake 依赖

WiFi 组件是 ESP-IDF 默认链接的，不需要在 `PRIV_REQUIRES` 中特别添加。但需要确认 sdkconfig 中 WiFi 已启用：

```
CONFIG_ESP_WIFI_ENABLED=y
```

这个默认就是 `y`，一般不需要改。

如果你的代码中使用 `nvs_flash`，需要在 CMakeLists.txt 中加入：

```cmake
idf_component_register(SRCS "main.c"
                       PRIV_REQUIRES nvs_flash
                       INCLUDE_DIRS "")
```

但 `nvs_flash` 通常也是默认链接的（因为 ESP-IDF 的默认依赖包含它）。只在链接时报 `undefined reference to nvs_flash_init` 时才需要加。

---

## 10. 事件组 vs 全局变量

WiFi 连接完成后，其他任务需要知道"现在网络是否可用"。有两种做法：

| 方法 | 做法 | 推荐？ |
|------|------|--------|
| 全局变量 | `bool wifi_connected = true;` | ❌ 跨任务访问有竞争 |
| 事件组 | `xEventGroupSetBits(...)` + `xEventGroupWaitBits(...)` | ✅ 原子操作，没有竞争 |

事件组的具体用法（代码片段）：

```c
/* 在事件回调中置位 */
static void wifi_event_handler(...) {
    if (event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/* 在其他任务中等待 */
void mqtt_task(void *pv) {
    while (1) {
        xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT,
                            pdFALSE, pdTRUE, portMAX_DELAY);
        // 能走到这里说明 WiFi 已连接
        // ...
    }
}
```

**另一种常见做法：** 用队列或信号量。事件组的好处是可以同时等待多个条件（比如"WiFi 连上了 且 传感器数据准备好了"）。

---

## 11. 在本项目中的定位

在 Phase 5 中，WiFi Station 是起点，后续模块都依赖它：

```
WiFi Station 连接（本文）
    │
    ├─ MQTT 客户端 ─→ 云平台（EMQX / 阿里云等）
    │
    ├─ HTTP 客户端 ─→ OTA 升级、REST API
    │
    └─ BLE + WiFi 共存（Phase 6 进阶）
```

需要用 NVS 存储 WiFi 配置（SSID/密码），便于后续的配网功能。

---

## 知识点脑图

```
WiFi Station 连接
│
├─ 架构分层
│   ├─ 应用层（事件处理 + 连接策略）
│   ├─ esp_netif（网络接口抽象）
│   └─ WiFi 驱动（底层扫描/认证/关联）
│
├─ 连接流程
│   ├─ 初始化顺序（NVS → netif → event → wifi）
│   ├─ 事件序列（START → CONNECTED → GOT_IP）
│   └─ 关键信号：IP_EVENT_STA_GOT_IP
│
├─ 事件驱动模型
│   ├─ WIFI_EVENT 系列（物理层状态）
│   ├─ IP_EVENT 系列（网络层状态）
│   ├─ 注册方式：esp_event_handler_register()
│   └─ 回调中不能阻塞
│
├─ 配置结构体
│   ├─ wifi_config_t（SSID/密码/认证类型）
│   ├─ wifi_init_config_t（WIFI_INIT_CONFIG_DEFAULT）
│   └─ esp_netif_create_default_wifi_sta()
│
├─ 重连机制
│   ├─ 内置自动重连（简单但不可控）
│   └─ 手动重连（自定义策略）
│
├─ 状态通知
│   ├─ 事件组（推荐，无竞争）
│   └─ 全局变量（不推荐，有风险）
│
└─ 常见坑
    ├─ NVS 空间满 → 擦除重试
    ├─ 连上但没 IP → DHCP 问题
    ├─ 忘了 esp_wifi_connect()
    └─ 回调中做耗时操作
```
