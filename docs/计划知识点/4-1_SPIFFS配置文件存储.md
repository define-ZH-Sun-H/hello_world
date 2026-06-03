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

SPIFFS 挂载后，通过 ESP-IDF 的 VFS（Virtual File System）层把标准 C 的文件函数（fopen、fread 等）重定向到 Flash 上。你不需要学任何新 API，用你学 C 语言时就见过的 `<stdio.h>` 函数就行。

### 5.1 核心概念：FILE 指针

```c
FILE *f = fopen("/spiffs/config.txt", "r");
```

- `fopen` 返回一个 `FILE *` 指针，后续所有操作都通过这个指针
- 第一个参数是文件路径，**必须带 `/spiffs/` 前缀**（这是挂载时的 `base_path`）
- 第二个参数是**打开模式**，初学者最容易搞错的地方

### 5.2 打开模式详解

| 模式 | 读？ | 写？ | 文件不存在？ | 文件已存在？ |
|------|------|------|-------------|-------------|
| `"r"` | ✅ | ❌ | 返回 NULL（报错） | 正常读取 |
| `"w"` | ❌ | ✅ | 创建新文件 | **清空内容**重新写 |
| `"a"` | ❌ | ✅ | 创建新文件 | 末尾追加 |
| `"r+"` | ✅ | ✅ | 返回 NULL | 覆盖写（不 truncate） |
| `"w+"` | ✅ | ✅ | 创建新文件 | **清空内容**重新写 |
| `"a+"` | ✅ | ✅ | 创建新文件 | 末尾追加 |

**初学者最容易犯的错：** 用 `"w"` 打开一个只想读的文件——文件内容会被清空。想读取现有文件一定用 `"r"`。

### 5.3 核心函数 · 逐个拆解

#### fopen / fclose — 开和关

```c
FILE *f = fopen("/spiffs/config.txt", "r");
if (f == NULL) {
    // 文件不存在或打开失败，这里要处理
    ESP_LOGE(TAG, "打开文件失败");
    return;
}

// ... 读写操作 ...

fclose(f);  // 一定要关！否则泄漏文件描述符
```

**黄金法则：** `fopen` 和 `fclose` 要像括号一样配对出现。写完 `fopen` 立刻写 `fclose`，再往中间填代码。

#### fread — 读二进制数据

```c
char buf[256];
FILE *f = fopen("/spiffs/config.txt", "r");
if (f == NULL) return;

size_t read_count = fread(buf, 1, sizeof(buf), f);
// read_count 是实际读到的字节数
// 返回值可能小于 sizeof(buf)，说明文件没那么大
buf[read_count] = '\0';  // 如果当文本用，记得加字符串结尾

fclose(f);
```

**参数含义：** `fread(往哪里存, 每个元素多大, 最多读几个, 文件指针)`
`fread(buf, 1, sizeof(buf), f)` = "每次读 1 字节，最多读 256 次"

#### fwrite — 写二进制数据

```c
const char *text = "Hello SPIFFS!";
FILE *f = fopen("/spiffs/config.txt", "w");
if (f == NULL) return;

size_t written = fwrite(text, 1, strlen(text), f);
if (written != strlen(text)) {
    ESP_LOGE(TAG, "写入字节数不对");
}

fclose(f);
```

#### fprintf — 按格式写文本（最方便）

```c
FILE *f = fopen("/spiffs/config.txt", "w");
if (f == NULL) return;

fprintf(f, "ssid=%s\n", wifi_ssid);
fprintf(f, "password=%s\n", wifi_password);
fprintf(f, "brightness=%d\n", brightness);

fclose(f);
```

`fprintf` 和 `printf` 用法一样，只是输出目标从屏幕换成了文件。

#### fgets — 逐行读文本

```c
FILE *f = fopen("/spiffs/config.txt", "r");
if (f == NULL) return;

char line[128];
while (fgets(line, sizeof(line), f) != NULL) {
    // 每次读一行，读到换行符或 buffer 满为止
    // 行尾的 '\n' 也会被读进来，通常要去掉
    size_t len = strlen(line);
    if (line[len - 1] == '\n') line[len - 1] = '\0';
    
    printf("读到: %s\n", line);
}

fclose(f);
```

#### fseek / ftell — 跳转位置和查询位置

