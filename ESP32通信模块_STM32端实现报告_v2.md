# ESP32 通信模块 STM32 端 · 实现报告

> **固件版本：** v2  
> **完成日期：** 2026-05-28  
> **涉及分支：** touchgfx  

---

## 1. 实现概述

在 STM32G474 主控上完整实现了与 ESP32-C3 的 SPI 全双工通信，遵循 `ESP32-C3通信接口规范_v2.0.md` 中定义的 128 字节数据包协议。

**架构：三层分离**

```
Task/app_esp_task.c/h       ← 通信任务 (500ms 心跳 + 命令路由 + 响应处理)
Task/app_esp_protocol.c/h   ← 协议层 (组包/解包/命令码宏/格式化)
Drivers/ZeMCU-G4/driver_esp32.c/h ← 驱动层 (SPI4 收发/CS控制/硬复位)
```

---

## 2. 文件清单

| 文件 | 类型 | 行数(约) | 说明 |
|------|------|----------|------|
| `Drivers/ZeMCU-G4/driver_esp32.h` | 新建 | 41 | 驱动层头文件 |
| `Drivers/ZeMCU-G4/driver_esp32.c` | 新建 | 67 | SPI4 收发 + CS 控制 + 硬复位 |
| `Task/app_esp_protocol.h` | 新建 | 143 | 命令码宏 + 组包/解包/格式化 API 声明 |
| `Task/app_esp_protocol.c` | 新建 | 139 | 组包/解包实现 + 温度/进度格式化 + 状态映射 |
| `Task/app_esp_task.h` | 新建 | 55 | 任务 API 声明 + 全局标志位 extern |
| `Task/app_esp_task.c` | 新建 | 282 | ESP_Task 主循环 + 轮询分时 + 控制命令 + 响应处理 |
| `Core/Src/app_freertos.c` | 修改 | +12 | 新增 include/队列/任务属性/任务创建；修复 HostMotion 重复创建 |
| `MDK-ARM/pnp_1.uvprojx` | 修改 | +36 | 添加 6 个新文件到编译列表 |

**代码统计：新建 ~727 行，修改 ~48 行。**

---

## 3. 新增标志位（供后续模块复用）

以下全局变量定义于 `Task/app_esp_task.c`，通过 `Task/app_esp_task.h` 对外暴露：

| 变量 | 类型 | 默认值 | 含义 |
|------|------|--------|------|
| `g_esp_wifi_enabled` | `uint8_t` | 0 | WiFi 功能开关 (0=关, 1=开) — 收到 `0x20 0x01` 后置 1 |
| `g_esp_wifi_connected` | `uint8_t` | 0 | WiFi 实际连接状态 (0=未连, 1=已连) — ESP `0xF2` 回传 |
| `g_esp_fault_code` | `uint8_t` | 0x00 | 最新故障码 (0x00=无故障, 0x01=WiFi失败, 0x02=Web失败...) |
| `g_esp_last_rx_tick` | `uint32_t` | 0 | 最后一次收到 ESP 有效响应的系统 tick |

**复用建议：** 后续模块如需判断"设备是否可远程访问"，检查 `g_esp_wifi_enabled && g_esp_wifi_connected` 即可。不要在外部重复定义。

---

## 4. 新增 FreeRTOS 对象

| 对象 | 类型 | 深度 | 句柄 | 作用 |
|------|------|------|------|------|
| `esp_cmd_queue` | `osMessageQueueId_t` | 8 | 全局 | 其他任务 → ESP_Task 的控制命令 |
| `ESP_Task` | `osThreadId_t` | — | 通过 `osThreadNew` 创建 | 通信任务，栈 512B，Normal 优先级 |

---

## 5. 数据流说明

### 5.1 ST → ESP 数据推送

ESP_Task 以 500ms 为周期，**轮询分时**发送 4 个数据字段（每次 SPI 传输只发一个）：

| 轮次 | 子命令 | 内容 | 数据来源 |
|------|--------|------|----------|
| 0 | `0x10 0x01` | 贴片进度 "32/50" | `now_SMT` / `total_SMT` (Data_Transfer.c) |
| 1 | `0x10 0x02` | 贴片状态枚举 | `if_now_SMT` + `if_DOWNLOAD_READY` + HeaterState 综合判断 |
| 2 | `0x10 0x03` | 加热台开关 "1"/"0" | `Heater_GetCurrentStatus().state` |
| 3 | `0x10 0x04` | 加热台温度 "85.3" | `Heater_GetCurrentStatus().cur_temp`，变化 >0.5°C 才发送 |

