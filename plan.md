# FreeRTOS 物联网学习方案 — ESP32-S3 开发板

## 概述

- **硬件**: ESP32-S3-N16R8（双核 Xtensa LX7 @240MHz, 16MB Flash, 8MB PSRAM）
- **目标**: 掌握 FreeRTOS + BLE 蓝牙开发，以物联网为主线，逐步集成全部模块
- **框架**: ESP-IDF（内置 FreeRTOS SMP 版本，NimBLE 协议栈）
- **总周期**: 约 10 周，分 7 个实施阶段 + 1 个验证阶段

---

## 已发现的资料源（Phase 0 输出）

### 核心文档
| 资料 | 用途 |
|------|------|
| FreeRTOS 官方快速入门 (2026更新) | 任务/队列/信号量基础概念 |
| ESP-IDF FreeRTOS SMP 改动说明 | ESP-IDF 特有差异（自旋锁、核绑定） |
| ESP-IDF FreeRTOS API 参考 | 实际编码时的 API 手册 |
| ESP32-S3 技术参考手册 | 外设寄存器级参考 |
| esp32-camera GitHub | OV2640 驱动 |
| ESP-IDF NimBLE API 指南 | BLE GAP/GATT/广播/扫描 API |
| ESP-IDF BLE 入门示例 (ble_get_started) | NimBLE 分步教程（Beacon/连接/吞吐量） |

### 推荐课程
- GitHub: `god233012yamil/30-Day-FreeRTOS-Course-for-ESP32-Using-ESP-IDF`
- GitHub: `vikipat/RTOS-Essentials`（10周实战）
- Packt: 《Hands-On RTOS with Microcontrollers》2nd Edition
- ESP-IDF 官方 NimBLE 入门示例: `examples/bluetooth/ble_get_started/nimble/`（Beacon → 连接 → 吞吐量）
- ESP-IDF 官方 BLE 综合示例: `examples/bluetooth/nimble/`（bleprph/blecent/blehr 等）

### 关键差异备忘
1. **ESP-IDF 中栈大小单位是 bytes**（原生 FreeRTOS 是 words）
2. **不要调用 `vTaskStartScheduler()`**，ESP-IDF 自动启动
3. **临界区用 `portMUX_TYPE` 自旋锁**（非全局关中断）
4. **`pdMS_TO_TICKS(9)` 在 100Hz tick 下为 0**
5. **高优先级任务死循环会触发 TWDT**
6. **删除任务后需让空闲任务运行以回收内存**
7. **ESP32-S3 不支持经典蓝牙（BR/EDR/SPP）**，仅 BLE，推荐用 NimBLE 协议栈
8. **BLE + WiFi 共存需在 menuconfig 中使能 `CONFIG_ESP_COEX_SW_COEXIST_ENABLE`**

---

## Phase 1: 开发环境搭建与第一个 RTOS 程序

### 目标
搭建 ESP-IDF 开发环境，理解 FreeRTOS 在 ESP-IDF 中的启动流程，创建第一个多任务程序。

### 具体任务
1. **安装 ESP-IDF**
   - 下载 ESP-IDF 离线安装包（推荐 v5.3+）
   - 运行安装脚本，设置环境变量
   - 验证: `idf.py --version`

2. **创建 Hello World 工程**
   - `idf.py create-project freertos_learn`
   - 设置 target: `idf.py set-target esp32s3`
   - 菜单配置: `idf.py menuconfig` → 确认 Flash 和 PSRAM 设置
   - 构建: `idf.py build`
   - 烧录: `idf.py -p COM? flash monitor`
   - 验证: 看到 "Hello world!" 和重启信息

3. **第一个多任务程序**
   - 创建两个任务，分别以不同频率打印日志
   - 演示抢占：高优先级任务就绪时打断低优先级
   - 使用 `xTaskCreatePinnedToCore()` 将任务固定到不同核心
   - 使用 `vTaskDelay()` 和 `vTaskDelayUntil()` 控制周期

### 涉及的模块
- LED 指示灯（GPIO 输出，每个任务控制一个 LED）

### 参考文档
- ESP-IDF 入门指南（examples/get-started/hello_world）
- ESP-IDF FreeRTOS API 参考：task.h
- `xTaskCreatePinnedToCore()` API 签名（见子代理报告 §2）

### 验证清单
- [ ] `idf.py build` 成功
- [ ] 固件烧录到板子正常运行
- [ ] 两个任务同时在串口输出，核心 ID 不同
- [ ] LED 以不同频率闪烁