```c
// 跳到文件开头
fseek(f, 0, SEEK_SET);

// 跳到文件末尾（通常用来算文件大小）
fseek(f, 0, SEEK_END);
long file_size = ftell(f);  // 从文件头到当前位置的字节数
rewind(f);                  // 跳回开头，等价于 fseek(f, 0, SEEK_SET)
```

`SEEK_SET` = 从文件头算，`SEEK_CUR` = 从当前位置算，`SEEK_END` = 从文件尾算。

### 5.4 完整示例：读写配置文件（初学者模板）

```c
/* ── 写入 ── */
void save_config(void)
{
    FILE *f = fopen("/spiffs/config.txt", "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "无法创建配置文件");
        return;
    }
    
    fprintf(f, "wifi_ssid=MyHomeWiFi\n");
    fprintf(f, "wifi_pass=12345678\n");
    fprintf(f, "brightness=80\n");
    
    fclose(f);  // 这里才真正写入 Flash
    ESP_LOGI(TAG, "配置已保存");
}

/* ── 读取 ── */
void load_config(void)
{
    FILE *f = fopen("/spiffs/config.txt", "r");
    if (f == NULL) {
        ESP_LOGI(TAG, "配置文件不存在，使用默认值");
        return;  // 首次启动，文件还不存在，不是错误
    }
    
    char line[64];
    while (fgets(line, sizeof(line), f) != NULL) {
        // 去掉行尾的换行符
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';
        
        // 解析 key=value
        char key[32], value[32];
        if (sscanf(line, "%31[^=]=%31s", key, value) == 2) {
            if (strcmp(key, "brightness") == 0) {
                brightness = atoi(value);
            }
            // 解析更多字段...
        }
    }
    
    fclose(f);
}
```

### 5.5 其他实用函数

| 函数 | 作用 | 示例 |
|------|------|------|
| `remove()` | 删除文件 | `remove("/spiffs/config.txt")` |
| `rename()` | 重命名 | `rename("/spiffs/tmp.txt", "/spiffs/config.txt")` |
| `stat()` | 获取文件信息 | `stat("/spiffs/config.txt", &st)` → `st.st_size` 是文件大小 |

**rename 的使用场景：** 写入新配置时，先写到 `config.tmp`，成功后 `rename` 覆盖旧文件。这样即使写入过程中掉电，也不会损坏现有配置（原子写入技巧）。

### 5.6 初学者最容易踩的坑

| ❌ 错误 | ✅ 正确 | 原因 |
|---------|---------|------|
| 忘记 `fclose()` | 配对 `fopen/fclose` | SPIFFS 只有 `fclose` 时才真正往 Flash 写数据 |
| 用 `"w"` 打开现有文件 | 确认意图：读用 `"r"`，写用 `"w"` | `"w"` 会清空文件！ |
| `fgets` 不处理 `\n` | `if(line[len-1]=='\n') line[len-1]='\0'` | 否则比较字符串时末尾有个看不见的换行 |
| 假设一次 `fread` 能读完 | 检查返回值，可能只读了一部分 | `fread` 返回值才是实际读到的字节数 |
| 用 `fflush` 以为刷入了 Flash | 只有 `fclose` 才保证写入 Flash | `fflush` 只刷到 RAM 缓冲区 |
| 在 SPIFFS 上 `mkdir` | 别用，SPIFFS 不支持目录 | 所有文件都在一个平层里 |

---

## 6. 配置文件格式选择

### 6.1 选择之前：先理解你的数据

在嵌入式设备上存配置，本质上就是解决一个问题：**把内存中的结构体/变量变成字节存起来，下次开机再变回来**。

选择哪种格式，取决于你有几个配置项：

| 配置项数量 | 推荐方式 | 理由 |
|-----------|---------|------|
| 1-5 个 | 自定义 KEY=VALUE | 代码最少，一眼看懂 |
| 5-20 个 | cJSON | 结构化，可扩展，人类可读 |
| 只有几个数字 | NVS（ESP-IDF 内置） | 连 SPIFFS 都不用，API 最简 |
| 大量结构化数据 | cJSON | 可嵌套、可加字段不破坏兼容性 |

### 6.2 三种方案详细对比