WiFi 开启前只发送心跳包（全零），不推送数据。

### 5.2 ST → ESP 控制命令

通过 `ESP_SendCommand()` 接口发送到 `esp_cmd_queue`，ESP_Task 优先处理：

```
ESP_SendCommand(ESP_CMD_WIFI_ON)   → 发送 0x20 0x01 (打开 WiFi)
ESP_SendCommand(ESP_CMD_WIFI_OFF)  → 发送 0x20 0x02 (关闭 WiFi)
ESP_SendCommand(ESP_CMD_QUERY_FAULT) → 发送 0x30 0x01 (查询故障)
ESP_SendCommand(ESP_CMD_QUERY_WIFI)  → 发送 0x30 0x02 (查询 WiFi)
```

### 5.3 ESP → ST 响应处理

每次 SPI 传输后自动解析 ESP 回传的 128 字节：

| 响应类型 | 处理 |
|----------|------|
| `0x00` (空闲) | 忽略，清零无响应计数 |
| `0xF1` (故障) | 更新 `g_esp_fault_code`，PrintDebug |
| `0xF2` (WiFi状态) | 更新 `g_esp_wifi_connected`，PrintDebug |
| `0xFF` (复合) | 同时更新故障码和 WiFi 状态 |
| `0xFE` (版本) | PrintDebug 输出版本号 |

---

## 6. 关键设计决策

### 6.1 复用现有变量

ESP 模块**不创建**独立的数据缓存，直接读取项目中已有的全局变量：

- `now_SMT`, `total_SMT`, `if_now_SMT`, `Temp`, `if_DOWNLOAD_READY` → 来自 `Data_Transfer.c`
- `HeaterStatus_t` → 通过 `Heater_GetCurrentStatus()` 获取
- `g_debug_mutex` + `PrintDebug` → 共用日志系统

结果：**新增 RAM 占用 < 350 字节**（tx_buf 128B + rx_buf 128B + 状态变量 7B + 队列开销）。

### 6.2 零 sprintf 设计

协议层所有格式化使用整数运算实现（`_utoa` 内部函数），避免引入 `<stdio.h>` 的 ~800 字节栈开销。温度格式化 `ESP_FormatTemp` 栈占用 < 40 字节。

### 6.3 WiFi 默认关闭

遵循接口规范：ESP 上电后 WiFi 默认关闭，需主控显式发送 `0x20 0x01` 才启动。这是出于节能和安全的考虑——贴片机在不需要远程监控时不应暴露网络服务。

### 6.4 温度变化过滤

加热台温度仅在变化超过 0.5°C 时才发送更新包，避免 SPI 总线上传输冗余的相同温度值。

---

## 7. Bug 修复记录

| 问题 | 位置 | 修复 |
|------|------|------|
| HostMotion 任务重复创建 | `app_freertos.c:216-217` | 删除无句柄的 `osThreadNew`，保留 `hostMotionTaskHandle = osThreadNew(...)` |
| SPI4_CS/C3RESET 上电默认低电平 | `gpio.c` 配置 | `ESP_GPIO_Init()` 中将 CS 和 RST 置高 |

---

## 8. 待办事项 / 后续建议

1. **Keil 编译验证** — 当前修改未经过 Keil 编译器验证（需在目标环境编译），建议在 Keil IDE 中 Rebuild All
2. **ESP32 联调** — 需 ESP32 端也按照 `ESP32-C3通信接口规范_v2.0.md` 实现后，两端联调验证
3. **上位机集成** — 可通过上位机指令触发 `ESP_SendCommand(ESP_CMD_WIFI_ON)` 开启 Web 监控
4. **GUI 集成** — TouchGFX 界面可添加 WiFi 状态指示器，读取 `g_esp_wifi_connected` 显示
5. **加热台状态映射细化** — 当前 `_send_data_field(RR_SMT_STATUS)` 中的加热状态判断使用 `hs.state > 0 && hs.state < 5`，可后续根据实际加热台协议调整

---

## 9. 版本兼容性

| 项目 | 版本 |
|------|------|
| 通信协议版本 | v2 (对应 `ESP32-C3通信接口规范_v2.0.md`) |
| 命令码空间 | 已用 4 数据 + 2 控制 + 3 查询，剩余充足 |
| 向后兼容 | 协议层命令码通过宏定义，新增字段无需修改已有代码 |

---

*— 报告结束 —*