### 反模式保护
- ❌ 不要调用 `vTaskStartScheduler()`
- ❌ 不要在任务中写入阻塞性死循环而不加 `vTaskDelay()`
- ✅ 使用 `pdMS_TO_TICKS()` 转换毫秒到 tick

---

## Phase 2: 任务间通信基础 — 队列与信号量

### 目标
掌握 RTOS 的核心价值：任务间数据传递和同步。结合按键中断和传感器数据采集。

### 具体任务

1. **按键中断 + 二值信号量**
   - 配置 GPIO 中断（上升沿/下降沿）
   - 安装 ISR 服务: `gpio_install_isr_service(0)`
   - ISR 中发送信号量: `xSemaphoreGiveFromISR()`
   - 任务中等待信号量: `xSemaphoreTake()`
   - 验证: 按按键 → LED 切换

2. **队列传递传感器数据**
   - DHT11/DS18B20 数据采集任务（周期 2s）
   - 通过队列发送温湿度/温度数据
   - 显示任务接收队列并在 OLED 上显示
   - 使用 `xQueueCreate()` / `xQueueSend()` / `xQueueReceive()`

3. **互斥锁保护 I2C 总线**
   - 多个任务共享 I2C 总线（OLED + 传感器）
   - 用互斥锁保护 I2C 操作，防止数据竞争
   - `xSemaphoreCreateMutex()` + `xSemaphoreTake()/Give()`

### 涉及的模块
- 按键（GPIO 中断 + 二值信号量）
- DHT11/DS18B20（单总线或 I2C）
- OLED（I2C 显示）
- LED（输出指示）

### 参考文档
- ESP-IDF GPIO API: `driver/gpio.h`
- ESP-IDF I2C API: `driver/i2c.h`（或 v5.3+: `esp_driver_i2c`）
- FreeRTOS queue.h / semphr.h

### 验证清单
- [ ] 按键按下触发 LED 切换
- [ ] OLED 每 2 秒刷新一次温湿度数据
- [ ] 移除互斥锁后能看到显示错乱，加上后恢复正常

### 反模式保护
- ❌ ISR 中不要调用阻塞 API（只用 `FromISR` 版本）
- ❌ 不要在 ISR 中做耗时操作（I2C/SPI 通信）
- ✅ 全局变量跨任务访问必须用互斥锁保护

---

## Phase 3: 高级 IPC 与事件驱动

### 目标
掌握事件组、任务通知、软件定时器，实现复杂的事件驱动系统。

### 具体任务

1. **事件组协调多条件**
   - 定义事件位: KEY1_PRESS、KEY2_PRESS、TEMP_READY、PHOTO_READY
   - 不同任务设置不同事件位
   - 协调任务等待所有条件满足后执行联合操作
   - `xEventGroupCreate()` / `xEventGroupSetBits()` / `xEventGroupWaitBits()`

2. **任务通知替代信号量**
   - 按键 ISR 改为 `vTaskNotifyGiveFromISR()`
   - 处理任务用 `ulTaskNotifyTake()`
   - 对比与二值信号量的性能差异（任务通知快约 45%）

3. **软件定时器驱动周期性动作**
   - 创建自动重载定时器（传感器轮询）
   - 创建一次性定时器（超时关闭继电器）
   - 使用 `xTimerCreate()` / `xTimerStart()` / `xTimerStop()`

4. **RMT 驱动 WS2812B RGB 灯**
   - 配置 RMT 发送通道
   - 编码 RGB 颜色数据为 RMT 脉冲序列
   - 通过事件组控制：不同事件位对应不同颜色

### 涉及的模块
- 按键 × 4（事件源）
- WS2812B RGB（RMT 驱动）
- 继电器（软件定时器控制自动关断）
- 蜂鸣器（PWM 驱动，事件通知）

### 参考文档
- ESP-IDF event_groups.h
- ESP-IDF task.h（任务通知）
- ESP-IDF RMT API（v5.x 新架构）
- FreeRTOS 软件定时器 API

### 验证清单
- [ ] 同时按住 KEY1+KEY2 触发特定事件
- [ ] RGB 灯通过按键切换颜色
- [ ] 继电器在设定时间后自动关断
- [ ] 蜂鸣器按键发声

### 反模式保护
- ❌ 软件定时器回调中不要阻塞（运行在定时器任务上下文中）
- ✅ 事件组位操作在 SMP 下是安全的（内部用临界区保护）

