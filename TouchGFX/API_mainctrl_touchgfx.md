# TouchGFX ↔ 主控 对接文档（API）

> 本文档只面向**主控端 AI / 开发者**，用于对接 TouchGFX GUI 的消息通信接口。
> 主控端只通过统一消息中枢（`Data_Transfer`）与 GUI 通信，不直接耦合具体屏幕实现。

## 1. 通信模型

```
主控系统任务
   │
   ▼
DT_Notify*()
   │
   ▼
dataTransferQueue (16深)
   │
   ▼
GUI 内部处理
```

- **System → GUI**：主控端调用 `DT_Notify*()`，数据写入 `dataTransferQueue`
- **GUI → System**：GUI 通过 `DT_SendCommand()` 发送命令到 `guiCmdQueue`，主控端调用 `DT_Dispatch()` 分发

## 2. System → GUI 通知 API

主控端调用以下函数，GUI 会自动更新对应显示。

| API | 参数 | 说明 |
|-----|------|------|
| `DT_NotifySMTStatus(uint8_t is_smt)` | 0=空闲, 1=贴片中 | 贴片状态 |
| `DT_NotifyTemp(uint16_t temp)` | 单位 0.1°C | 加烸台温度 |
| `DT_NotifyDownloadStatus(uint8_t status)` | 状态码 | 下载/导入状态 |
| `DT_NotifySMTProgress(uint8_t current, uint8_t total)` | 当前/总数 | 贴片进度（可高频） |
| `DT_NotifyMotorResetDone(void)` | 无 | 电机复位完成 |
| `DT_NotifyMotorSpeed(uint16_t speed)` | 速度值 | 电机默认速度（低频，启动时调用一次即可） |
| `DT_NotifyLogText(uint8_t code, uint8_t param)` | 日志代号/参数 | 系统短日志 |
| `DT_NotifyWifiStatus(uint8_t connected)` | 0=断开, 1=已连接 | WiFi 连接状态 |
| `DT_NotifyCustom(uint8_t code, uint8_t param)` | 通用标签 | 自定义消息（备用） |

## 3. GUI → System 命令 API

GUI 端发送的命令通过 `DT_SendCommand()` 进入 `guiCmdQueue`，主控端需要在路由表中接入 handler。

| 命令 | 消息体字段 | 说明 |
|------|----------|------|
| `DT_CMD_MOTOR_MOVE (0x10)` | `data.move.x/y/r` (×100mm) | 电机移动到目标坐标 |
| `DT_CMD_MOTOR_STOP (0x11)` | 无 | 电机急停 |
| `DT_CMD_MOTOR_HOME (0x12)` | 无 | 电机回零 |
| `DT_CMD_SMT_START (0x13)` | 无 | 启动贴片流程 |
| `DT_CMD_SMT_PAUSE (0x14)` | 无 | 暂停贴片 |
| `DT_CMD_HEATER_SET (0x15)` | `data.temp` (0.1°C) | 设置加烸台温度 |
| `DT_CMD_SYSTEM_RESET (0x16)` | 无 | 系统软复位 |
| `DT_CMD_WIFI_CTRL (0x17)` | `data.status`: 0=关, 1=开 | WiFi 开关控制 |
| `DT_CMD_CUSTOM (0x1F)` | `data.raw[8]` | 自定义命令 |

所有命令的 handler 当前为 TODO 占位，主控端需要实现具体业务函数。

## 4. 消息体定义

```c
typedef struct {
    DT_MsgType_t type;
    union {
        struct { uint8_t current, total; } progress;  // 贴片进度
        struct { int32_t x, y, r; } move;              // 电机移动 (×100mm)
        uint16_t temp;                                 // 温度 (0.1°C)
        uint8_t  status;                               // 状态标志
        uint8_t  raw[8];                               // 原始数据
        struct { uint8_t code; uint8_t param; } tag;   // 短日志标签
        uint16_t motor_speed;                          // 电机速度
    } data;
} DT_Msg_t;
```

## 5. 对接约束（主控端必读）

1. **队列深度有限**：`dataTransferQueue` 和 `guiCmdQueue` 各 16 深，避免高频阻塞写入。
2. **不传长字符串**：`DT_Msg_t` 只有 8 字节 payload，日志类用 `DT_NotifyLogText(code, param)` 短栀签方式。
3. **进度类可高频，日志类建议低频**。
4. **主控端不要直接访问 TouchGFX View/Presenter/Widget**。
5. **坐标、温度、进度、速度均使用整数单位**，避免浮点进队列。

## 6. 当前未完成项（留给主控端 AI）

- `Data_Transfer.c` 中所有 GUI→System 命令的 handler 均为 TODO 占位，主控端需要补实际业务函数。
- WiFi 扫描/连接功能待后续实现。
