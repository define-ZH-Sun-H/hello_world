# 4-1 SPIFFS 配置文件存储

## 1. SPIFFS 是什么

| 项目 | 说明 |
|------|------|
| 全称 | SPI Flash File System |
| 性质 | 嵌入式 SPI Flash 的轻量级文件系统，日志结构（log-structured） |
| 特点 | 掉电安全、磨损均衡、实时垃圾回收，专为小内存 MCU 设计 |
| 限制 | **目录不支持**（所有文件扁平存储）、不支持创建子目录 |
| 适用场景 | 存储配置文件、校准参数、小规模数据记录 |
| 不适用场景 | 大文件、频繁写小文件（磨损快）、需要目录层次结构 |

**与 FAT 对比：**
- FAT 支持目录，但需要更多 RAM、有碎片问题、不是掉电安全设计
- SPIFFS 轻量，掉电安全，但不支持目录

---

## 2. 分区表（Partition Table）

SPIFFS 使用 Flash 上的一个独立分区。需在 `partitions.csv` 中添加。

### 基本概念
- `partitions.csv` 位于工程根目录，ESP-IDF 按此表划分 Flash
- 默认分区表包含：`nvs`、`phy_init`、`factory`（OTA_0）
- SPIFFS 用 `data` 类型、`spiffs` 子类型

### 典型条目

```
# Name,   Type, SubType, Offset,  Size, Flags
storage,  data, spiffs,  ,        2M,
```

- `2M` ＝ 分区大小，根据 Flash 总量（16MB）和固件大小分配
- Offset 留空则自动紧接上一个分区末尾
- Flags 可设 `encrypted`（需要 Flash Encryption 支持）

### 验证命令
```bash
idf.py partition-table
```

---

## 3. `esp_vfs_spiffs_register()` — 挂载 SPIFFS

挂载 SPIFFS 的核心函数。配置通过 `esp_vfs_spiffs_conf_t` 结构体。

```c
esp_vfs_spiffs_conf_t conf = {
    .base_path = "/spiffs",        // 挂载点（虚拟路径前缀）
    .partition_label = "storage",  // 分区表名称（与 csv 一致）
    .max_files = 5,                // 最大同时打开文件数（占用 RAM）
    .format_if_mount_failed = true // 挂载失败自动格式化（开发时方便）
};
esp_err_t ret = esp_vfs_spiffs_register(&conf);
if (ret != ESP_OK) {
    ESP_LOGE(TAG, "SPIFFS mount failed");
}
```

### 配置项详解
- `base_path` — POSIX 函数的前缀，后续 `fopen("/spiffs/config.txt", "r")`
- `max_files` — 每开一个文件消耗约 64-128 字节 RAM，按需设置
- `format_if_mount_failed` — 生产环境应设为 **false**，防止意外格掉已有配置
- `esp_vfs_spiffs_unregister()` — 卸载 SPIFFS，断开文件系统

---

## 4. 状态检查与信息获取

```c
// 检查分区是否已挂载
esp_err_t ret = esp_spiffs_mounted("storage");
if (ret == ESP_OK) {
    ESP_LOGI(TAG, "SPIFFS mounted");
}

// 获取信息（总大小、已用大小）
size_t total = 0, used = 0;
esp_err_t ret = esp_spiffs_info("storage", &total, &used);
if (ret == ESP_OK) {
    ESP_LOGI(TAG, "SPIFFS: %d total, %d used", total, used);
}

// 手动触发垃圾回收（通常不需要，SPIFFS 自动回收）
esp_spiffs_gc("storage", 4096);  // 回收 4KB
```

---

## 5. POSIX 文件操作 API

基于 VFS（Virtual File System），挂载后使用标准 C 库函数。

### 核心函数

| 函数 | 用途 | 示例 |
|------|------|------|
| `fopen()` | 打开/创建文件 | `fopen("/spiffs/config.txt", "r")` |
| `fprintf()` | 文本格式写入 | `fprintf(f, "key=%s\n", value)` |
| `fwrite()` | 二进制写入 | `fwrite(buf, 1, size, f)` |
| `fread()` | 二进制读取 | `fread(buf, 1, size, f)` |
| `fgets()` | 逐行读取文本 | `fgets(line, sizeof(line), f)` |
| `fseek()` | 定位 | `fseek(f, 0, SEEK_SET)` |
| `ftell()` | 当前位置 | `long pos = ftell(f)` |
| `fclose()` | 关闭 | `fclose(f)` |