| 特性 | cJSON（推荐） | 自定义 KEY=VALUE | NVS |
|------|-------------|-----------------|-----|
| 学习成本 | 中等，要学几个 API | 低，sscanf 就行 | 低，API 极其简单 |
| 代码量 | 中等（序列化/反序列化） | 少 | 极少 |
| 人类可读 | ✅ 格式化后一目了然 | ✅ 勉强可读 | ❌ 二进制，看不到 |
| 增加字段 | ✅ 加一个 JSON key 就行 | ✅ 加一行 | ✅ 加一个 key |
| 嵌套结构 | ✅ 支持对象的对象 | ❌ 扁平 KV | ❌ 扁平 KV |
| 解析开销 | ⚠️ 需要 JSON 解析器（约 4KB ROM） | 极小 | 极小 |
| RAM 占用 | ⚠️ JSON 字符串需要 buffer（至少几百字节） | 很小 | 很小 |

### 6.3 什么是 JSON（给初学者的解释）

JSON 就是一套规则，把数据写成"名字：值"的格式：

```json
{
    "ssid": "MyHomeWiFi",
    "password": "12345678",
    "brightness": 80,
    "sensor": {
        "temp_offset": 0.5,
        "update_interval_s": 60
    }
}
```

- 冒号左边叫 **key**（键），右边叫 **value**（值）
- 值可以是：字符串（双引号）、数字（不带引号）、布尔、数组、对象（嵌套的 `{}`）
- 用 `cJSON` 库就是把这些文本和 C 语言的 `char*` / `int` / `double` 互相转换

### 6.4 cJSON 核心函数速查

初学者只需要记 6 个函数：

```c
// ---- 创建 / 构造（内存 → JSON 文本） ----
cJSON *root = cJSON_CreateObject();                     // 建一个空对象 {}
cJSON_AddStringToObject(root, "ssid", "MyWiFi");        // 加字符串
cJSON_AddNumberToObject(root, "brightness", 80);        // 加数字
cJSON_AddBoolToObject(root, "enable_led", true);        // 加布尔

char *json_str = cJSON_Print(root);                     // 格式化输出（含换行和缩进）
char *json_str = cJSON_PrintUnformatted(root);          // 紧凑输出（省空间）

// ---- 解析（JSON 文本 → 内存） ----
cJSON *root = cJSON_Parse(json_str);                    // 解析 JSON 文本
cJSON *ssid = cJSON_GetObjectItem(root, "ssid");        // 按 key 取值
if (cJSON_IsString(ssid)) {
    // 用 ssid->valuestring 拿到字符串
}

// ---- 清理 ----
cJSON_Delete(root);  // 释放所有 cJSON 对象
free(json_str);       // cJSON_Print 返回的字符串要手动 free
```

### 6.5 cJSON 完整读写示例（带错误处理）

```c
/* ===========================================================
 * 写入配置到 SPIFFS（cJSON 版）
 * =========================================================== */
esp_err_t config_save(void)
{
    // 1. 在内存中构建 JSON 对象
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) return ESP_ERR_NO_MEM;
    
    cJSON_AddStringToObject(root, "ssid",         wifi_ssid);
    cJSON_AddStringToObject(root, "password",     wifi_password);
    cJSON_AddNumberToObject(root, "brightness",   display_brightness);
    cJSON_AddNumberToObject(root, "temp_offset",  temp_calib_offset);
    
    // 2. 序列化为文本
    char *json_text = cJSON_Print(root);     // ← 这行分配了内存
    if (json_text == NULL) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }
    
    // 3. 写入 SPIFFS
    FILE *f = fopen("/spiffs/config.json", "w");
    if (f == NULL) {
        free(json_text);
        cJSON_Delete(root);
        return ESP_ERR_NOT_FOUND;
    }
    
    fprintf(f, "%s", json_text);
    fclose(f);
    
    // 4. 清理内存（容易忘！）
    free(json_text);
    cJSON_Delete(root);
    
    ESP_LOGI(TAG, "配置已保存");
    return ESP_OK;
}

/* ===========================================================
 * 从 SPIFFS 读取配置（cJSON 版）
 * =========================================================== */
esp_err_t config_load(void)
{
    // 1. 从 SPIFFS 读出全部文本
    FILE *f = fopen("/spiffs/config.json", "r");
    if (f == NULL) {
        ESP_LOGI(TAG, "配置文件不存在，使用默认配置");
        return ESP_ERR_NOT_FOUND;    // 首次启动正常，不是错误
    }
    
    // 2. 读文件到 buffer
    char buf[1024];
    size_t read_len = fread(buf, 1, sizeof(buf) - 1, f);
    buf[read_len] = '\0';
    fclose(f);
    
    // 3. 解析 JSON
    cJSON *root = cJSON_Parse(buf);
    if (root == NULL) {
        const char *err = cJSON_GetErrorPtr();
        ESP_LOGE(TAG, "JSON 解析出错: %s", err ? err : "未知");
        return ESP_ERR_INVALID_ARG;
    }
    
    // 4. 逐个提取字段（要检查是否存在、类型是否正确）
    cJSON *ssid = cJSON_GetObjectItem(root, "ssid");
    if (cJSON_IsString(ssid) && ssid->valuestring != NULL) {
        strlcpy(wifi_ssid, ssid->valuestring, sizeof(wifi_ssid));
    } else {
        ESP_LOGW(TAG, "配置缺少 ssid，使用默认值");
    }
    
    cJSON *brightness = cJSON_GetObjectItem(root, "brightness");
    if (cJSON_IsNumber(brightness)) {
        display_brightness = brightness->valuedouble;  // 稳妥：就算是整数也拿 double
    }
    
    cJSON *offset = cJSON_GetObjectItem(root, "temp_offset");
    if (cJSON_IsNumber(offset)) {
        temp_calib_offset = offset->valuedouble;
    }
    
    // 5. 清理
    cJSON_Delete(root);
    
    ESP_LOGI(TAG, "配置已加载");
    return ESP_OK;
}
```