---

## Phase 4: 文件系统与数据记录

### 目标
掌握 SPIFFS（内部 Flash）和 SD 卡（FAT）文件系统，实现传感器数据记录和配置文件存储。

### 具体任务

1. **SPIFFS 配置文件存储**
   - 分区表增加 storage 分区
   - 挂载 SPIFFS: `esp_vfs_spiffs_register()`
   - 写入/读取 WiFi 配置、校准参数
   - 使用标准 POSIX API（fopen/fread/fwrite/fprintf）

2. **SD 卡数据记录**
   - SDMMC 或 SDSPI 接口挂载 SD 卡
   - 定时任务每 5 分钟将温湿度数据写入 CSV 文件
   - 按键触发拍照：SD 卡存储 JPEG 图片

3. **摄像头初始化与拍照**
   - 从组件管理器添加 `espressif/esp32-camera`
   - menuconfig 中启用 PSRAM
   - 配置 `camera_config_t` 引脚映射
   - `esp_camera_fb_get()` 拍照 → 数据写入 SD 卡
   - 帧率控制：限制拍照频率防 OOM

### 涉及的模块
- SD 卡（SPI/SDMMC）
- OV2640 摄像头（DVP 并行接口）
- OLED（拍照时显示状态）

### 参考文档
- ESP-IDF SPIFFS API: `esp_vfs_spiffs.h`
- ESP-IDF SDMMC/SDSPI: `esp_vfs_fat.h`
- esp32-camera GitHub: API 和使用示例

### 验证清单
- [ ] 板子重启后能从 SPIFFS 读取 WiFi 配置
- [ ] SD 卡 CSV 文件包含正确的温湿度时间序列
- [ ] 按键触发拍照，JPEG 文件正确存储到 SD 卡
- [ ] 长时间运行不内存泄漏

### 反模式保护
- ❌ 不要在 SPIFFS 上频繁写小文件（磨损）
- ✅ 大缓冲区用 `heap_caps_malloc(size, MALLOC_CAP_SPIRAM)` 分配到 PSRAM
- ✅ 拍照帧缓冲区使用后必须调用 `esp_camera_fb_return()`

---

## Phase 5: 网络通信 — WiFi + MQTT 物联网核心

### 目标
实现 WiFi 连接、MQTT 协议通信，接入主流物联网云平台，实现远程数据上报和命令下发。

### 具体任务

1. **WiFi Station 连接（带重连）**
   - `esp_netif_init()` + `esp_event_loop_create_default()`
   - `esp_netif_create_default_wifi_sta()`
   - 注册事件处理器（`WIFI_EVENT_STA_START/CONNECTED/DISCONNECTED`、`IP_EVENT_STA_GOT_IP`）
   - 断线自动重连逻辑
   - WiFi 配置从 SPIFFS 读取 / NVS 存储

2. **MQTT 客户端接入**
   - 初始化 `esp_mqtt_client_init()`
   - 注册事件处理器（`MQTT_EVENT_CONNECTED/DATA/DISCONNECTED`）
   - 订阅下行命令 topic
   - 温湿度数据定时发布到 MQTT topic
   - 使用公共测试 broker 验证（如 `broker.emqx.io`）

