# TouchGFX 端侧开发文档

> 本文档用于记录 TouchGFX 端侧当前已实现功能、关键文件、开发流程、更新历史、以及已知问题，便于后续 agent 继续开发。

## 1. 当前已实现功能

### 1.1 消息通信中枢（Data_Transfer）
- `TouchGFX/gui/include/gui/model/Data_Transfer.h`
- `TouchGFX/gui/src/model/Data_Transfer.c`

已实现：
- `DT_Msg_t` 统一消息体
- `dataTransferQueue`（System → GUI）
- `guiCmdQueue`（GUI → System）
- `DT_Notify*` 系列通知函数
- `DT_Dispatch()` 路由表（GUI → System）

新增：
- `DT_MOTOR_SPEED`
- `DT_LOG_TEXT`
- `DT_NotifyMotorSpeed()`
- `DT_NotifyLogText()`

### 1.2 Model / ModelListener 分发
- `TouchGFX/gui/src/model/Model.cpp`
- `TouchGFX/gui/include/gui/model/ModelListener.hpp`

已实现：
- `Model::processQueue()` 每帧消费 GUI 队列
- `Model::sendCommand()` Presenter 发命令入口
- `ModelListener` 新增：
  - `onNotifyMotorSpeed(uint16_t speed)`
  - `onNotifyLogText(uint8_t code, uint8_t param)`

### 1.3 Screen_HOME
- `TouchGFX/gui/src/screen_home_screen/Screen_HOMEView.cpp`
- `TouchGFX/gui/src/screen_home_screen/Screen_HOMEPresenter.cpp`

已实现：
- `MotorSpeed` 文本显示
- `progress` 圆环 + 数字显示
- 通过 Presenter 接收：
  - `onNotifySMTProgress`
  - `onNotifyMotorSpeed`
- 仅在数据变化时刷新

### 1.4 Screen_LOG
- `TouchGFX/gui/src/screen_log_screen/Screen_LOGView.cpp`
- `TouchGFX/gui/src/screen_log_screen/Screen_LOGPresenter.cpp`

已实现：
- `LogAddStr()` 追加日志到 `textArea1`
- 最大 100 行裁剪（`LOG_MAX_LINES`）
- 自动跟随最新日志（默认 `m_autoFollow=true`）
- Presenter 接收 `DT_LOG_TEXT`，转发到 `handleSystemLog(code, param)`

## 2. 关键文件位置

### 消息层
- `TouchGFX/gui/include/gui/model/Data_Transfer.h`
- `TouchGFX/gui/src/model/Data_Transfer.c`
- `TouchGFX/gui/include/gui/model/Model.hpp`
- `TouchGFX/gui/src/model/Model.cpp`
- `TouchGFX/gui/include/gui/model/ModelListener.hpp`

### Home
- `TouchGFX/gui/include/gui/screen_home_screen/Screen_HOMEView.hpp`
- `TouchGFX/gui/src/screen_home_screen/Screen_HOMEView.cpp`
- `TouchGFX/gui/include/gui/screen_home_screen/Screen_HOMEPresenter.hpp`
- `TouchGFX/gui/src/screen_home_screen/Screen_HOMEPresenter.cpp`

### LOG
- `TouchGFX/gui/include/gui/screen_log_screen/Screen_LOGView.hpp`
- `TouchGFX/gui/src/screen_log_screen/Screen_LOGView.cpp`
- `TouchGFX/gui/include/gui/screen_log_screen/Screen_LOGPresenter.hpp`
- `TouchGFX/gui/src/screen_log_screen/Screen_LOGPresenter.cpp`

### WIFI
- `TouchGFX/gui/include/gui/screen_wifi_screen/Screen_WIFIView.hpp`
- `TouchGFX/gui/src/screen_wifi_screen/Screen_WIFIView.cpp`
- `TouchGFX/gui/include/gui/screen_wifi_screen/Screen_WIFIPresenter.hpp`
- `TouchGFX/gui/src/screen_wifi_screen/Screen_WIFIPresenter.cpp`

### PageTable（页面选择容器）
- `TouchGFX/gui/include/gui/containers/PageTable.hpp`
- `TouchGFX/gui/src/containers/PageTable.cpp`

## 3. 当前开发结论

### MotorSpeed
- 主控端低频通知即可：
```c
DT_NotifyMotorSpeed(speed);
```

### Progress
- 主控端高频通知即可：
```c
DT_NotifySMTProgress(current, total);
```

### LOG
- 当前推荐短标签日志：
```c
DT_NotifyLogText(code, param);
```
- GUI 显示为 `code:param`
- 后续如果需要“模板化中文提示”，可在 GUI 侧扩展映射表，而不是在主控端拼字符串。

## 4. 更新历史

### 2026-06-24
- 新增 `DT_MOTOR_SPEED` / `DT_LOG_TEXT`
- `Screen_HOME` 支持 MotorSpeed 显示
- `Screen_LOG` 支持 100 行裁剪 + 自动跟随
- Presenter 层补齐 `onNotifyMotorSpeed` / `onNotifyLogText`
- 新增：
  - `TouchGFX/API_mainctrl_touchgfx.md`
  - `TouchGFX/README_touchgfx.md`

### 2026-06-25
- 新增 `Screen_WIFI` 页面（第 5 页，page_cnt=4）
- `PageTable` 支持 WIFI 图标四态切换（`wifi_disc` / `wifi_connected` / `wifi_disc_selc` / `wifi_connected_selc`）
- `PageTable::handleKey` 补齐 `case 4` → `gotoScreen_WIFIScreenNoTransition()`
- `Screen_LOG` 修复：清空日志后退出再进入页面不再重现旧内容（`s_firstBoot` 静态标志）
- 模拟器兼容性修复：`Data_Transfer.h` 条件编译 `cmsis_os2.h` / `touchgfx_sim_os_shim.h`

### 2026-06-26（BUG 修复）
- **BUG 修复**：`PageTable::handleKey` KEY_KEY2 缺少 `case 4`（WIFI），导致 WIFI 页面无法通过按键进入
- **BUG 修复**：`Screen_LOGView::setupScreen` 每次进入都写入模拟日志，清空后重进会重现。改为 `static bool s_firstBoot` 控制仅首次写入

## 5. 已知问题 / 注意事项

1. `Data_Transfer.c` 中 GUI→System 的 7 个 handler 仍是 TODO，需要主控端接入真实函数。
2. `Screen_LOG` 当前仅支持 ASCII 短文本；中文日志建议后续走模板映射，不要直接塞长 Unicode 文本。
3. `dataTransferQueue` 深度默认 16，日志类通知不要高频灌入。
4. `Screen_WIFI` 当前为空壳页面，待后续接入 WiFi 扫描/连接功能。
5. `DT_NotifyWifiStatus()` 尚未添加——需要在 `Data_Transfer.c` 中新增消息类型，让主控端推送 WiFi 连接状态到 GUI。