### 6.6 自定义 KEY=VALUE 格式（轻量替代方案）

如果你只有 2-3 个配置项，用 JSON 反而重了。直接用 `fprintf` + `sscanf` 更简单：

```c
/* ── 写入 ── */
void save_simple_config(void)
{
    FILE *f = fopen("/spiffs/config.txt", "w");
    if (f == NULL) return;
    
    fprintf(f, "ssid=%s\n", wifi_ssid);
    fprintf(f, "brightness=%d\n", display_brightness);
    fclose(f);
}

/* ── 读取 ── */
void load_simple_config(void)
{
    FILE *f = fopen("/spiffs/config.txt", "r");
    if (f == NULL) return;
    
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char key[32], val[64];
        if (sscanf(line, "%31[^=]=%63s", key, val) != 2) continue;
        
        if      (strcmp(key, "ssid")       == 0) strlcpy(wifi_ssid, val, sizeof(wifi_ssid));
        else if (strcmp(key, "brightness") == 0) display_brightness = atoi(val);
    }
    fclose(f);
}
```

**优点：** 不依赖任何库，C 标准库就够了。**缺点：** 值里不能有等号和换行。

### 6.7 cJSON 初学者常犯错误

| ❌ 错误 | ✅ 正确 | 为什么 |
|---------|---------|--------|
| `free(root)` | `cJSON_Delete(root)` | root 是 cJSON 内部结构，不是 malloc 的直接返回 |
| 忘记 `free(json_text)` | `cJSON_Print()` 后配对 `free()` | `cJSON_Print` 内部 malloc 了，你不 free 就泄漏 |
| 不检查 `cJSON_GetObjectItem` 返回值 | `if(cJSON_IsString(item))` 再取值 | JSON 文件可能缺少某个字段或被手动改坏 |
| 假设 JSON 值一定是字符串 | 用 `cJSON_IsNumber` / `cJSON_IsString` 检查 | 别人可能把 `"80"` 写成字符串而不是数字 |
| 每次写配置都 parse JSON 再 create | 读写分离，读用 parse，写用 create | parse 和 create 互为逆向，别混着用 |

### 6.8 选择建议

```
只有 < 5 个简单配置项
    └→ 用自定义 KEY=VALUE（省一个库依赖）

有 5+ 个配置项或嵌套结构
    └→ 用 cJSON（可读性好，可扩展）

只有几个计数器/开关量
    └→ 用 NVS（连 SPIFFS 都不用，API 更简单）
```

**本项目的选择：** 因为后续会有 WiFi 配置（ssid/password）、校准参数（温度偏移、传感器类型）、显示设置（亮度、翻转）等 10+ 个配置项，所以用 **cJSON**。

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
