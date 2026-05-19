# ESP-IDF CMake 配置详解

> 适用项目：hello_world（ESP32-S3, ESP-IDF 5.5）

---

## 文件结构

```
hello_world/
├── CMakeLists.txt              # 顶层构建入口
├── sdkconfig                   # Kconfig 图形化配置产物
├── main/
│   └── CMakeLists.txt          # 主应用组件注册
└── components/
    └── bsp/
        ├── CMakeLists.txt      # 板级支持包组件注册
        ├── iic/                # I2C 驱动
        │   ├── iic.c
        │   └── iic.h
        └── oled/               # OLED 显示驱动
            ├── oled.c
            ├── oled.h
            └── oledfont.h
```

---

## 1. 顶层 CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# "精简"构建。只包含最小组件集：main 及其依赖。
idf_build_set_property(MINIMAL_BUILD ON)

project(hello_world)
```

| 行 | 作用 |
|---|---|
| `cmake_minimum_required(VERSION 3.16)` | 指定 CMake 最低版本。ESP-IDF 5.5 自带的 CMake ≥ 3.16 |
| `include($ENV{IDF_PATH}/...)` | 引入 ESP-IDF 构建模块。`IDF_PATH` 指向 `/home/devcontainers/esp-idf`，这行把所有 IDF 构建规则（编译、链接、烧录、监控）注入项目 |
| `idf_build_set_property(MINIMAL_BUILD ON)` | **精简构建**：只编译 main 及其直接/间接依赖的组件，跳过 IDF 中 200+ 未使用组件，大幅缩短编译时间 |
| `project(hello_world)` | CMake 标准函数，设定项目名称。最终生成 `hello_world.bin` |

> 顶层文件是 ESP-IDF 项目的固定模板，除项目名和 MINIMAL_BUILD 外极少变动。

---

## 2. main/CMakeLists.txt

```cmake
idf_component_register(SRCS "hello_world_main.c"
                       PRIV_REQUIRES spi_flash bsp
                       INCLUDE_DIRS "")
```

| 参数 | 含义 |
|---|---|
| `SRCS` | 源文件列表 |
| `PRIV_REQUIRES` | **私有依赖**：编译和链接时需要 `spi_flash` 和 `bsp`。私有依赖不向外传递，其他组件 `REQUIRES main` 时不会继承这两个依赖 |
| `INCLUDE_DIRS` | 附加头文件搜索路径（空=只搜索当前目录） |

### REQUIRES vs PRIV_REQUIRES

| 关键字 | 传递性 | 适用场景 |
|---|---|---|
| `REQUIRES` | 公开，会向外传递 | 本组件的头文件中直接引用了其他组件的 API |
| `PRIV_REQUIRES` | 私有，不向外传递 | 只在 `.c` 实现文件中引用其他组件，头文件不暴露 |

当下层的 `bsp` 或 `driver` 升级时，`PRIV_REQUIRES` 不会触发上层组件的重编译，减少级联影响。

---

## 3. components/bsp/CMakeLists.txt

```cmake
set(src_dirs
            iic
            oled)

set(include_dirs
            iic
            oled)

set(requires
            driver)

idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})
```

| 参数 | 含义 |
|---|---|
| `SRC_DIRS` | 递归扫描子目录下所有 `.c` 文件，无需逐一提及源文件名 |
| `INCLUDE_DIRS` | 头文件搜索路径。代码中 `#include "iic.h"` 或 `#include "oled.h"` 时从这两目录查找 |
| `REQUIRES` | 依赖 ESP-IDF 的 `driver` 组件（内含 GPIO、I2C、SPI、LEDC 等外设驱动 API） |

### 简化写法

等价但不推荐（不利于后续扩展）：

```cmake
idf_component_register(SRC_DIRS "iic" "oled"
                       INCLUDE_DIRS "iic" "oled"
                       REQUIRES driver)
```

### 条件编译扩展示例

```cmake
set(src_dirs iic oled)
set(include_dirs iic oled)
set(requires driver)

# 仅在 menuconfig 启用 OLED 时加入
if(CONFIG_BSP_OLED_ENABLE)
    list(APPEND src_dirs oled)
endif()

idf_component_register(...)
```

---

## 4. sdkconfig — 构建的心脏

`sdkconfig` 不是 CMake 文件，但与 CMake 构建系统紧密配合。

- **来源**：`idf.py menuconfig` 图形化配置生成
- **作用**：定义芯片型号、功能开关、内存分区、外设配置等约 2000 项参数
- **修改方式**：`idf.py menuconfig`（推荐）或直接编辑（不推荐）
- **版本控制**：不推荐提交到 git，可通过 `sdkconfig.defaults` 维护基线配置

### 当前项目关键配置

| 配置 | 典型值 | 说明 |
|---|---|---|
| `CONFIG_IDF_TARGET` | `esp32s3` | 目标芯片 |
| `CONFIG_PARTITION_TABLE_*` | 预设 | Flash 分区表布局 |
| `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ` | 240 | CPU 主频 (MHz) |
| `CONFIG_FREERTOS_*` | 默认 | FreeRTOS 内核参数 |

---

## 5. 完整构建流程

```
idf.py build
  │
  ├─ cmake -B build/
  │    ├─ include $IDF_PATH/tools/cmake/project.cmake   ← 注入 IDF 构建规则
  │    ├─ 加载 sdkconfig                                   ← 芯片型号、功能开关
  │    ├─ 扫描 components/ 目录                             ← 发现自定义组件 (bsp)
  │    ├─ 生成 compile_commands.json                       ← F12 代码导航依赖
  │    └─ 生成 build.ninja                                 ← Ninja 增量构建规则
  │
  └─ ninja -C build/          ← 真正执行编译
       ├─ bsp/iic/iic.c       → iic.o
       ├─ bsp/oled/oled.c     → oled.o
       ├─ hello_world_main.c  → hello_world_main.o
       └─ 链接 → hello_world.bin
```

---

## 6. 各文件责任边界

```
CMakeLists.txt              → 项目挂接到 IDF 构建系统
main/CMakeLists.txt         → 注册应用代码，声明对外依赖
components/bsp/CMakeLists.txt → 注册组件，声明对驱动层的依赖
sdkconfig                   → 芯片型号、功能开关、内存/外设参数
```

---

## 7. 当前项目的依赖链路

```
hello_world_main.c
  ├─ PRIV_REQUIRES spi_flash  (IDF 内置 — Flash 读写)
  └─ PRIV_REQUIRES bsp        (自定义 — 板级硬件抽象)
       └─ REQUIRES driver     (IDF 内置 — GPIO/I2C/SPI 等外设驱动)
            └─ ... 其他 IDF 内核层组件
```

```
作者: Claude Code
时间: 2026-05-19
```
