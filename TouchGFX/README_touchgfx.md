# TouchGFX 端侧开发文档

> 本文档记录 TouchGFX 端侧已实现功能、关键文件位置、开发流程、更新历史、已知问题。仅面向 GUI 端开发者。
> 主控端对接请参考 `API_mainctrl_touchgfx.md`。

## 1. 页面结构

当前共 4 个页面，由 PageTable 容器管理焦点切换。

| page_cnt | 页面 | 说明 |
|----------|------|------|
| 0 | Screen_HOME | 主页，显示电机速度、贴片进度、温度 |
| 1 | Screen_IMPORT | 导入页 |
| 2 | Screen_LOG | 日志页，支持滚动查看、自动跟随、清空 |
| 3 | Screen_WIFI | WiFi 页，支持 WiFi 开关切换 |

- `PAGE_COUNT = 4`，CW/CCW 循环切换
- KEY2 跳转到当前选中页面
- PUSH 键进入/退出页面焦点模式（Screen_LOG 专用）

## 2. 已实现功能

### 2.1 消息通信中枢（Data_Transfer）
- `TouchGFX/gui/include/gui/model/Data_Transfer.h`
- `TouchGFX/gui/src/model/Data_Transfer.c`

已实现：
- `DT_Msg_t` 统一消息体（8字节 payload）
- `dataTransferQueue`（System → GUI，16深）
- `guiCmdQueue`（GUI → System，16深）
- System→GUI：`DT_NotifySMTStatus/Temp/DownloadStatus/SMTProgress/MotorResetDone/Custom/MotorSpeed/LogText/WifiStatus`
- GUI→System：`DT_SendCommand()` + `DT_Dispatch()` 路由表
- `DT_CMD_WIFI_CTRL (0x17)` 已注册到路由表（handler 待主控端实现）

### 2.2 Model / ModelListener
- `TouchGFX/gui/src/model/Model.cpp`
- `TouchGFX/gui/include/gui/model/ModelListener.hpp`

已实现：
- `Model::processQueue()` 每帧消费 GUI 队列，通过 Presenter 分发到 View
- `Model::sendCommand()` Presenter 发命令入口
- `ModelListener` 回调：`onNotifyMotorSpeed/onNotifyLogText/onNotifySMTProgress/onNotifyWifiStatus` 等

### 2.3 Screen_HOME
- `TouchGFX/gui/src/screen_home_screen/Screen_HOMEView.cpp`
- `TouchGFX/gui/src/screen_home_screen/Screen_HOMEPresenter.cpp`

已实现：
- `MotorSpeed` 文本显示（仅值变化时刷新）
- `Progress1` 圆环 + 数字显示
- `temperature` 温度显示
- `按键事件直接传递给 PageTable`

### 2.4 Screen_LOG
- `TouchGFX/gui/src/screen_log_screen/Screen_LOGView.cpp`
- `TouchGFX/gui/src/screen_log_screen/Screen_LOGPresenter.cpp`

已实现：
- `LogAddStr()` 追加日志到 `textArea1`（逐字符拷贝到 Unicode 缓冲区）
- `最大 100 行裁剪（LOG_MAX_LINES = 100，LOG_LINE_RESERVE = 8）
- `缓冲区大小 LOG_BUF_SIZE = 2048 UnicodeChar
- 自动跟随最新日志（m_autoFollow），手动上滑解锁，滑到底部重新锁定
- PUSH 键进入焦点模式：CW/CCW 滚动日志，KEY1 跳到最新，KEY2 清空，PUSH 退出焦点
- `s_firstBoot` 静态标志控制模拟日志仅首次启动写入
- `tip1/tip2` 固定文本框（Designer 中放置）
- Presenter 接收 `DT_LOG_TEXT`，转发到 `handleSystemLog(code, param)`

### 2.5 Screen_WIFI
- `TouchGFX/gui/src/screen_wifi_screen/Screen_WIFIView.cpp`
- `TouchGFX/gui/src/screen_wifi_screen/Screen_WIFIPresenter.cpp`