3. **云平台对接（可选 EMQX / 阿里云 IoT / AWS IoT Core）**
   - MQTT TLS 加密连接 (mqtts://)
   - 设备证书认证
   - 设备影子 / 属性上报

4. **双核分工架构**
   - APP_CPU: 传感器采集 + 摄像头（数据生产）
   - PRO_CPU: WiFi + MQTT + OLED（数据消费和通信）
   - 用队列跨核传递传感器数据

### 涉及的模块
- WiFi（ESP32-S3 内置）
- MQTT 协议
- 全部已有传感器（温湿度、摄像头等数据源）

### 参考文档
- ESP-IDF WiFi Station 示例: `examples/wifi/getting_started/station/`
- ESP-MQTT API: `mqtt_client.h`
- ESP-MQTT 示例: `examples/protocols/mqtt/tcp/`

### 验证清单
- [ ] 开发板成功连接 WiFi 并获取 IP
- [ ] MQTT 连接测试 broker 成功
- [ ] 传感器数据在云端可见
- [ ] 云端下发的命令被板子接收并执行（如控制继电器、RGB灯）
- [ ] WiFi 断开后自动重连，MQTT 自动恢复

### 反模式保护
- ❌ 不要在 WiFi/MQTT 事件回调中做耗时操作
- ✅ 事件回调中仅做标志设置，实际处理在任务中
- ❌ 不要在 ISR 中访问 WiFi/MQTT API

---

## Phase 6: BLE 通信 — NimBLE 协议栈实战

### 目标
掌握 BLE 5.0 开发，使用 NimBLE 协议栈实现外设（Peripheral）和中心（Central）角色，实现手机 App 与开发板双向通信。

### 核心知识点
| 概念 | 说明 |
|------|------|
| GAP (Generic Access Profile) | 广播、扫描、连接管理 |
| GATT (Generic Attribute Profile) | Service/Characteristic 层次结构 |
| UUID | 16-bit（标准服务）vs 128-bit（自定义服务） |
| Notification / Indication | 服务器主动推送数据 |
| Advertising | 广播包格式与参数（间隔、类型、数据） |
| BLE 5.0 新特性 | 2M PHY、Coded PHY（长距离）、扩展广播 |

### 具体任务

1. **BLE Beacon（信标）**
   - 配置 NimBLE 协议栈：`nimble_port_init()` + `ble_hs_cfg`
   - 设置设备名称: `ble_svc_gap_device_name_set()`
   - 配置广播参数（`ble_gap_adv_params`）：可发现、不可连接模式
   - 广播自定义数据（Eddystone 或 iBeacon 格式）
   - 验证：手机 nRF Connect App 扫描到设备

2. **GATT Server（外设角色）**
   - 定义自定义 Service（128-bit UUID）
   - 定义 Characteristic：读/写/通知属性
   - 注册服务表: `ble_gatts_count_cfg()` + `ble_gatts_add_svcs()`
   - 实现 access callback 处理读写请求
   - 通过 `ble_gatts_notify()` 主动推送传感器数据
   - 验证：手机 App 连接后查看服务列表、接收通知

3. **GATT Client（中心角色）**
   - 实现扫描回调：`ble_gap_disc()` 发现设备
   - 主动连接目标设备
   - 发现服务：`ble_gattc_disc_svc_by_uuid()`
   - 读写特征值：`ble_gattc_write_flat()` / `ble_gattc_read_by_uuid()`
   - 验证：开发板扫描并连接另一台 BLE 设备

4. **BLE 数据传输实战**
   - 温湿度数据通过 BLE 通知定时推送
   - 手机 App 发送命令控制继电器/RGB 灯
   - 实现 Nordic UART Service (NUS) 风格的透传通道
   - MTU 协商与大数据包传输

5. **BLE + WiFi 共存（进阶）**
   - menuconfig 使能共存：`CONFIG_ESP_COEX_SW_COEXIST_ENABLE`
   - BLE 任务固定 Core 0，WiFi 任务固定 Core 1
   - 运行 `bleprph_wifi_coex` 示例验证稳定
   - 观察共存对吞吐量和延迟的影响

### 涉及的模块
- BLE（ESP32-S3 内置）
- 按键（触发 BLE 事件）
- OLED（显示连接状态和设备信息）
- RGB LED（指示 BLE 连接状态）
- 温湿度传感器（数据通过 BLE 推送）

### 参考文档
- ESP-IDF NimBLE API: `nimble/nimble_port.h`、`host/ble_hs.h`、`services/gap/ble_svc_gap.h`
- NimBLE GATT Server: `host/ble_gatt.h`（`ble_gatts_*` API 族）
- NimBLE GATT Client: `host/ble_gattc.h`（`ble_gattc_*` API 族）
- 官方示例: `examples/bluetooth/nimble/bleprph`（Server），`examples/bluetooth/nimble/blecent`（Client）
- BLE 入门系列: `examples/bluetooth/ble_get_started/nimble/`
- BLE+WiFi 共存示例: `examples/bluetooth/nimble/bleprph_wifi_coex`
- nRF Connect App（手机端调试工具）

### 验证清单
- [ ] 手机 nRF Connect 扫描到开发板的 BLE 广播
- [ ] 手机连接后能发现自定义 Service 和 Characteristic
- [ ] 手机写入特征值，开发板收到并执行（如控制 LED）
- [ ] 开发板主动推送传感器数据到手机（Notification）
- [ ] GATT Client 扫描并连接其他 BLE 设备
- [ ] BLE + WiFi 同时工作稳定，无异常断开

### 反模式保护
- ❌ NimBLE 中 `onWrite` 回调内不要直接调用 `notify()`（通知可能在写确认前发出）
- ❌ 不要在 BLE event handler 中做耗时操作（应发队列到任务处理）
- ✅ GATT 服务表用 `ble_gatts_count_cfg()` + `ble_gatts_add_svcs()` 两步注册
- ✅ BLE 和 WiFi 共存时固定任务到不同核心
- ✅ 使用 `ble_hs_cfg.sync_cb` 等待主机同步完成后再操作 GATT

---

## Phase 7: 综合物联网项目 — 智能环境控制节点（WiFi + BLE 双模）

### 目标
整合所有已学知识，实现一个完整的物联网终端产品原型。

### 系统架构

```
                    ┌──────────────────────────────────────────┐
   APP_CPU (Core 1) │  传感器采集任务    摄像头拍照任务           │
                    │  (DHT11/DS18B20)  (OV2640 JPEG)         │
                    │       │                  │                │
                    │       ▼                  ▼                │
                    │   ┌──────────────┐  ┌────────┐           │
                    │   │ 传感器数据队列 │  │图片队列│           │
                    │   └──────┬───────┘  └───┬────┘           │
                    └──────────┼──────────────┼────────────────┘
                               │              │
                    ┌──────────┼──────────────┼────────────────┐
   PRO_CPU (Core 0) │          ▼              ▼                 │
                    │   ┌─────────────────────────────┐        │
                    │   │  数据汇聚与格式化任务          │        │
                    │   └──────┬──────────┬───────────┘        │
                    │          ▼          ▼                     │
                    │   ┌──────────┐  ┌──────────────┐         │
                    │   │ MQTT上传 │  │ BLE GATT通知 │         │
                    │   │ (WiFi)   │  │ (手机直连)   │         │
                    │   └──────────┘  └──────────────┘         │
                    │          │              │                  │
                    │          ▼              ▼                  │
                    │   ┌──────────────────────────────┐       │
                    │   │     命令处理任务              │       │
                    │   │  (MQTT下行 + BLE 写入)       │       │
                    │   │  → 继电器 / 舵机 / RGB灯     │       │
                    │   └──────────────────────────────┘       │
                    │                                          │
                    │   ┌──────────────────────────────┐       │
                    │   │ BLE Beacon (关机仍广播状态)   │       │
                    │   └──────────────────────────────┘       │
                    │                                          │
                    │   ┌──────────────────────────────┐       │
                    │   │ OTA 升级任务                  │       │
                    │   └──────────────────────────────┘       │
                    └──────────────────────────────────────────┘
```

### 具体任务

1. **系统任务划分**
   - 传感器采集任务（APP_CPU, 优先级 5）：DHT11 + DS18B20 轮询
   - 摄像头拍照任务（APP_CPU, 优先级 4）：按键触发或定时拍照
   - OLED 显示任务（PRO_CPU, 优先级 3）：实时数据 + 系统状态
   - MQTT 上传任务（PRO_CPU, 优先级 5）：WiFi 传感器数据上报
   - BLE GATT 任务（PRO_CPU, 优先级 4）：手机直连，推送数据 + 接收指令
   - BLE Beacon 任务（PRO_CPU, 优先级 1）：低优先级状态广播
   - 命令处理任务（PRO_CPU, 优先级 4）：MQTT 下行 + BLE 写入控制
   - OTA 升级任务（PRO_CPU, 优先级 2）：固件远程升级

2. **交互逻辑**
   - KEY1: 切换 OLED 显示页面（温湿度/系统状态/网络信息/BLE 连接）
   - KEY2: 手动拍照并上传（MQTT 或 BLE 通知）
   - KEY3: 切换 RGB 灯效模式
   - KEY4: 继电器手动开关
   - 红外遥控：可替代按键功能
   - 手机 App BLE 直连：查看数据、遥控执行器

3. **双模通信策略**
   - 有 WiFi 时：MQTT 上云 + BLE 本地直连并行
   - 无 WiFi 时：自动降级为纯 BLE 模式，手机 App 本地控制
   - WiFi 恢复后自动切回云端模式

4. **执行器控制**
   - 继电器：MQTT 远程控制 + 定时自动关断
   - 舵机：云台（摄像头角度调整）+ MQTT 控制
   - 直流/步进电机：根据传感器数据自动控制（如温度 > 阈值开启风扇）
   - RGB LED：系统状态指示（正常绿色/告警红色/OTA蓝色）

4. **调试与监控**
   - 串口输出 `vTaskList()` 和 `vTaskGetRunTimeStats()`
   - 栈余量监测：`uxTaskGetStackHighWaterMark()`
   - TWDT 配置和监控

### 涉及的模块（全部）
| 模块 | 用途 |
|------|------|
| OV2640 | 定时/按键拍照，MQTT/BLE上传 |
| OLED | 实时数据、系统状态显示 |
| SD卡 | 数据记录、固件OTA缓存 |
| DHT11/DS18B20 | 环境温湿度监测 |
| WS2812B | 系统状态指示（含 BLE 连接状态） |
| 继电器 | 远程控制大功率设备 |
| 蜂鸣器 | 告警/操作反馈 |
| 电机/步进电机 | 散热风扇/机械控制 |
| 按键×4 | 本地交互 |
| 红外接收 | 无线遥控 |
| 舵机×4 | 云台/机械臂控制 |
| ADC电位器 | 模拟量输入（如光照阈值设定） |
| LED | 运行状态指示 |
| **BLE (NimBLE)** | **手机直连通信，WiFi 断连时自动降级** |

### 验证清单
- [ ] 系统启动后自动连接 WiFi 和 MQTT
- [ ] 温湿度数据定时上报到云平台
- [ ] OLED 显示实时数据，按键切换页面
- [ ] 云平台下发命令控制继电器/舵机/RGB灯
- [ ] 按键拍照，JPEG 通过 MQTT 上传
- [ ] 手机 nRF Connect App 连接 BLE 查看传感器数据
- [ ] 手机 App 通过 BLE 写入控制执行器
- [ ] WiFi 断开后自动降级为纯 BLE 模式
- [ ] WiFi 恢复后自动切回云端模式，BLE 继续并行服务
- [ ] OTA 升级成功
- [ ] 连续运行 24 小时无内存泄漏/看门狗复位

---

## Phase 8: 最终验证

### 验证内容

1. **代码质量检查**
   - 每个任务的栈余量 > 20%（`uxTaskGetStackHighWaterMark()`）
   - 所有 ISR 中使用 `FromISR` 版本 API
   - 所有共享资源有互斥锁保护
   - 无全局变量裸访问（都用队列/信号量/互斥锁）

2. **功能验证**
   - 所有模块正常工作（见各阶段验证清单汇总）
   - MQTT 通断测试（断开 30 秒后自动恢复）
   - BLE 长连接稳定性（保持连接 2 小时无断连）
   - WiFi + BLE 双模同时工作不冲突
   - 长时间稳定性测试（24 小时运行，记录重启次数）

3. **反模式检查**
   - 无 `vTaskStartScheduler()` 调用
   - 无 `vTaskDelay(0)` 空转
   - 无 ISR 中 I2C/SPI 通信
   - 无任务死循环（每个循环路径都有阻塞点）

### 验证工具
- ESP-IDF Monitor（日志输出）
- `vTaskList()` / `vTaskGetRunTimeStats()`
- MQTT 客户端订阅验证数据

---

## 各阶段模块使用矩阵

| 模块 | P1 | P2 | P3 | P4 | P5 | P6 | P7 | P8 |
|------|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|
| LED指示灯 | ✔ | ✔ |   |   |   |   | ✔ |   |
| 按键×4 |   | ✔ | ✔ | ✔ |   | ✔ | ✔ |   |
| DHT11/DS18B20 |   | ✔ |   |   |   | ✔ | ✔ |   |
| OLED |   | ✔ |   |   |   | ✔ | ✔ |   |
| WS2812B RGB |   |   | ✔ |   |   | ✔ | ✔ |   |
| 继电器 |   |   | ✔ |   |   |   | ✔ |   |
| 蜂鸣器 |   |   | ✔ |   |   |   | ✔ |   |
| SD卡 |   |   |   | ✔ |   |   | ✔ |   |
| OV2640摄像头 |   |   |   | ✔ |   |   | ✔ |   |
| 红外接收 |   |   |   |   |   |   | ✔ |   |
| 舵机×4 |   |   |   |   |   |   | ✔ |   |
| 电机/步进电机 |   |   |   |   |   |   | ✔ |   |
| ADC电位器 |   |   |   |   |   |   | ✔ |   |
| WiFi+MQTT |   |   |   |   | ✔ | ✔ | ✔ |   |
| **BLE (NimBLE)** |   |   |   |   |   | ✔ | ✔ |   |
| OTA |   |   |   |   |   |   | ✔ |   |