### 补充函数
- `remove("/spiffs/config.txt")` — 删除文件
- `rename("/spiffs/old.txt", "/spiffs/new.txt")` — 重命名
- `stat("/spiffs/config.txt", &st)` — 获取文件大小等元信息

### 重要注意点
- SPIFFS 文件最大尺寸受分区大小和碎片影响，建议单文件不超过分区 10%
- `fclose()` 后数据实际刷入 Flash（`fflush()` 只刷到 VFS 层）
- `fopen("w")` 会截断现有文件，`"a"` 追加
- **目录操作（mkdir/opendir）在 SPIFFS 上不可用**

---

## 6. 配置文件格式选择

### 格式对比

| 格式 | 优点 | 缺点 | 推荐场景 |
|------|------|------|----------|
| **cJSON** | 人类可读、灵活、可扩展 | 解析开销大，代码量多，大 JSON 占用栈 | WiFi 配置等多字段场景 |
| **自定义 INI/KV** | 极轻量、解析快 | 灵活性差 | 少量参数（2-5 个） |
| **NVS** | ESP-IDF 内置 key-value API | 不适合结构化数据、大小有限 | 替代 SPIFFS 的超轻量配置 |

### cJSON 写入示例

```c
// 构建 JSON 对象
cJSON *root = cJSON_CreateObject();
cJSON_AddStringToObject(root, "ssid", "MyWiFi");
cJSON_AddStringToObject(root, "password", "12345678");
cJSON_AddNumberToObject(root, "calib_offset", 12);

// 序列化为字符串
char *json = cJSON_Print(root);

// 写入文件
fopen("/spiffs/config.json", "w");
fwrite(json, 1, strlen(json), f);
fclose(f);

// 清理
cJSON_Delete(root);
free(json);
```

### cJSON 读取示例

```c
// 读取文件
FILE *f = fopen("/spiffs/config.json", "r");
if (f == NULL) {
    // 文件不存在，使用默认配置
    return;
}
fread(buf, 1, sizeof(buf), f);
fclose(f);

// 解析 JSON
cJSON *root = cJSON_Parse(buf);
if (root == NULL) {
    ESP_LOGE(TAG, "JSON parse error");
    return;
}

cJSON *ssid = cJSON_GetObjectItem(root, "ssid");
if (cJSON_IsString(ssid)) {
    strlcpy(wifi_ssid, ssid->valuestring, sizeof(wifi_ssid));
}

cJSON_Delete(root);
```

---

## 7. 读写流程最佳实践

### 写入流程

```
1. 构建配置对象（内存中）
2. 序列化为字符串（cJSON_Print 或 sprintf）
3. fopen("w") → 截断旧文件
4. fwrite() 写入全部数据
5. fclose() → 刷入 Flash
```

### 读取流程

```
1. fopen("r") → 检查 NULL（文件不存在）
2. fread() 或 fgets() 读取
3. 解析（cJSON_Parse 或 sscanf）
4. fclose()
5. 返回值检查：存在则加载，不存在则使用默认配置
```

**核心思路：** 首次启动时文件不存在 → 用默认配置写入 → 后续启动从文件读取

---

## 8. SPIFFS 在 ESP-IDF 中的配置路径

| 配置项 | menuconfig 路径 | 说明 |
|--------|----------------|------|
| CONFIG_SPIFFS_MAX_PARTITIONS | Component config → SPIFFS | 最大分区数 |
| CONFIG_SPIFFS_CACHE | 同上 | 启用缓存（加速读） |
| CONFIG_SPIFFS_CACHE_WR | 同上 | 启用写缓存 |
| CONFIG_SPIFFS_PAGE_SIZE | 同上 | 页大小（256/512 字节） |
| CONFIG_SPIFFS_GC_MAX_RUNS | 同上 | GC 最大运行次数 |

**默认配置通常够用**，除非有特殊性能要求。

---

## 9. 反模式与注意事项

| ❌ 不要做 | ✅ 应该做 |
|-----------|----------|
| 频繁 `fwrite()` 小数据（磨损 Flash） | 攒够一定量再写入，或只在配置变更时写 |
| `format_if_mount_failed = true` 在产品固件中 | 生产环境设为 false，防止异常擦除用户配置 |
| 在 ISR 中调用文件操作 | 文件操作移到任务上下文中 |
| 忘记 `fclose()` 导致 RAM 和文件描述符泄漏 | 配对 `fopen/fclose`，用后即关 |
| 打开超过 `max_files` 的文件 | `max_files` 按需设置 |
| 单文件过大（>分区 50%） | SPIFFS 碎片多了后 GC 会变慢，建议<10% |