已实现：
- KEY1 切换 WiFi 开/关（`m_wifiEnabled` 状态变量）
- ON/OFF 图片显示切换（堆叠放置，切换 `setVisible`）
- `按下 KEY1 时通过 DT_CMD_WIFI_CTRL 发送命令给主控（data.status: 0=关, 1=开）
- `applyWifiImage()` 根据 `m_wifiEnabled` 更新 ON/OFF 显示
- 其他按键（CW/CCW/PUSH/KEY2）交给 PageTable 处理
- WiFi 扫描/连接功能待后续实现

### 2.6 PageTable（页面选择容器）
- `TouchGFX/gui/include/gui/containers/PageTable.hpp`
- `TouchGFX/gui/src/containers/PageTable.cpp`

已实现：
- CW/CCW 循环切换 4 个页面焦点（`updateSelection`）
- KEY2 跳转到当前选中页面
- WIFI 图标四态切换：`wifi_disc` / `wifi_connected` / `wifi_disc_selc` / `wifi_connected_selc`
- `setWifiConnected(bool)` 公共 API，由 Presenter 回调触发

## 3. 关键文件位置

### 消息层
- `TouchGFX/gui/include/gui/model/Data_Transfer.h`
- `TouchGFX/gui/src/model/Data_Transfer.c`
- `TouchGFX/gui/include/gui/model/Model.hpp`
- `TouchGFX/gui/src/model/Model.cpp`
- `TouchGFX/gui/include/gui/model/ModelListener.hpp`

### FrontendApplication（页面跳转）
- `TouchGFX/gui/include/gui/common/FrontendApplication.hpp`
- `TouchGFX/gui/src/common/FrontendApplication.cpp`

**重要**：Designer 重新生成后通常只保留 `gotoScreen_HOME`其他页面跳转需要在此文件手动补全（IMPORT/LOG/WIFI）及其 `Callback` 对象。

### HOME
- `TouchGFX/gui/include/gui/screen_home_screen/Screen_HOMEView.hpp`
- `TouchGFX/gui/src/screen_home_screen/Screen_HOMEView.cpp`
- `TouchGFX/gui/include/gui/screen_home_screen/Screen_HOMEPresenter.hpp`
- `TouchGFX/gui/src/screen_home_screen/Screen_HOMEPresenter.cpp`

### IMPORT
- `TouchGFX/gui/include/gui/screen_import_screen/Screen_IMPORTView.hpp`
- `TouchGFX/gui/src/screen_import_screen/Screen_IMPORTView.cpp`
- `TouchGFX/gui/include/gui/screen_import_screen/Screen_IMPORTPresenter.hpp`
- `TouchGFX/gui/src/screen_import_screen/Screen_IMPORTPresenter.cpp`

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

### PageTable
- `TouchGFX/gui/include/gui/containers/PageTable.hpp`
- `TouchGFX/gui/src/containers/PageTable.cpp`

## 4. 开发注意事项

### 4.1 Designer 重新生成的影响
- `TouchGFX/generated/` 下的文件会被覆盖，不要手动编辑
- `TouchGFX/gui/` 下的手写文件不会被覆盖
- **每次重新生成后需要检查**：`FrontendApplicationBase.hpp` 中是否自动生成了新的 `gotoScreen_*` 方法，如果与手动添加的重复则需删除手动的一份

### 4.2 编码规范
- 文件编码：UTF-8 + CRLF
- 中文注释支持，但需确保 UTF-8 编码与乱码检查
- `Data_Transfer.c` 中 GUI→System handler 均为 TODO，接入时注意不要破坏已有路由结构

### 4.3 按键映射
| 按键 | 值 | 说明 |
|------|-----|------|
| KEY1 | 0 | PageTable: detail；LOG: 跳到最新；WIFI: 切换 WiFi 开关 |
| KEY2 | 1 | PageTable: 跳转到选中页面；LOG: 清空日志 |
| CW (KEY_DOWN) | 2 | 向下翻页 / 日志下滚 |
| CCW (KEY_UP) | 3 | 向上翻页 / 日志上滚 |
| PUSH | 4 | 进入/退出焦点模式（LOG） |

## 5. 更新历史

### 2026-06-24
- 初始版本：Data_Transfer 消息中枢、Model/ModelListener 分发、Screen_HOME、Screen_LOG
- 新增 `DT_MOTOR_SPEED` / `DT_LOG_TEXT`
- 新增 `API_mainctrl_touchgfx.md` 和 `README_touchgfx.md`

### 2026-06-25
- 新增 Screen_WIFI 页面（初始为空壳）
- PageTable 支持 WIFI 图标四态切换
- Screen_LOG 修复：清空后重进不再重现（`s_firstBoot`）
- 模拟器兼容性修复

### 2026-06-26
- BUG 修复：PageTable KEY_KEY2 缺少 WIFI case
- BUG 修复：Screen_LOGView setupScreen 每次进入都写模拟日志

### 2026-07-01
- 删除 Screen_RESET 页面及所有 refre 相关代码
- PageTable PAGE_COUNT 5→4，WIFI 编号 4→3
- Screen_WIFI 功能实现：KEY1 切换 WiFi 开关 + ON/OFF 图片 + DT_CMD_WIFI_CTRL
- Data_Transfer 新增 DT_CMD_WIFI_CTRL (0x17)
- FrontendApplication 手动补全 IMPORT/LOG/WIFI 跳转方法（Designer 只自动生成 HOME）

## 6. 已知问题 / 注意事项

1. `Data_Transfer.c` 中 GUI→System 的 handler 均为 TODO，需要主控端接入真实函数。
2. `Screen_LOG` 当前仅支持 ASCII 短文本，中文日志建议走模板映射。
3. `dataTransferQueue` 深度默认 16，日志类通知不要高频灌入。
4. `Screen_WIFI` 当前只有开关切换，WiFi 扫描/连接待后续实现。
5. `DT_NotifyWifiStatus()` 已声明（0x08），主控端可调用推送 WiFi 连接状态，GUI 会切换 PageTable 中的 WiFi 图标。
6. `motorReset_Start/motorReset_IsDone` 仍在 `Data_Transfer.h/c` 中声明，但 Screen_REFRE 已删除，若无其他引用可考虑移除。
