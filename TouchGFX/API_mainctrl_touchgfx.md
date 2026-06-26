# TouchGFX ↔ 主控 对接文档（API）

> 本文档只面向**主控端 AI / 开发者**，用于对接 TouchGFX GUI 的消息通信接口。
> 目标是：主控端只通过统一消息中枢（`Data_Transfer`）与 GUI 通信，不直接耦合具体屏幕实现。

## 1. 通信模型

```
主控系统任务
   │
   ▼
DT_Notify*()
   │
   ▼
dataTransferQueue
   │
   ▼
Model::processQueue()
   │
   ▼
Presenter -> View
```

- **System → GUI**：主控端调用 `DT_Notify*()`
- **GUI → System**：Presenter 通过 `Model::sendCommand()` 发送命令

## 2. 当前可用 System → GUI API

### 2.1 贴片状态
```c
void DT_NotifySMTStatus(uint8_t is_smt);
```

### 2.2 温度
```c
void DT_NotifyTemp(uint16_t temp); // 单位 0.1°C
```

### 2.3 导入/下载状态
```c
void DT_NotifyDownloadStatus(uint8_t status);
```

### 2.4 贴片进度（高频可用）
```c
void DT_NotifySMTProgress(uint8_t current, uint8_t total);
```

### 2.5 电机复位完成
```c
void DT_NotifyMotorResetDone(void);
```

### 2.6 电机默认速度（低频）
```c
void DT_NotifyMotorSpeed(uint16_t speed);
```

### 2.7 系统短日志（推荐）
```c
void DT_NotifyLogText(uint8_t code, uint8_t param);
```

### 2.8 通用消息（旧接口）
```c
void DT_NotifyCustom(uint8_t code, uint8_t param);
```

## 3. 当前推荐的 LOG 使用方式

**不要直接发长字符串到 GUI 队列。**

推荐：

- 用 `DT_NotifyLogText(code, param)`
- GUI 端会显示为：`code:param`

后续可扩展“短模板映射表”（GUI 侧根据 code 映射中文提示），但当前版本先保持最小闭环。

## 4. MotorSpeed / Progress 显示

### MotorSpeed
- 主控启动后调用一次：
```c
DT_NotifyMotorSpeed(speed);
```
- GUI `Screen_HOME` 会写入 `MotorSpeed` 文本框，且只在值变化时刷新。

### Progress
- 高频进度更新使用：
```c
DT_NotifySMTProgress(current, total);
```
- GUI `Screen_HOME` 会刷新 `circleProgress` 及数字显示。

## 5. GUI → System 命令（保留，待主控端接入）

```c
DT_CMD_MOTOR_MOVE
DT_CMD_MOTOR_STOP
DT_CMD_MOTOR_HOME
DT_CMD_SMT_START
DT_CMD_SMT_PAUSE
DT_CMD_HEATER_SET
DT_CMD_SYSTEM_RESET
DT_CMD_CUSTOM
```

这些命令当前由 `Data_Transfer.c` 的路由表维护，但具体业务函数仍需主控端实现。

## 6. 对接约束（主控端必读）

1. **队列深度有限**：`dataTransferQueue` 默认 16 深度，避免高频阻塞写入。
2. **不传长字符串**：`DT_Msg_t` 只有 8 字节 raw payload。
3. **进度类通知可高频，日志类通知建议低频**。
4. **主控端不要直接访问 TouchGFX View/Presenter/Widget**。
5. **坐标、温度、进度、速度均使用整数单位**，避免浮点进队列。

### WiFi 状态
- 主控端通知 WiFi 连接状态使用：
```c
DT_NotifyWifiStatus(connected);  // connected: 0=断开, 1=已连接
```
- GUI `Screen_WIFI` / `PageTable` 会切换对应图标（`wifi_disc` / `wifi_connected` / 选中态）

> 注意：`DT_NotifyWifiStatus` 尚未在 `Data_Transfer.c` 中实现，需要主控端 AI 或 GUI 端 AI 补充消息类型 `DT_WIFI_STATUS` 及对应 handler。

## 7. 当前未完成项（留给主控端 AI）

- `Data_Transfer.c` 中 7 个 GUI→System handler 仍是 TODO 占位。
- 主控端需要补实际动作函数后，再让 `DT_Dispatch()` 真正触发系统行为。
- 需要新增 `DT_WIFI_STATUS` 消息类型及 `DT_NotifyWifiStatus()` 接口函数。