### 磨损估算
- SPIFFS 每次写文件都会在 Flash 不同位置写入（磨损均衡）
- 典型 Flash 擦写寿命 ~10 万次
- 配置一次写入（通常 100-500 字节/次）× 10万次 ≈ 不频繁改写时寿命足够
- 但每 5 秒写一次日志的话，几周就报废了 → **这种场景用 SD 卡**

---

## 10. 在本项目中的具体应用

根据 Phase 4 的规划，SPIFFS 在本工程中用于：

- **WiFi 配置**：SSID、密码，供 Phase 5 的 WiFi Station 使用
- **校准参数**：DS18B20 温度偏移、OLED 对比度等传感器校准值

**建议格式：** 使用 cJSON，因为 WiFi 配置 + 校准参数需要结构化存储，cJSON 可读可扩展。

---

## 11. 工程集成实战记录（2026-05-27）

以下是在本项目中实际挂载 SPIFFS 时遇到的坑和解决过程，文档层面不会写，但实操一定会碰到。

### 11.1 前置步骤

**在写一行代码之前，必须先改 sdkconfig 中的 3 项配置：**

| 步骤 | sdkconfig | 原值 | 新值 |
|------|-----------|------|------|
| ① | `CONFIG_ESPTOOLPY_FLASHSIZE` | `2MB` → `16MB` | ESP32-S3-N16R8 真实 Flash 是 16MB |
| ② | `CONFIG_PARTITION_TABLE_SINGLE_APP` | `y` → `n` | 默认分区表没有 SPIFFS 分区，必须切到自定义 |
| ③ | `CONFIG_PARTITION_TABLE_FILENAME` | `partitions_singleapp.csv` → `partitions.csv` | 指向自定义分区表 |

如果第一步不改，后面全是白费 — **2MB Flash 分不出空间给 SPIFFS**。

**必须同时改两个地方：**
```sdkconfig
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y     # 标志位
CONFIG_ESPTOOLPY_FLASHSIZE="16MB"     # 字符串值
```
两者缺一不可，编译时会用字符串值做检查。

### 11.2 partitions.csv 绑定关系

创建 `partitions.csv` 后注意 **3 个名字必须一致**：

```csv
# partitions.csv 中的 Name 列
storage,  data, spiffs,  ,        2M,
```

```c
// main.c 中挂载时填的分区名
.partition_label = "storage";

// esp_spiffs_info 查询时填的分区名
esp_spiffs_info("storage", &total, &used);
```

三个 `"storage"` 必须一致。拼错任何一个，挂载都失败。

### 11.3 CMake 依赖 — 最容易被忽略

```cmake
# main/CMakeLists.txt
idf_component_register(SRCS "queue_demo.c" "main.c"
                       PRIV_REQUIRES spi_flash bsp spiffs   # ← 必须加 spiffs
                       INCLUDE_DIRS "")
```

**不加会怎样：** 编译能过（头文件路径由 IDF 自动注入），但**链接时报错**，找不到 `esp_vfs_spiffs_register`、`esp_spiffs_info` 等函数。

### 11.4 SPIFFS Kconfig 配置块

使能 SPIFFS 后 sdkconfig 中会生成一整块配置。最小值（可安全用于生产）如下：

| 配置项 | 值 | 说明 |
|--------|-----|------|
| `CONFIG_SPIFFS=y` | — | 主开关 |
| `CONFIG_SPIFFS_PAGE_SIZE=256` | 256 | 页大小，默认即可 |
| `CONFIG_SPIFFS_OBJ_NAME_LEN=32` | 32 | 文件名最大长度 |
| `CONFIG_SPIFFS_MAX_PARTITIONS=3` | 3 | SPIFFS 分区最大数量 |
| `CONFIG_SPIFFS_GC_MAX_RUNS=10` | 10 | 每次 GC 最多运行次数 |
| `CONFIG_SPIFFS_USE_IDATA_CACHE=y` | 256B | 索引缓存，加速读 |
| `CONFIG_SPIFFS_USE_ICACHE=y` | 256B | 元数据缓存 |
| `CONFIG_SPIFFS_USE_BDATA_CACHE=y` | 256B | 块数据缓存 |

这些不需要手动写全 —— 在 `idf.py menuconfig` → `Component config → SPIFFS Configuration` 中勾选 `SPIFFS` 后会自动生成。

### 11.5 实际写入的挂载代码

```c
/* ================================================================
 * SPIFFS 挂载 — 配置文件存储
 * ================================================================ */
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "storage",
        .max_files = 5,
        .format_if_mount_failed = true,   // 开发时方便
    };
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret == ESP_OK) {
        size_t total = 0, used = 0;
        esp_spiffs_info("storage", &total, &used);
        DBG_INFO("SPIFFS 挂载成功: %s 分区, 总 %d KB, 已用 %d KB\n",
                 conf.partition_label, total / 1024, used / 1024);
    } else {
        DBG_WARN("SPIFFS 挂载失败: %s\n", esp_err_to_name(ret));
    }
}
```

**几点说明：**
- 用 `{}` 块作用域包裹，变量不泄漏到函数作用域
- `esp_err_to_name()` 比数字错误码更可读（比如 `ESP_ERR_NOT_FOUND` vs `0x103`）
- 挂载成功后立即打印分区容量，方便确认分区大小分配是否正确

### 11.6 构建验证的关键输出

```
[607/617] Linking C static library esp-idf/spiffs/libspiffs.a   ← SPIFFS 库编译了
[608/617] Building C object ...main/main.c.obj                   ← 挂载代码编译了
[614/617] Linking CXX executable hello_world.elf                  ← 链接成功
Successfully created esp32s3 image.                               ← 固件生成

hello_world.bin binary size 0x4b130 bytes. Smallest app partition is 0x200000 bytes.
0x1b4ed0 bytes (85%) free.                                        ← factory 分区足够大
```

**验证 Checklist：**
- [x] `spiffs/libspiffs.a` 被编译链接
- [x] `main.c.obj` 编译无 SPIFFS 相关错误
- [x] 固件 bin 生成成功
- [x] 固件体积 (300KB) << factory 分区 (2MB)，85% 空闲

### 11.7 启动后预期串口输出

```
SPIFFS 挂载成功: storage 分区, 总 2048 KB, 已用 0 KB
```

如果看到这个，说明分区表、sdkconfig、CMake 依赖、挂载代码全部正确串联。

### 11.8 常见失败及排查

| 现象 | 原因 | 解法 |
|------|------|------|
| `SPIFFS 挂载失败: ESP_FAIL` | 分区表没有 storage 分区或名不匹配 | 检查 `partitions.csv` → `partition_label` 一致性 |
| `SPIFFS 挂载失败: ESP_ERR_NOT_FOUND` | SPIFFS 组件未链接 | 检查 CMake `PRIV_REQUIRES` 是否包含 `spiffs` |
| 编译时报 implicit declaration | 缺 `#include "esp_spiffs.h"` | 检查 include |
| 链接时报 undefined reference | CMake 缺 `spiffs` 依赖 | 加 `PRIV_REQUIRES spiffs` |
| 分区表解析失败 | Flash 大小和分区表不匹配 | 确认 `CONFIG_ESPTOOLPY_FLASHSIZE="16MB"` |

### 11.9 生产环境注意

当前代码中 `format_if_mount_failed = true` 只是开发阶段的便利。**产品固件一定要改成 `false`**，否则 SPIFFS 校验失败时会自动格式化，用户配置全部丢失。

---

## 知识点脑图

```
SPIFFS 配置文件存储
│
├─ 文件系统基础
│   ├─ SPIFFS 原理（log-structured, 掉电安全, 无目录）
│   └─ 与 FAT / NVS 的适用场景对比
│
├─ 分区表配置
│   ├─ partitions.csv 格式
│   ├─ data/spiffs 类型
│   └─ 分区大小规划
│
├─ 挂载流程
│   ├─ esp_vfs_spiffs_conf_t 配置
│   ├─ esp_vfs_spiffs_register()
│   ├─ esp_spiffs_info() 查询
│   └─ 首次启动自动格式化
│
├─ POSIX 文件操作
│   ├─ fopen/fclose/fread/fwrite/fprintf
│   └─ remove/rename/stat
│
├─ 序列化格式
│   ├─ cJSON（推荐）
│   └─ 自定义文本/二进制
│
├─ 工程集成
│   ├─ menuconfig SPIFFS 选项
│   ├─ 首次启动初始化流程
│   └─ 配置文件读取 → 应用参数
│
└─ 最佳实践
    ├─ 写入频率控制（防磨损）
    ├─ format_if_mount_failed 开关
    ├─ 文件描述符泄漏防范
    └─ 生产/开发配置分离
```
