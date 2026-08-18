## 十一、调试任务与经验总结

### 11.1 StartHostMotionTestTask

位于 `Task/app_test.c`，是一个将**上位机通讯 + XY 运动控制**结合在一起的测试任务。

**启动流程：**
1. `CAN_Init(&hfdcan1, NULL)` — 启动 CAN 外设（调用 `HAL_FDCAN_Start`）
2. `HAL_FDCAN_ActivateNotification(...)` — 激活 CAN RX 中断（必须！否则收不到电机到位反馈）
3. `Motor_Init()` — 初始化三轴电机（设置 vFOC 模式 → 使能 → 设到位阈值 → 开同步 → 归零）
4. 发送 `DEBUG_MODE\n` 触发上位机进入调试模式
5. 主循环：接收上位机指令 → 解析 → 执行电机动作 + 回显

**命令映射（当前）：**
| 上位机命令 | 物理轴 | 运动方式 |
|---|---|---|
| `MOVE_UP / MOVE_DOWN` | X 轴（X1+X2 双电机） | 阻塞式相对移动 |
| `MOVE_LEFT / MOVE_RIGHT` | Y 轴（单电机） | 阻塞式相对移动 |
| `MOVE_UP_START / MOVE_DOWN_START` | X 轴（X1+X2） | 连续 JOG |
| `MOVE_LEFT_START / MOVE_RIGHT_START` | Y 轴 | 连续 JOG |
| `MOVE_STOP` | X1+X2+Y 三轴 | 立即急停 |
| `SET_ORIGIN` | X1+X2+Y 三轴 | 归零 |

**mm 转步数常量：** `#define STEPS_PER_MM 3276.8f`（1圈=16384步，假设导程=5mm，需实际校准）

### 11.2 MKS 电机 CAN 控制要点

1. **CAN 外设必须手动启动：** STM32 的 `MX_FDCAN1_Init` 只初始化时钟和引脚，还必须调用 `HAL_FDCAN_Start`（在 `CAN_Init` 内部）CAN 才能真正通信。否则 TX 帧只写入 FIFO 不发到总线。

2. **CAN RX 中断必须激活：** 调用 `HAL_FDCAN_ActivateNotification` 激活 `FDCAN_IT_RX_FIFO0_NEW_MESSAGE` 后才能收到电机反馈。否则 `CAN_Process_Task` 永远收不到到位事件，`osEventFlagsWait` 每次都超时。

3. **同步模式影响急停：** `Motor_Init` 中 `motorSyncEnable(1)` 开启了同步模式，之后所有 `0xF5` 指令（包括急停帧）都被缓存，只有收到 `0x4B` 同步触发才执行。`MOVE_STOP` 发完急停帧后必须跟 `motorSyncTrigger(0)`。

4. **速度单位：** `positionMode3Run` 的 speed 参数是 MKS 的 RPM 值，不是 mm/s。JOG 命令从上位机收到的 speed 参数（mm/s）不能直接透传，需要缩放。当前代码直接 `(uint16_t)parsed.param`，应在 `MOVE_*_START` case 中加转换系数。

5. **栈要求：** `StartHostMotionTestTask` 栈已扩至 **4096** 字节（`app_freertos.c`），同时 `PrintDebug` 内部的 `vsnprintf` 已替换为自实现的 `dbg_vformat`（栈占用约 80 字节，远低于标准库的 ~800 字节）。连续 CAN 发送之间插入 `osDelay(2)` 释放栈帧，防崩溃。

### 11.3 上位机通讯协议要点

上位机（SerialTool.exe）协议见 `E:/聊天记录/上位机串口通讯协议.txt`：
- 格式：`COMMAND arg\n`，UTF-8 编码
- 离散移动：`MOVE_UP/DOWN/LEFT/RIGHT <mm>`
- 连续移动（勾选"连续移动"后）：第一次点击发 `MOVE_*_START <速度>`，第二次点击同一方向发 `MOVE_STOP`
- 点击不同方向：先自动发 `MOVE_STOP`，再发新方向的 `START`
- G4 启动时发 `DEBUG_MODE\n` 激活上位机调试面板

### 11.4 已知坑点

| 问题 | 现象 | 根因 | 解决 |
|------|------|------|------|
| 没调 CAN_Init | CAN TX 有打印但电机不动 | CAN 外设未启动 | CAN_Init(&hfdcan1, NULL) |
| 没调 HAL_FDCAN_ActivateNotification | 点动后阻塞 10 秒 | CAN RX 中断未激活 | 激活 RX FIFO 中断 |
| 栈太小 | 启动消息不打印/崩溃 | vsnprintf 栈深约800B | 栈扩至4096，vsnprintf替换为dbg_vformat |
| JOG speed太低于300 | 电机只震不转 | RPM太低 | 上位机发speed至少300 |
| 同步模式急停不执行 | MOVE_STOP后电机继续跑 | 急停被缓存未触发 | axis_stop后跟motorSyncTrigger(0) |
| CMSIS错误码与EVENT_ANY_ERROR冲突 | 单步移动全返回-2 | osFlagsErrorParameter=0x80000008的bit3与EVENT_ANY_ERROR重合 | 先用(int32_t)flags小于0过滤错误码 |
| 同步触发后状态被消耗 | 下次单步X1X2直接执行不走同步 | motorSyncTrigger后同步标志被清除 | disable_sync_stop末尾补motorSyncEnable(1) |
| JOG切换方向电机不动 | UP_START后点DOWN_START停住不反走 | 运行中缓存被锁 | disable_sync_stop先行急停再发新方向 |
| MOVE_TO被当作CSV | 坐标运动命令不识别 | app_uart_parser.c中MOVE_TO错嵌套在SET_ORIGIN内 | 移出嵌套平级处理 |
| 连续CAN发送崩溃 | 日志在TX ID=2处截断 | PrintDebug到vsnprintf栈叠加超限 | osDelay(2)间隔发送释放栈帧 |
| dbg_vformat输出百分号2X | CAN日志显示格式串原文 | 百分号02X的宽度位2未被跳过 | 加数字位跳过逻辑 |
### 11.5 本任务涉及的文件

| 文件 | 角色 |
|------|------|
| Task/app_test.c | StartHostMotionTestTask + move_xy_relative + axis_stop + disable_sync_stop + dbg_vformat + PrintDebug |
| Task/app_test.h | 函数声明 + extern hostMotionTestTask_attributes |
| Task/app_uart_parser.c | 上位机行协议解析器（MOVE_TO/SET_ORIGIN 括号已修复） |
| Core/Src/app_freertos.c | RTOS 线程创建 + hostMotionTestTask_attributes（栈 4096） |
| Drivers/ZeMCU-G4/driver_motor.c | positionMode3Run、Motor_Init、motorSyncTrigger、motorSyncEnable 等 API |
| Drivers/ZeMCU-G4/driver_can.c | CAN_Init、CAN_Transmit_Data（CRC 含 CAN ID）+ PrintDebug 调用 |

### 11.6 MKS 同步模式深度解析

MKS SERVO42D 同步模式流程：

1. `motorSyncEnable(1)` 广播 → 电机进入同步模式（此后 0xF5 被缓存，状态码 0x05）
2. 发送 `0xF5` 位置指令到各轴 → 电机缓存指令（状态码 0x05）
3. `motorSyncTrigger(0)` 广播 → 所有电机**同时**执行缓存指令（状态码 0x01→0x02）

**关键坑点**：
- **运行期间缓存被锁**：电机执行中（0x01）不接受新 0xF5 缓存覆盖。切换方向/停止需先用 `disable_sync_stop` 中止
- **同步触发消耗状态**：`motorSyncTrigger(0)` 执行后同步标志被清除，必须重新 `motorSyncEnable(1)` 才能继续缓存
- **syncEnable 可能被电机忽略**：日志证明 Y 轴运行时无视 `motorSyncEnable(0)` 广播，故不切换同步模式、直接用缓存+触发更可靠
- **三轴状态不一致**：一个 disable_sync_stop 周期后可能 X1/X2 在非同步而 Y 在同步，需末尾 `motorSyncEnable(1)` 统一
- **move_xy_relative 同步保障**：每次 `move_xy_relative` 调用必须 (1) 发包前 `motorSyncEnable(1)` 确保缓存开启，(2) `motorSyncTrigger(0)` 后立即 `motorSyncEnable(1)` 恢复同步。缺失任一步骤会导致后续运动 X1 比 X2 早起步，双 X 龙门拉扯 → 抖动 + error。

### 11.7 disable_sync_stop 函数设计

> **【2026-06-20 校正】** 以下内容描述的是早期"同步模式"设计。当前实际代码已迁移至 `Task/app_motion.c`，
> 且 `Motor_Init()` 调用 `motorSyncEnable(0)` 关闭了同步模式，三轴独立执行。
> `disable_sync_stop` 当前实现仅依次调用 `axis_stop(X1/X2/Y)` + `osDelay(5)`，无需 `motorSyncTrigger`。
> 详见 §23.1。

最终版本（`Task/app_test.c` 第 523-533 行）：
```c
static void disable_sync_stop(void) {
    axis_stop(X1_ADDR);    // 缓存急停（0xF5 速度=0）
    axis_stop(X2_ADDR);
    axis_stop(Y_ADDR);
    osDelay(5);
    motorSyncTrigger(0);   // 触发执行 → 中止当前运动
    osDelay(10);
    motorSyncEnable(1);    // 恢复同步模式（触发后状态被消耗）
    osDelay(10);
}
```

设计演进（3 版）：
1. 初版：`motorSyncEnable(0)` 退出同步 → `axis_stop` → `motorSyncEnable(1)` 重开。问题：Y 轴无视 syncEnable 广播
2. 二版：去掉同步切换，纯缓存+触发。问题：触发后同步状态丢失，下次单步 X1/X2 直接执行不走同步
3. **终版**：缓存急停 + 触发 + 恢复同步。兼顾可靠停止与状态一致性
### 11.8 move_xy_relative 当前实现（2026-05 最终版）

> **【2026-06-20 校正】** 以下内容描述的是早期"同步模式"设计。当前实际代码已迁移至 `Task/app_motion.c`，
> 且不再调用 `motorSyncEnable(1)` / `motorSyncTrigger(0)`。`Motor_Init()` 关闭同步模式后，
> `positionMode3Run` 通过 `CAN_Transmit_Data` 直接发送，各轴独立执行。详见 §23.1。

（`Task/app_test.c` move_xy_relative 函数）

```c
motorSyncEnable(1);         // ★ 发包前强制开启同步（确保 0xF5 被缓存而非立即执行）
osDelay(5);

if (dx != 0) {
    positionMode3Run(X1_ADDR, speed, acc, target_x);
    osDelay(2);             // ★ CAN 时序：给 MKS 电机足够时间处理 0xF5 缓存
    positionMode3Run(X2_ADDR, speed, acc, target_x);
    osDelay(2);
}
if (dy != 0) {
    positionMode3Run(Y_ADDR,  speed, acc, target_y);
    osDelay(2);             // ★ 无此延时 Y 轴来不及缓存 → 不运动
}

motorSyncTrigger(0);        // 三轴同时执行
osDelay(5);
motorSyncEnable(1);         // ★ 触发后恢复同步，下次运动不被破坏
osDelay(5);
// ... 等待到位事件 ...
```

**关键要点**：
1. **osDelay(2) 的双重作用**：(a) CAN TX FIFO 防溢出，(b) 给 MKS 电机处理 0xF5 缓存的时间。去掉后 Y 的 0xF5 距 0x4B 触发仅 ~100μs，电机来不及缓存 → Y 不运动
2. **同步双保险**：`motorSyncTrigger` 消耗同步标志，不恢复则下次 `move_xy_relative` 的 0xF5 立即执行，X1 比 X2 早 2ms 起步 → 机械拉扯
3. **最优参数**：speed=300, acc=25（speed=600/acc=70 导致短行程无巡航段、全程加减速、跟随误差累积）

### 11.9 dbg_vformat 轻量格式化

（`Task/app_test.c` 第 50-107 行）

自实现格式化引擎，替代 `<stdio.h>` 的 `vsnprintf`：

| 特性 | vsnprintf | dbg_vformat |
|------|-----------|-------------|
| 栈占用 | ~800 字节 | ~80 字节 |
| 支持格式 | printf 全部 | 项目实际 7 种：%d %ld %u %.1f %.2f %02X %s %.*s |
| 中文/UTF-8 | 支持 | 支持（逐字节透传，0x25 才触发格式解析） |
| 依赖 | stdio.h | 无外部依赖 |

**格式位解析顺序**：旗标(0)→宽度→精度(.2/.1/.*)→长度(l)→类型符(d/u/s/X/f)。
**坑**：宽度位（如 %02X 的 2）必须在进入类型符前跳过，否则被当作普通字符输出。

### 11.10 位置模式跟随误差调试记录（2026-05-28）

**现象**：JOG 连续运动无 error，MOVE_TO 位置运动时 X1 出现跟随误差（逐渐增大、到位清零），三轴抖动。

**根因**：
- MKS 0xF5 位置模式是内置轨迹规划器，给定 speed/acc/target 后内部规划加速→巡航→减速曲线
- JOG 目标 = 8388607（极远）→ 短暂加速后恒速巡航 → 跟随误差 ≈ 0
- 位置模式目标 = 短行程（如 10mm = 32768 步）→ speed=600/acc=70 时全程加减速、无巡航段 → 跟随误差持续累积
- X1 机械负载 > X2（双 X 龙门不对称），加速时 X1 力矩不足，误差更大

**解决过程**：
1. 降速降加速度 speed 600→300, acc 70→10：error 仍存在，根因是 **同步模式在 trigger 后被消耗，move_xy_relative 未恢复**
2. 修复同步：发包前后双 `motorSyncEnable(1)` + 移除 osDelay(2) → error 消失，但 **Y 轴不运动**
3. 恢复 osDelay(2)：无延时 Y 的 0xF5 距触发仅 ~100μs，MKS 来不及缓存 → Y 被跳过
4. 微调 acc 10→25：在扭矩需求和加速持续时间之间折中
5. MKS 硬件上调高 X1 工作电流（Ma，0x83 指令）：补偿 X1 额外机械负载，降低跟随误差。保持电流 HoldMa 不调整

**最终状态**：speed=300, acc=25，`move_xy_relative` 带同步双保险 + osDelay(2) 时序保护，X1 电流在 MKS 调参软件中单独提升。

## 十二、TouchGFX + FreeRTOS 移植日志（2026-05）

> **【2026-08-01 校正】** TouchGFX/ST7306/Key_Task/Data_Transfer 桥接层已随 GUI 独立板迁移删除；G4 改由 SPI2 与 G0B1 GUI 板通信（协议见 AGENTS.md §4.5，本次会话见 HISTORY.md §49）。本节仅作历史参考。

### 12.1 移植概述

| 项目 | 移植前状态 | 移植后状态 |
|------|-----------|-----------|
| TouchGFX | 裸机 main() while(1) 轮询 | 独立 TouchGFX_Task（栈 8192B） |
| VSYNC 信号 | 未实现 | TIM7 硬件定时器 30Hz 模拟 VSYNC |
| LCD 驱动 | HAL_Delay 依赖 Systick | BusyDelay 忙等替代，移除 HAL_Delay |
| 按键驱动 | 裸机轮询 | FreeRTOS Key_Task（10ms 周期） + 消息队列 |
| 主系统?GUI 通信 | 全局变量轮询 | FreeRTOS 消息队列（Data_Transfer） |
| 系统时基 | Systick | TIM6（HAL 系统时基） |

### 12.2 新增文件清单

| 文件 | 说明 |
|------|------|
| `TouchGFX/target/KeyController.cpp` | 按键控制器，消费 keyEventQueue，松开触发 |
| `TouchGFX/target/KeyController.hpp` | KeyController 头文件，继承 ButtonController |
| `TouchGFX/gui/src/model/Data_Transfer.c` | 主系统?GUI 消息队列模块 |
| `TouchGFX/gui/include/gui/model/Data_Transfer.h` | Data_Transfer 头文件 |
| `TouchGFX/gui/src/containers/circleProgress.cpp` | 圆形进度条容器 |
| `TouchGFX/gui/include/gui/containers/circleProgress.hpp` | 圆形进度条头文件 |
| `Task/app_motor.c/h` | 电机应用层任务（待实现） |
| `startup_stm32g474xx.s` | 启动汇编文件 |
| `.gitignore` | Git 忽略规则（Keil 构建产物） |

### 12.3 新增 FreeRTOS 任务

| 任务名 | 入口函数 | 栈大小 | 优先级 | 周期/触发 | 功能 |
|--------|---------|--------|--------|----------|------|
| `TouchGFX_Task` | `TouchGFX_Task` | 8192 | Normal | VSYNC 驱动 | GUI 渲染（ST7306_Init → touchgfx_taskEntry） |
| `Key_Task` | `Key_Task` | 256 | Normal | 10ms | 5键硬件扫描 + 消抖 → keyEventQueue |

### 12.4 新增 FreeRTOS 通信对象

| 对象 | 类型 | 定义位置 | 用途 |
|------|------|---------|------|
| `keyEventQueue` | osMessageQueue(16) | app_freertos.c | Key_Task → KeyController → TouchGFX |
| `dataTransferQueue` | osMessageQueue(16) | app_freertos.c | 主系统 Task → Model::processQueue() → UI 更新 |
| `frame_buffer_sem` | osSemaphore(1) | OSWrappers.cpp | TouchGFX 帧缓冲互斥 |
| `vsync_queue` | osMessageQueue(1) | OSWrappers.cpp | TIM7 ISR → TouchGFX 渲染循环 |

### 12.5 TouchGFX 移植详细步骤

#### 12.5.1 CubeMX 配置
- FreeRTOS: CMSIS_V2, TICK_RATE_HZ=1000, TOTAL_HEAP_SIZE=32768, heap_4
- TIM6: HAL 系统时基（替代 Systick）
- TIM7: TouchGFX VSYNC 信号源（Prescaler=169, Period=33333 → 30Hz@170MHz）
- SPI2: ST7306 LCD 通信

#### 12.5.2 VSYNC 信号链路
```
TIM7 (30Hz) → TIM7_DAC_IRQHandler → TouchGFX_VSYNC_IRQCallback()
  → touchgfxSignalVSync()
    → HAL::vSync() + OSWrappers::signalVSync()
      → vsync_queue → TouchGFX 渲染循环唤醒
```

**关键实现文件：**
- `Core/Src/stm32g4xx_it.c:415` — `TouchGFX_VSYNC_IRQCallback()` 调用点
- `TouchGFX/target/TouchGFXHAL.cpp:43-58` — `TouchGFX_VSYNC_TimerInit()` / `TouchGFX_VSYNC_IRQCallback()`

#### 12.5.3 HAL 初始化顺序
```
MX_TouchGFX_PreOSInit()  → 空
MX_TouchGFX_Init()       → touchgfx_components_init() + touchgfx_init()
  touchgfx_init()        → FrontendHeap::getInstance() → gotoStartScreen() → hal.initialize()
    HAL::initialize()    → OSWrappers::initialize() (创建 FreeRTOS 信号量/队列)
    TouchGFXHAL::initialize() → setButtonController(&keyController) + isInited=1
osKernelStart()
  TouchGFX_Task:
    ST7306_Init(&hspi2)
    touchgfx_taskEntry()
      enableLCDControllerInterrupt() → TouchGFX_VSYNC_TimerInit() → TIM7 启动
      backPorchExited() → swapFrameBuffers() + tick() → handlePendingScreenTransition()
         → gotoScreen_HOMEScreenNoTransition() → 渲染 HOME 界面
      主循环: waitForVSync() → render → flushFrameBuffer() → ST7306_Refresh()
```

### 12.6 按键系统架构

#### 12.6.1 事件流
```
Key_Task (10ms FreeRTOS 任务)
  → Key_Scan()                    // 软件消抖（10ms 采样)
  → keyEventQueue                 // KeyEvent_t {key_id, type: 0=松开 1=按下}

KeyController::sample()           // TouchGFX 框架每帧调用，唯一消费者
  → 仅处理 type==0（松开/单击）
  → TouchGFX 框架 → handleKeyEvent(key_id)
    → Screen_HOMEView::handleKeyEvent()
      → PageTable::handleKey(key)
        ├─ KEY_DOWN (2)  → 光标下移 (page_cnt+1)%4
        ├─ KEY_UP   (3)  → 光标上移 (page_cnt+3)%4
        ├─ KEY_KEY1 (0)  → 切换详情面板
        └─ KEY_KEY2 (1)  → 进入选中页面/gotoScreen
```

#### 12.6.2 按键 ID 映射
| ID | 宏 | 引脚 | 功能 |
|----|-----|------|------|
| 0 | KEY_KEY1 | PC6 | 切换详情 |
| 1 | KEY_KEY2 | PC7 | 确认/进入 |
| 2 | KEY_DOWN | PC8 | 光标下移 |
| 3 | KEY_UP | PA8 | 光标上移 |
| 4 | KEY_PUSH | PC9 | 按压 |

> ?? CW/CCW 引脚与 AGENTS.md §二 不一致（规格: CW=PA8, CCW=PC8），反映编码器物理安装方向

#### 12.6.3 关键 API
| 函数 | 文件 | 说明 |
|------|------|------|
| `Key_Init()` | key.c | GPIO 初始化（上拉输入，低有效） |
| `Key_Scan()` | key.c | 消抖扫描（每10ms调用），产生 press/release 事件 |
| `Key_GetEvent()` | key.c | 读取松开事件（单击） |
| `Key_GetPressEvent()` | key.c | 读取按下事件 |
| `Key_IsAnyPressed()` | key.c | 查询实时按键状态（返回首个按下键ID，无按键0xFF） |
| `KeyController::sample()` | KeyController.cpp | TouchGFX 框架接口，消费 keyEventQueue（松开触发） |
| `PageTable::handleKey()` | PageTable.cpp | UI 按键路由（HOME/IMPORT/LOG/RESET 切换） |

### 12.7 已修复问题

| 问题 | 现象 | 根因 | 解决方案 |
|------|------|------|----------|
| **屏幕不显示** | ST7306_Init 后全黑，touchgfx_taskEntry 无输出 | `stm32g4xx_it.c` 中 `touchgfxSignalVSync()` 被注释，渲染循环死锁 | ISR 改为调用 `TouchGFX_VSYNC_IRQCallback()` |
| **TIM7 启动失败** | enableLCDControllerInterrupt 后 waitForVSync 阻塞 | TouchGFX_VSYNC_TimerInit 只 Stop+Init，不 Start | TimerInit 末尾增加 `HAL_TIM_Base_Start_IT(&htim7)` |
| **TIM7 优先级不当** | FreeRTOS API 调用可能失败 | NVIC pri=5 在 configMAX_SYSCALL 边界 | enableLCDControllerInterrupt 中设置 pri=14 |
| **按键映射混乱** | 按 KEY1 触发 KEY_DOWN | KeyController 与 Model 双路消费 keyEventQueue + 无消抖 GPIO 读取 | KeyController 单路消费队列；Model 移除按键处理 |
| **按键按下即触发** | 期望松开触发 | Model 处理 type==1（按下事件） | 改为 type==0（松开/单击事件） |
| **KeyController 不可用** | 编译报错 undefined symbol | KeyController.cpp 未加入 Keil 编译列表 | 添加到 pnp_1.uvprojx |
| **LCD SPI 死锁** | 卡在 `while (!(hspi->SR & SPI_SR_TXE))` | HAL_Delay 依赖 uwTick，FreeRTOS 启动前 uwTick=0 | `BusyDelay()` 替代 `HAL_Delay()`（__NOP 忙等） |
| **ST7306_Init 卡死** | HAL_GetTick 返回 0，超时循环死锁 | HAL 时基未初始化 | CubeMX 配置 TIM6 为 HAL 时基（替代 Systick） |
| **TouchGFXHAL.cpp 编译报错** | TIM7_IRQn / hspi2 / touchgfxSignalVSync 未声明 | 裸机代码直接粘贴，缺少 RTOS 适配 | 重写 HAL 初始化流程 |
| **Data_Transfer 符号重复** | dataTransferQueue multiply defined | 全局变量在多个 .c/.o 中定义 | 仅在 app_freertos.c 定义，其他文件 extern |
| **TIM7 ISR 重复定义** | TIM7_DAC_IRQHandler multiply defined | CubeMX 生成 + 自定义 ISR 冲突 | 删除自定义 ISR，复用 stm32g4xx_it.c |

### 12.8 仍存在的问题（?? 待处理）

| 问题 | 位置 | 说明 |
|------|------|------|
| **keyEventQueue 被 KeyController 每次只取1条** | KeyController.cpp | `osMessageQueueGet` 零超时只取1条，高频按键可能堆积。可改为 while 循环排空 |
| **PageTable 未处理 KEY_PUSH** | PageTable.cpp | KEY_PUSH(id=4) 无 case 分支 |
| **其他屏幕未实现 handleKeyEvent** | IMPORT/LOG/RESET View | 仅 HOME 屏幕有按键处理，切换到其他屏幕后按键无响应 |
| **Data_Transfer 各 case 未实现** | Model.cpp:processQueue() | DT_SMT_STATUS/DT_TEMP_CHANGE 等 case 体为 TODO 注释 |
| **MCU 负载未监控** | TouchGFXHAL | 未启用 `MCUInstrumentation`，无帧率/CPU 占用统计 |
| **TOUCHGFX_Framebuffer 段未在链接脚本定义** | STM32G474XX_FLASH.ld | 帧缓冲使用 `LOCATION_PRAGMA_NOLOAD` 放置，可能未正确对齐 |
| **KeyController::sample() 未清空按下事件** | KeyController.cpp | 仅消费 type==0（松开），按下事件(type==1)残留在队列中。虽不触发但占用队列空间 |

### 12.9 关键代码路径速查

| 功能 | 入口文件 | 关键函数/行号 |
|------|---------|-------------|
| TouchGFX 任务 | `TouchGFX/App/app_touchgfx.c` | `TouchGFX_Task():86-92` |
| VSYNC ISR | `Core/Src/stm32g4xx_it.c` | `TIM7_DAC_IRQHandler:408-417` |
| TIM7 初始化 | `TouchGFX/target/TouchGFXHAL.cpp` | `TouchGFX_VSYNC_TimerInit:43-49` |
| HAL 初始化 | `TouchGFX/target/TouchGFXHAL.cpp` | `TouchGFXHAL::initialize:68-73` |
| 帧刷新 | `TouchGFX/target/TouchGFXHAL.cpp` | `flushFrameBuffer:96-101` |
| 按键任务 | `Drivers/ZeMCU-G4/key.c` | `Key_Task:105-127` |
| 按键扫描 | `Drivers/ZeMCU-G4/key.c` | `Key_Scan:62-101` |
| 按键控制器 | `TouchGFX/target/KeyController.cpp` | `sample:16-26` |
| 消息队列处理 | `TouchGFX/gui/src/model/Model.cpp` | `processQueue:27-67` |
| LCD 初始化 | `st7306/lcd.c` | `ST7306_Init:176-188` |
| LCD 刷新 | `st7306/lcd.c` | `ST7306_Refresh:209-215` |
| 1bpp→2×4 转换 | `TouchGFX/target/TouchGFXHAL.cpp` | `convert_1bpp_to_2x4:80-93` |
| FreeRTOS 任务创建 | `Core/Src/app_freertos.c` | `MX_FREERTOS_Init:165-222` |
| 任务属性定义 | `Core/Src/app_freertos.c` | `touchGFX_attributes:48-52` `keyTask_attributes:54-58` |

| 任务属性定义 | `Core/Src/app_freertos.c` | `touchGFX_attributes:48-52` `keyTask_attributes:54-58` |
## 十三、分发中枢（Data_Transfer Dispatcher）协议文档

> 本章节供 Agent 和开发人员参考，描述 FreeRTOS ? TouchGFX 双向通信框架。

### 13.1 架构概览

```
┌──────────────────────────────┐
│     Data_Transfer.h/c        │  分发中枢（唯一出入口）
│   ┌────────┐  ┌───────────┐  │
│   │ 路由表  │  │ DT_Dispatch│  │
│   └────────┘  └───────────┘  │
└──────┬──────────────┬────────┘
       │  System→GUI  │  GUI→System
       │  (通知)      │  (命令)
       ▼              ▼
┌─────────────┐  ┌──────────────┐
│dataTransferQ│  │  guiCmdQueue  │
│ (已有)      │  │  (新增)       │
└──────┬──────┘  └──────┬───────┘
       │                │
  Model::tick()    Model::processQueue()
  →processQueue()  →DT_Dispatch()
  →ModelListener   →路由表→handler
  →Presenter→View  →系统任务
```

### 13.2 消息类型速查表

#### System → GUI（通知，0x00~0x0F）
| ID | 枚举 | 数据字段 | 发送 API | Presenter 回调 |
|----|------|---------|----------|---------------|
| 0x00 | `DT_SMT_STATUS` | `.data.status` | `DT_NotifySMTStatus(u8)` | `onNotifySMTStatus(u8)` |
| 0x01 | `DT_TEMP_CHANGE` | `.data.temp` | `DT_NotifyTemp(u16)` | `onNotifyTemp(u16)` |
| 0x02 | `DT_DOWNLOAD_STATUS` | `.data.status` | `DT_NotifyDownloadStatus(u8)` | `onNotifyDownloadStatus(u8)` |
| 0x03 | `DT_SMT_PROGRESS` | `.data.progress` | `DT_NotifySMTProgress(cur,total)` | `onNotifySMTProgress(u8,u8)` |
| 0x04 | `DT_MOTOR_RESET_DONE` | — | `DT_NotifyMotorResetDone()` | `onNotifyMotorResetDone()` |
| 0x05 | `DT_CUSTOM_MSG` | `.data.raw[0..1]` | `DT_NotifyCustom(code,param)` | `onNotifyCustom(u8,u8)` |

#### GUI → System（命令，0x10~0x2F）
| ID | 枚举 | 参数1 | 参数2 | 对应 handler |
|----|------|-------|-------|-------------|
| 0x10 | `DT_CMD_MOTOR_MOVE` | x(mm*100) | y(mm*100) | `_h_motor_move` |
| 0x11 | `DT_CMD_MOTOR_STOP` | — | — | `_h_motor_stop` |
| 0x12 | `DT_CMD_MOTOR_HOME` | — | — | `_h_motor_home` |
| 0x13 | `DT_CMD_SMT_START` | — | — | `_h_smt_start` |
| 0x14 | `DT_CMD_SMT_PAUSE` | — | — | `_h_smt_pause` |
| 0x15 | `DT_CMD_HEATER_SET` | temp(0.1℃) | — | `_h_heater_set` |
| 0x16 | `DT_CMD_SYSTEM_RESET` | — | — | `_h_system_reset` |
| 0x1F | `DT_CMD_CUSTOM` | code | param | 自定义 |

### 13.3 添加新消息的步骤

**Step 1 — 定义 ID**：在 `Data_Transfer.h` 的 `DT_MsgType` 枚举中添加。
- System→GUI 通知：加在 `0x00~0x0F` 区段
- GUI→System 命令：加在 `0x10~0x2F` 区段

**Step 2 — 注册路由**（仅命令方向需要）：在 `Data_Transfer.c` 的 `s_routeTable[]` 中添加：
```c
{ DT_CMD_YOUR_NEW_CMD, _h_your_handler, NULL },
```

**Step 3 — 实现 handler / 发送函数**：
- 命令：在 `Data_Transfer.c` 末尾添加 `static void _h_xxx(const DT_Msg_t *msg)`
- 通知：在 `Data_Transfer.c` 添加 `DT_NotifyXxx()` + 在 `ModelListener.hpp` 添加 `onNotifyXxx()` + 在 `Model.cpp` 的 switch 中添加 case

### 13.4 使用示例

#### 示例1：系统任务通知 GUI 温度变化
```c
// 在任意 FreeRTOS 任务中（如加热台任务）
#include "gui/model/Data_Transfer.h"

void Heater_Task(void *arg) {
    for (;;) {
        uint16_t temp = read_thermocouple();  // 读取温度 (0.1℃)
        DT_NotifyTemp(temp);                   // 通知 GUI 更新显示
        osDelay(500);
    }
}
```

#### 示例2：GUI 按钮触发电机移动
```cpp
// 在 Presenter 中
#include <gui/model/Model.hpp>

void Screen_HOMEPresenter::onButtonMoveClicked()
{
    // 移动电机到 (50.00mm, 30.00mm)，坐标以 mm*100 为单位
    model->sendCommand(DT_CMD_MOTOR_MOVE, 5000, 3000);
}
```

#### 示例3：GUI 按钮启动贴片
```cpp
void Screen_IMPORTPresenter::onStartSMTClicked()
{
    model->sendCommand(DT_CMD_SMT_START);  // 无参数命令
}
```

#### 示例4：系统任务发送自定义通知
```c
// 发送带两个字节参数的自定义消息
DT_NotifyCustom(0xAB, 0xCD);

// GUI Presenter 端接收：
void Screen_HOMEPresenter::onNotifyCustom(uint8_t code, uint8_t param)
{
    if (code == 0xAB) {
        // 处理自定义逻辑
    }
}
```

### 13.5 关键文件索引

| 文件 | 角色 |
|------|------|
| `TouchGFX/gui/include/gui/model/Data_Transfer.h` | 消息协议定义（枚举+结构体+API声明） |
| `TouchGFX/gui/src/model/Data_Transfer.c` | 路由表 + 分发函数 + 发送实现 |
| `TouchGFX/gui/include/gui/model/Model.hpp` | Model 声明（sendCommand 入口） |
| `TouchGFX/gui/src/model/Model.cpp` | 双向队列消费（processQueue + dispatch） |
| `TouchGFX/gui/include/gui/model/ModelListener.hpp` | Presenter 回调接口 |
| `Core/Src/app_freertos.c` | 队列创建（guiCmdQueue）+ DT_Init() |

### 13.6 设计原则

1. **单一入口**：所有 FreeRTOS?GUI 通信必须经过 `Data_Transfer.h` 的 API，禁止直接操作全局变量
2. **路由解耦**：GUI 端只调用 `Model::sendCommand()`，不关心命令如何到达目标
3. **命名约定**：`DT_xxx` = 通知，`DT_CMD_xxx` = 命令，`onNotifyXxx` = Presenter 回调
4. **非阻塞**：所有队列操作使用 `osMessageQueuePut/Get` 零超时，不阻塞渲染循环
5. **向后兼容**：全局变量 `if_now_SMT`/`total_SMT`/`now_SMT`/`Temp`/`if_DOWNLOAD_READY` 保留可用，但新代码应通过 `DT_Notify*` 系列函数更新
## 十四、任务报告 — TouchGFX FreeRTOS 移植 + 分发中枢（2026-05-20~21）（后续任务报告可在此基础上进行延申）

### 14.1 任务概述

将 STM32G474 贴片机项目中的 TouchGFX GUI、ST7306 LCD 驱动、5键按键驱动从裸机迁移到 FreeRTOS，
并搭建统一的分发中枢（Dispatcher）实现 FreeRTOS ? TouchGFX 双向通信。

### 14.2 实现的功能清单

| 序号 | 功能 | 涉及文件 |
|------|------|----------|
| 1 | VSYNC 信号修复（屏幕点亮） | `Core/Src/stm32g4xx_it.c`, `TouchGFX/target/TouchGFXHAL.cpp` |
| 2 | TIM7 定时器启停时序修复 | `TouchGFX/target/TouchGFXHAL.cpp` |
| 3 | FreeRTOS 安全 NVIC 优先级配置 | `TouchGFX/target/TouchGFXHAL.cpp` |
| 4 | Key_Task 创建（10ms 扫描周期） | `Core/Src/app_freertos.c` |
| 5 | 按键消抖 + 松开触发事件流 | `Drivers/ZeMCU-G4/key.c`, `TouchGFX/target/KeyController.cpp` |
| 6 | KeyController HAL 注册（ButtonController 框架） | `TouchGFX/target/TouchGFXHAL.cpp`, `KeyController.cpp` |
| 7 | 按键→UI 导航（HOME/IMPORT/LOG/RESET 切换） | `TouchGFX/gui/src/containers/PageTable.cpp` |
| 8 | TouchGFX_Task 精简（移除调试代码） | `TouchGFX/App/app_touchgfx.c` |
| 9 | Model 按键消费移除（避免双路竞争） | `TouchGFX/gui/src/model/Model.cpp` |
| 10 | 分发中枢 — 统一消息协议（DT_Msg_t） | `TouchGFX/gui/include/gui/model/Data_Transfer.h` |
| 11 | 分发中枢 — 路由表 + DT_Dispatch() | `TouchGFX/gui/src/model/Data_Transfer.c` |
| 12 | 分发中枢 — System→GUI 通知发送 API（6个） | `TouchGFX/gui/src/model/Data_Transfer.c` |
| 13 | 分发中枢 — GUI→System 命令路由（7个 handler） | `TouchGFX/gui/src/model/Data_Transfer.c` |
| 14 | 分发中枢 — guiCmdQueue 双向闭环 | `Core/Src/app_freertos.c`, `Model.cpp` |
| 15 | ModelListener 扩展（6个 onNotify* 回调） | `TouchGFX/gui/include/gui/model/ModelListener.hpp` |
| 16 | Model::sendCommand() — Presenter 发令入口 | `TouchGFX/gui/include/gui/model/Model.hpp`, `Model.cpp` |
| 17 | Model::processQueue() 实现全部 case 分发 | `TouchGFX/gui/src/model/Model.cpp` |
| 18 | .gitignore 完善（构建产物 + 密钥/凭证保护） | `.gitignore` |
| 19 | AGENTS.md 更新（§12 移植日志 + §13 协议文档） | `AGENTS.md` |
| 20 | Keil 工程添加 KeyController.cpp | `MDK-ARM/pnp_1.uvprojx` |

### 14.3 修复的问题

| # | 问题 | 根因 | 解决方案 |
|---|------|------|----------|
| 1 | 屏幕不亮，卡黑屏 | stm32g4xx_it.c 中 touchgfxSignalVSync() 被注释 | ISR 改为调用 TouchGFX_VSYNC_IRQCallback() |
| 2 | TIM7 不启动 | TouchGFX_VSYNC_TimerInit 只 Stop+Init | 末尾加 HAL_TIM_Base_Start_IT |
| 3 | 按键映射混乱 | KeyController 与 Model 双路消费 keyEventQueue | KeyController 单路消费；Model 移除按键处理 |
| 4 | 按下即触发（期望松开） | Model 处理 type==1 | 改为 type==0（松开/单击） |
| 5 | KeyController 编译报错 | 未加入 Keil 编译列表 | 添加到 pnp_1.uvprojx |
| 6 | LCD 初始化卡死 | HAL_Delay 依赖 Systick（FreeRTOS 启动前 uwTick=0） | BusyDelay() 替代 HAL_Delay() |
| 7 | 队列类型名冲突 | 新旧 DataTransferMsg_t / DT_Msg_t | 统一为 DT_Msg_t |

### 14.4 新增的 API

#### System → GUI 通知（C 函数，任意任务可调用）
```c
void DT_NotifySMTStatus(uint8_t is_smt);
void DT_NotifyTemp(uint16_t temp);           // 0.1℃ 单位
void DT_NotifyDownloadStatus(uint8_t status);
void DT_NotifySMTProgress(uint8_t current, uint8_t total);
void DT_NotifyMotorResetDone(void);
void DT_NotifyCustom(uint8_t code, uint8_t param);
```

#### GUI → System 命令（C++ 方法，Presenter 调用）
```cpp
// Model.hpp
void Model::sendCommand(DT_MsgType_t type, int32_t p1 = 0, int32_t p2 = 0);
```

#### ModelListener 回调（C++ 虚函数，Presenter 覆写）
```cpp
virtual void onNotifySMTStatus(uint8_t is_smt)        {}
virtual void onNotifyTemp(uint16_t temp)               {}
virtual void onNotifyDownloadStatus(uint8_t status)    {}
virtual void onNotifySMTProgress(uint8_t cur, uint8_t total) {}
virtual void onNotifyMotorResetDone()                  {}
virtual void onNotifyCustom(uint8_t code, uint8_t param) {}
```

### 14.5 代码统计

| 类别 | 修改 | 新增 | 合计 |
|------|------|------|------|
| 文件数 | 7 | 1 (.gitignore 完善) | 8 |
| 代码行 | ~300 行重写 | ~250 行新增 | ~550 行 |
| 文档行 | 50 行更新 | ~330 行新增 | ~380 行 |

### 14.6 仍待完成

1. `Data_Transfer.c` 中的 7 个 handler 多数为 TODO 占位，需对接实际系统函数
2. IMPORT/LOG/RESET 屏幕未实现 `handleKeyEvent`，切换后按键无响应
3. `Data_Transfer` 全局变量 (`if_now_SMT` 等) 应逐步迁移到 `DT_Notify*` 模式
4. 分发中枢目前由 `Model::tick()` 驱动（~30Hz），高负载场景可考虑独立 `DT_DispatchTask`


## 十五、ESP32 通信模块 — IoT 互联 (v2, 2026-05-28)

### 15.1 模块概述

通过 SPI4 接口与 ESP32-C3 模块全双工通信，实现贴片机状态数据实时推送至局域网 Web 仪表盘。
协议遵循 `esp-temp/ESP32-C3通信接口规范_v2.0.md`。

**三层架构：**

| 层 | 文件 | 职责 |
|----|------|------|
| 驱动层 | `Drivers/ZeMCU-G4/driver_esp32.c/h` | SPI4 128B 全双工收发 + CS(PE3) 控制 + C3RESET(PC13) 硬复位 |
| 协议层 | `Task/app_esp_protocol.c/h` | 组包(数据/控制/查询)、解包(响应)、命令码宏、温度/进度格式化 |
| 任务层 | `Task/app_esp_task.c/h` | ESP_Task 500ms 心跳 + 轮询分时数据推送 + 控制命令路由 + 响应处理 |

### 15.2 任务架构

| 任务 | 栈 | 优先级 | 周期/触发 |
|------|-----|--------|----------|
| `ESP_Task` | 512 | Normal | 事件驱动 + 500ms 心跳 |

**任务间通信：**
- `esp_cmd_queue` (8 深度) — 其他任务 → ESP_Task，发送 WiFi 开关/查询指令
- `esp_cmd_queue` 在 `app_freertos.c` 中定义并创建，`app_esp_task.h` 通过 extern 声明供其他模块引用

### 15.3 数据流

```
系统状态变量                         ESP_Task                      ESP32-C3
(复用现有)                            │                              │
now_SMT/total_SMT → 进度 "32/50" → 0x10 0x01 ──SPI4──►  解析 → WebSocket
if_now_SMT/Heater → 状态 "SMTing" → 0x10 0x02 ──SPI4──►  推送
HeaterStatus.state → 加热 "1"/"0" → 0x10 0x03 ──SPI4──►
HeaterStatus.cur_temp → 温度 "85.3" → 0x10 0x04 ──SPI4──►

ESP_SendCommand(WIFI_ON) → esp_cmd_queue → 0x20 0x01 ──SPI4──►  启动 WiFi
                                                       ◄──SPI4──  0xF2 "1"
                              g_esp_wifi_connected = 1
```

### 15.4 全局标志位 (供后续模块复用)

| 变量 | 定义位置 | 含义 |
|------|----------|------|
| `g_esp_wifi_enabled` | `app_esp_task.c` | WiFi 功能开关 (0=关, 1=开) |
| `g_esp_wifi_connected` | `app_esp_task.c` | WiFi 实际连接状态 (ESP 回传) |
| `g_esp_fault_code` | `app_esp_task.c` | 故障码 (0x00=无故障) |
| `g_esp_last_rx_tick` | `app_esp_task.c` | 最后收到响应的 tick |

### 15.5 命令码速查

| 主命令 | 子命令 | 含义 | 发送接口 |
|--------|--------|------|----------|
| `0x10` | `0x01` | 贴片进度 | ESP_Task 自动推送 |
| `0x10` | `0x02` | 贴片状态 | ESP_Task 自动推送 |
| `0x10` | `0x03` | 加热台状态 | ESP_Task 自动推送 |
| `0x10` | `0x04` | 加热台温度 | ESP_Task 自动推送 (变化>0.5°C) |
| `0x20` | `0x01` | 打开 WiFi | `ESP_SendCommand(ESP_CMD_WIFI_ON)` |
| `0x20` | `0x02` | 关闭 WiFi | `ESP_SendCommand(ESP_CMD_WIFI_OFF)` |
| `0x30` | `0x01` | 查询故障 | `ESP_SendCommand(ESP_CMD_QUERY_FAULT)` |
| `0x30` | `0x02` | 查询 WiFi | `ESP_SendCommand(ESP_CMD_QUERY_WIFI)` |

### 15.6 已修复 Bug

- **HostMotion 任务重复创建：** `app_freertos.c` 中 `osThreadNew(StartHostMotionTestTask, ...)` 出现两次，已删除无句柄的那行，仅保留 `hostMotionTaskHandle = osThreadNew(...)`
- **SPI4_CS/C3RESET 上电默认低：** `ESP_GPIO_Init()` 将 CS 和 RST 置高
- **esp_cmd_queue 重复定义 (L6200E)：** `app_esp_task.c` 中移除定义，统一由 `app_freertos.c` 定义，`app_esp_task.h` 通过 extern 声明引用

### 15.7 新增文件清单

| 文件 | 行数 |
|------|------|
| `Drivers/ZeMCU-G4/driver_esp32.c` | 67 |
| `Drivers/ZeMCU-G4/driver_esp32.h` | 41 |
| `Task/app_esp_protocol.c` | 139 |
| `Task/app_esp_protocol.h` | 143 |
| `Task/app_esp_task.c` | 282 |
| `Task/app_esp_task.h` | 55 |
| `ESP32通信模块_STM32端实现报告_v2.md` | 报告文档 |

### 15.8 仍待完成

1. ESP32 端联调 (ESP 端需按同一接口规范实现)
2. 上位机集成 WiFi 开关指令
3. TouchGFX GUI 添加 WiFi 状态指示器

> **校正（2026-08-06）：** §15 为 v2 历史记录。当前 ESP32 SPI 协议以 AGENTS.md §4.6 和 HISTORY.md §55 为准：PC13 为 IRQ、无 RST、`ESP_Task` 栈 4096 且默认启用。


## 十六、TMC2209 UART 调试记录（2026-06-05~06）

### 16.1 背景

TMC2209 在 CubeMX 中添加 USART3 DMA 后 StartMotorTestTask 无法驱动电机旋转。
同一套代码在别人（无 DMA 配置）的工程中可以正常工作。

**最终确认的硬件根因：** TMC2209 模组被错误接到了 LPUART1 (PC0/PC1) 而非 USART3 (PB9/PB11)。
接线纠正后 UART 通信恢复，但暴露出以下软件问题。

### 16.2 已修复的软件问题

#### 问题 1：DMA 句柄干扰 TMC 阻塞式通信

**文件：** Drivers/ZeMCU-G4/driver_tmc2209.c — TMC_Init()

**根因：** CubeMX 启用 USART3 DMA 后，huart3.hdmarx / huart3.hdmatx 非 NULL。
UART_Error_Handler → UART_StartReceive_DMA(UART_CH3) 会真正启动 DMA 空闲接收，
偷走 TMC2209 应答字节，导致阻塞式 HAL_UART_Receive 超时。

**修复：** TMC_Init 中在 HAL_UART_DMAStop + HAL_UART_Abort 之后立即置 NULL：
`c
huart3.hdmarx = NULL;   // 断开 DMA RX 句柄
huart3.hdmatx = NULL;   // 断开 DMA TX 句柄
`

#### 问题 2：单线 UART 回声导致 ORE 阻塞 RXNE

**文件：** Drivers/ZeMCU-G4/driver_tmc2209.c — TMC_FlushRX()

**根因：** TMC2209 单线 UART 上 STM32 每发一字节，RX 脚同时收到回声。
HAL_UART_Transmit 阻塞模式下不管 RX，导致回声累积触发 ORE（溢出错误）。
**STM32G4 清除 ORE 必须写 ICR 寄存器**，读 ISR/RDR 方式无效（与老 STM32 不同）。

**修复：** TMC_FlushRX() 在读完 RXNE 后加入：
`c
if (__HAL_UART_GET_FLAG(&huart3, UART_FLAG_ORE)) {
    __HAL_UART_CLEAR_FLAG(&huart3, UART_CLEAR_OREF);  // 写 ICR 清 ORE
}
`

#### 问题 3：TMC_ReadReg_Internal 读请求发完后 FlushRX 吃掉首字节

**文件：** Drivers/ZeMCU-G4/driver_tmc2209.c — TMC_ReadReg_Internal()

**根因：** HAL_UART_Transmit 发读请求后，TMC_FlushRX 在 TC 等待后仍被调用，
但 TMC2209 在 ~17μs 内即开始应答，FlushRX 误将第一个应答字节当做残留回声读走。

**修复：** 读请求改为逐字节发送 + 同步读回声响：
`c
for (int i = 0; i < TMC_READ_REQUEST_LEN; i++) {
    while (!__HAL_UART_GET_FLAG(&huart3, UART_FLAG_TXE));
    huart3.Instance->TDR = req[i];
    uint32_t tmo = 100000;
    while (!__HAL_UART_GET_FLAG(&huart3, UART_FLAG_RXNE) && --tmo);
    if (tmo) { volatile uint32_t echo = huart3.Instance->RDR; (void)echo; }
}
while (!__HAL_UART_GET_FLAG(&huart3, UART_FLAG_TC));
// 不再调用 TMC_FlushRX — 回声已在逐字节循环中消费完毕
`

#### 问题 4：TMC2209 应答格式兼容（7 字节 vs 8 字节）

**文件：** Drivers/ZeMCU-G4/driver_tmc2209.c — TMC_ReadReg_Internal()

**根因：** 此 TMC2209 模组使用 7 字节应答格式（sync+addr+data[4]+crc），
不含寄存器地址回显字节。代码原本只能处理 8 字节格式。

**修复：** 接收逻辑自动适配：
`c
uint8_t data_ofs = (rx_count == 8) ? 3 : 2;  // 8字节格式 data 在 [3-6]，7字节在 [2-5]
uint8_t crc_ofs = rx_count - 1;
`

#### 问题 5：TMC2209 应答 CRC 与标准不符

**文件：** Drivers/ZeMCU-G4/driver_tmc2209.c — TMC_ReadReg_Internal()

**根因：** 此 TMC2209 模组的读应答 CRC 计算结果与标准 CRC-8-ATM 不一致
（标准计算 0x67，实际返回 0x01）。但读写通信均正常，数据正确。

**修复：** CRC 不匹配改为警告而非致命错误，数据直接信任使用。

#### 问题 6：TMC2209 初始化时序

**文件：** Drivers/ZeMCU-G4/driver_tmc2209.c — TMC_Init()

**根因：** 原代码在 TMC2209 禁用期间写配置寄存器，部分模组（VCCIO 由内部 LDO 供电）
ENN=HIGH 时 LDO 关断，数字逻辑掉电，UART 不工作，所有配置写入无效。
此外 TMC2209 上电后需要 ~200ms 稳定时间。

**修复：** 重构初始化顺序：
1. TMC_SetEnable(false) → 硬件复位
2. HAL_Delay(10) → 复位保持
3. DMA 接管 + TX 推挽配置
4. TMC_SetEnable(true) → 先使能
5. vTaskDelay(200) → 等 LDO/振荡器稳定
6. 写 GCONF/CHOPCONF/PWMCONF/IHOLD_IRUN/TPOWERDOWN

#### 问题 7：TX 引脚驱动模式

**结论：** 经过测试，TX 推挽模式（GPIO_MODE_AF_PP）配合 1kΩ 串联电阻是正确的。
开漏模式（GPIO_MODE_AF_OD）上升沿过慢导致信号质量问题。此 TMC2209 模组地址为 0x00，
波特率 115200，工作正常。

### 16.3 电机扭矩调优

| 参数 | 最终值 | 说明 |
|------|--------|------|
| 运行电流 | 1200mA (TMC2209_MOTOR_RUN_CURRENT) | 原 800mA 扭矩不足 |
| 斩波模式 | spreadCycle (GCONF_EN_SPREADCYCLE) | 比 StealthChop 扭矩大 |
| 微步分辨率 | 256 (MRES=0) | 保持精度 |
| 测试速度 | 40000 μsteps/s | ~47 RPM |

### 16.4 调试诊断方法总结

| 诊断手段 | 用途 |
|----------|------|
| USART_ISR 寄存器打印 | 判断 ORE/IDLE/RXNE 状态 |
| 逐字节收发 + 回声匹配检查 | 确认 TX→RX 物理层 |
| 地址扫描（0x00~0x03 读 IOIN） | 定位 TMC2209 实际地址 |
| 原始应答字节打印 | 分析 CRC/格式问题 |
| GCONF 写后读回验证 | 确认寄存器写入是否生效 |
| CRC 校准（4 种方案测试） | 排查非标准 CRC 实现 |

### 16.5 涉及文件

| 文件 | 变更 |
|------|------|
| Drivers/ZeMCU-G4/driver_tmc2209.c | 核心修改：FlushRX ICR清ORE、逐字节收发、7/8字节兼容、CRC非致命、初始化时序重构 |
| Drivers/ZeMCU-G4/driver_tmc2209.h | 电流 800→1000mA、GCONF 加 spreadCycle |
| Task/app_test.c | 测试速度调整 |

### 16.6 关键经验

1. **STM32G4 清 ORE 必须用 ICR**，读 ISR/RDR 方式在 G4 上无效
2. 单线 UART 的 TX 推挽 + 1kΩ 限流电阻是正确配置，开漏反而有害
3. TMC2209 上电后需 200ms+ 稳定，LDO 供电模组在 ENN=HIGH 时 UART 掉电
4. 此 TMC2209 模组使用 7 字节应答格式 + 非标准 CRC，但数据和读写功能正常
5. huart3.hdmarx/hdmatx 必须在 TMC 使用前置 NULL，否则 UART_Error_Handler 会重启 DMA 偷走数据


### 16.7 TMC2209 使能/关闭设计模式（2026-06-11~12）

**背景：** §16.2 解决了 TMC2209 通信层的所有问题（DMA 干扰、ORE、应答格式、初始化时序），
但遗留了一个系统级问题：`TMC_Init()` 完成后 TMC2209 驱动一直保持使能状态（ENN=LOW），
即使 R 轴不旋转，电机线圈也持续通电，导致发热和潜在噪声。

**设计决策：** 采取「用到才开，用完即关」策略。

**`TMC_Init()` 行为变更：** 写完全部配置寄存器后调用 `TMC_SetEnable(false)` 关闭驱动。
调用方无需额外操作——初始化完成时 TMC2209 已处于安全关闭状态。

**`TMC_ENABLE_DELAY_MS` 宏：** 定义在 `driver_tmc2209.h`，值 50ms。
取代之前散落在各处的硬编码延时（200ms、10ms 不统一），所有调用点统一引用。

**`r_axis_rotate` 包裹模式：**
```
TMC_SetEnable(true) → vTaskDelay(TMC_ENABLE_DELAY_MS)
  → TMC_SetSpeed(vactual) → vTaskDelay(run_time_ms)
  → TMC_SetSpeed(0) → vTaskDelay(R_ACCEL_DELAY)
  → TMC_SetEnable(false)
```
提前返回路径（`fabsf(delta) < 0.5f`）安全：此时 TMC2209 处于关闭状态。

**CubeMX PD15 默认值陷阱：** PD15(TMC1_EN) 在 CubeMX 中为 GPIO_Output，默认 LOW。
ENN 低有效 → 从 boot 起 TMC2209 使能。之前依赖 `Host_Task` 在启动时拉高 PD15，
但 `Host_Task` 可能被注释（如运行 `StartCamTestTask` 时）。**任何不使用 R 轴的任务必须在启动时调用 `TMC_SetEnable(false)`。**

**各任务 TMC2209 状态速查：**

| 任务 | TMC2209 状态 | 机制 |
|------|-------------|------|
| `Host_Task` | 启动时关闭 | `TMC_SetEnable(false)` |
| `StartCamTestTask` | 启动时关闭 | `TMC_SetEnable(false)` |
| `StartPickPlaceTestTask` | `TMC_Init` 后自动关闭 | `TMC_Init()` 末尾关闭 |
| `MotionTask_Func` | `TMC_Init` 后自动关闭 | `TMC_Init()` 末尾关闭 |
| `StartMotorTestTask` | 循环内每次开→用→关 | 显式包裹 |
| `r_axis_rotate` (通用) | 每次调用开→用→关 | 函数内包裹 |

**风格统一：** 所有延时统一使用 `vTaskDelay(pdMS_TO_TICKS(...))`，不再混用 `osDelay`。

**涉及文件：**
| 文件 | 改动 |
|------|------|
| `driver_tmc2209.h` | 新增 `TMC_ENABLE_DELAY_MS 50` |
| `driver_tmc2209.c` | `TMC_Init()` 末尾关闭 + 延时改宏 |
| `app_motion.c` | `r_axis_rotate` 包裹使能/关闭 + 延时改宏 + `osDelay`→`vTaskDelay` |
| `app_test.c` | `StartMotorTestTask` 包裹 + `StartCamTestTask` 关闭 |
| `app_host.c` | 裸 GPIO 写 → `TMC_SetEnable(false)` |

## 十七、PickPlace 联合测试任务（2026-06-04~10）

> **状态：已弃用。** 此测试任务的功能（Z 轴舵机、DRV8803、TMC2209）已合并到 `Host_Task` 的初始化流程中（见 §9.8-5/6）。`StartPickPlaceTestTask` 在 `app_freertos.c` 中处于注释状态。

### 17.1 概述

`StartPickPlaceTestTask` 位于 `Task/app_test.c`，用于测试 Z 轴舵机 + 吸嘴气泵 + 电磁阀 + R 轴的联合工作流程。

**任务属性：** 栈 2048B，优先级 Normal，名 `"PickPlace"`

**初始化顺序：**
1. `DRV8803_Init()` → `EnableChip(1)` + `EnableChip(2)` — 使能 12V 和 24V 两个 DRV8803 芯片
2. `Servo_Init(&htim2)` → `DRV8803_SetOutput(&Port_12VO4, true)` — TIM2_CH3(PB10) 50Hz PWM + 舵机上电
3. `TMC_Init()` — R 轴 TMC2209 初始化

**测试循环：**

| 步骤 | 动作 | 舵机 | 气泵(PE11) | 电磁阀(PA6) |
|------|------|------|-----------|------------|
| 拾取 | 舵机→30° + 气泵ON + 阀ON | PB10→30° | HIGH | LOW(导通) |
| 贴装 | 阀OFF + 泵OFF + 舵机→120° | PB10→120° | LOW | HIGH(关断) |
| R正转 | 斜坡 5000→80000 μstep/s | — | — | — |
| R反转 | 斜坡 -5000→-80000 μstep/s | — | — | — |

### 17.2 关键实现细节

**R 轴速度斜坡：** TMC2209 VACTUAL 模式无极变速/加速斜坡。直接跳全速时静摩擦力会卡住电机，需从 5000 μstep/s 起步，每级 +8000，40ms/级，逐步提升至 80000。

**电磁阀控制：** 已修正为通过语义化接口 `Valve_On()/Valve_Off()` 控制（见 §9.12）。实际硬件为标准 DRV8803：IN=HIGH→OUT=LOW→导通。不再使用 BSRR 直写。
**调试开关：**
- `PICKPLACE_VERBOSE`（app_test.c）— 开启 VACTUAL/DRV_STATUS 读回校验
- `SERVO_DEBUG`（driver_servo.h）— 开启 TIM 寄存器诊断输出

### 17.3 涉及文件

| 文件 | 角色 |
|------|------|
| Task/app_test.c | StartPickPlaceTestTask + 辅助函数 |
| Task/app_test.h | 函数声明 + 任务属性 |
| Core/Src/app_freertos.c | 任务创建（pickPlaceTestTaskHandle） |
| Drivers/ZeMCU-G4/driver_servo.c/h | 舵机 PWM 驱动 |
| Drivers/ZeMCU-G4/driver_drv8803.c/h | DRV8803 端口驱动 |
| Drivers/ZeMCU-G4/driver_tmc2209.c/h | R 轴 TMC2209 驱动 |


## 十八、舵机驱动改进记录（2026-06）

### 18.1 CubeMX AF 映射 Bug

**问题：** CubeMX 生成的 `HAL_TIM_MspPostInit` 中 PE8(TIM5_CH3) 被设为 `GPIO_AF1_TIM5`(AF1)，但 STM32G474 上 PE8→TIM5_CH3 对应 AF2。AF1 实际是 TIM1_CH1N，导致 TIM5_CH3 PWM 无法输出到 PE8。

**修复：** `driver_servo.c` 的 `Servo_Init()` 内检测 `htim->Instance == TIM5` 时，用 AF=0x02 重新初始化 PE8。

**注意：** TIM2_CH3(PB10) 的 AF 配置在 CubeMX 中是正确的，不需要修复。

### 18.2 TIM5 多通道 HAL 状态锁

**问题：** TIM5_CH1(PB2, 24V_C1) 被其他代码先调用 `HAL_TIM_PWM_Start` 启动后，`htim5.State` 变为 BUSY。之后 `Servo_Init` 调用 `HAL_TIM_PWM_Start(&htim5, TIM_CHANNEL_3)` 时 HAL 状态检查返回 HAL_ERROR，CC3E(CCER bit8) 未被置位。

**修复：** 临时保存 `htim->State` → 设为 `HAL_TIM_STATE_READY` → 调用 `HAL_TIM_PWM_Start` → 还原 State。此操作在任务启动阶段无并发风险。

### 18.3 舵机改为 TIM2_CH3

舵机信号从 PE8(TIM5_CH3) 迁移到 PB10(TIM2_CH3)。CubeMX 中需配置：
- TIM2: PSC=169, ARR=19999 → 50Hz
- TIM2_CH3: PWM Generation CH3

TIM2_CH1(PA0, 12V_C1) 受此频率变更影响从 ~1kHz 降到 50Hz，但由于 12V_C1 pulse 远超 ARR 实际只是开关控制，不受影响。

### 18.4 SERVO_DEBUG 诊断

在 `driver_servo.h` 中定义 `SERVO_DEBUG` 后，`Servo_Init()` 会输出 TIM PSC/ARR/CCR/CR1/CCER 寄存器值，用于验证 PWM 配置。调试完成后注释掉即可恢复干净编译。


## 十九、DRV8803 端口映射与使用注意事项

> 此章节的内容已合并至 §10.4（DRV8803 端口对照与使用示例），故障排查表见 §10.4 末尾。

---

## 二十、2026-06-17 会话更新 — Host_Task 全面重构

### 20.1 上位机命令扩充

§4.1 命令列表新增：
- `HEAT_ON` / `HEAT_OFF` — 加热台 回流焊启动/停止
- `SET_SCATTER_AREA` / `SET_SCATTER_SIZE <mm>` — 标定散料区
- `SET_PCB_AREA_MIN` / `SET_PCB_AREA_MAX` — 标定 PCB 区域
- `SET_BOTTOM_CAM` — 标定下相机位置
- `SET_Z_SAFE` / `SET_Z_PICK` / `SET_Z_PLACE` — 标定 Z 轴高度
- `SET_R_ZERO` — R 轴当前位置设为 0°
- `SAVE_CALIB` — 保存标定值到 Flash（写入 W25Q64）

### 20.2 CSV 解析 v2

CSV 格式从逗号分隔动态列升级为**制表符 `\t` 分隔 15 固定列**（对齐 `AGENTS_MCU_processing.md`）：
- 分隔符：`,` → `\t`
- 列结构：动态表头检测（`g_col_x/y/rot/smd`）→ 固定 15 列硬编码索引
- 坐标格式：纯数字 → 含 `"mm"` 后缀，新增 `parse_mm()` 剥离
- SMD 过滤：首字符匹配 → 全串白名单（`strcmp` 仅放行 `"Yes"`/`"MARK"`）
- 字段提取：旧逗号版 `get_csv_field()` → 新制表符版 `csv_get_field()`，含引号剥离
- 下载超时：300ms → **500ms**

### 20.3 Mark/元件分离存储

- Mark 点（SMD=`"MARK"`）独立存储于 `g_marks[MAX_MARKS]`，不再与贴装元件混存
- 新增 `g_mark_count` 计数
- `download_done()` 自动判断：有 Mark → 先 P2 建系 → 再 P1 贴装；无 Mark → 直接 P1
- `Component_t` 新增字段：`footprint[32]`, `layer`, `is_mark`

### 20.4 加热台集成

- `Host_Task` 初始化调用 `Heater_Init()`
- 主循环每轮调用 `Heater_ProcessStatus()` 处理 CAN 状态帧
- `HEAT_ON` → `Heater_SendStart()`, `HEAT_OFF` → `Heater_SendStop()`

### 20.5 P1 类别询问（v2 协议适配）

- `find_comp_step()` 新增 `VISION_GOT_CATEGORY_QUERY` 状态处理
- 调用 `Vision_ClsReply()` 回复元件类别
- 所有 `Vision_Start()` 补全 `class_id` 参数

### 20.6 标定系统

- `CalibrationData_t`（`app_config.h`）存储散料区/PCB区域/下相机/Z轴高度等标定值
- `g_calib` 全局变量，`Calib_Save()`/`Calib_Load()` 通过 W25Q64 Flash 持久化
- `HOST_PICK`/`HOST_PLACE`/`mark_align_step` 使用 `g_calib.*` 替代硬编码 `BOTTOM_CAM_*`/`FEEDER_AREA_*` 宏

### 20.7 编译修复

- 移除 `handle_debug_cmd` 中未使用变量 `int32_t steps;`
- 补全 `g_mark_count = 0` + `memset(g_marks, ...)` 初始化
- 补全 `Heater_Init()` 调用
- 修复 `HOST_PLACE` case 块括号配对
- 修复 `app_uart_parser.c` 中 `#undef MATCH` 被错误拼接到 `}` 同行

### 20.8 涉及文件

| 文件 | 改动 |
|------|------|
| `Task/app_uart_parser.h` | +`HCMD_HEAT_ON/OFF` + 标定 11 条命令枚举 |
| `Task/app_uart_parser.c` | `parse_cmd()` 新增 HEAT + 标定命令匹配 |
| `Task/app_host.h` | `Component_t` +footprint/layer/is_mark; `MAX_MARKS`; 移除硬编码校准宏 |
| `Task/app_host.c` | CSV解析重写; Mark/元件分离; HEAT+标定命令; 加热台集成; P1类别询问 |
| `Task/app_config.h` | `CalibrationData_t` + `g_calib` + `Calib_Save/Load` 声明 |
### 20.9 P3 err3_3 重试修复

- `offset_check_step()` 新增 `VISION_GOT_ERR_RETRY` case，调用 `Vision_Go()` 重发 go 帧
- 修复前：err3_3 时 Host_Task 不做响应，P3 卡死
- 修复后：`Vision_Go()` 内部检测 `VISION_GOT_ERR_RETRY` 自动重发，最多容忍 3 次，第 4 次 Cam 发 err3_7 → VISION_ERROR → 降级贴装

### 20.10 P1 分类 ID 动态映射

- 新增 `footprint_to_class_id(fp)` 函数，根据封装名映射 P1 类别：`LED*` → cledo(2)，`C0*`/`R0*` → ccapt(0)
- 所有 `Vision_Start(VCMD_P1, 0)` 硬编码替换为 footprint 映射
- 注意：LED-SMD 封装无法区分 cledy/cledo 子类，当前一律映射为 cledo

### 20.11 P1 错误分级重试

- 新增 `g_p1_retry_count` 静态变量跟踪每元件重试次数
- `find_comp_step()` VISION_ERROR 按错误码分级：
  - `err1_5`/`err1_8`/`err1_9`（可恢复）和 `err1_1`/`err1_4`（相机故障）：重试最多 3 次
  - 其他错误或重试耗尽：跳过当前元件
- VISION_DONE 成功后复位 `g_p1_retry_count = 0`

### 20.12 StartCamTestTask 角度追踪闭环

- `cam_test_run()` 新增 `float *out_angle_deg` 出参，VISION_DONE 时写入视觉检测角度（°）
- StartCamTestTask 测试流程改为完整闭环：P2 Mark 建系 → P1 找元件 → 吸取 → R 轴矫正1 → 移下相机 → P3 复核 → R 轴矫正2 → 释放（执行顺序 P2→P1→P3，R 轴矫正位于吸取后）
- 初始化新增：`Servo_Init`、`TMC_Init`、`DRV8803_EnableChip(2)`、`Calib_Load`、`Valve_Off`

### 20.13 app_vision.c 变量重命名

- `g_tmp_ang` → `g_tmp_cls`（4 处），消除 P1 Phase1 第三字段为 class_id 而非角度的命名误导

### 20.14 Flash 驱动修复 (driver_spiflash_w25q64.c)

审查发现并修复三个问题：
- **P0：CS 引脚定义错误。** 宏定义为 `GPIOC, GPIO_PIN_11`（SPI3 MISO），修正为 `GPIOA, GPIO_PIN_15`（SPI3 硬件 CS）。注释中 `SPI1_*` 同步更正为 `SPI3_*`
- **P1：`W25Q64_WaitReady` 超时永远返回成功。** `while(timeout--)` 循环 + `if(!timeout)` 死代码，超时时返回 0。改为 `for(i=0;i<W25Q64_TIMEOUT;i++)` + 明确的 `return -1`
- **P2：`W25Q64_Erase` 对齐检查用 `&&` 应为 `||`。** 单侧不对齐时漏过检查，改为 `||`

### 20.15 视觉角度未使用（已知待办）

P1/P3 视觉检测的 `angle_x100` 在 `Host_Task` 的 `find_comp_step`/`offset_check_step` 中 `VISION_DONE` 时未被保存。`HOST_PLACE` 的 `r_axis_rotate` 仅使用 CSV 原始 `target_angle`。需在 `Component_t` 增加 `vision_angle_offset` 字段，在 P1/P3 完成时累加，在 PLACE 时合成最终 R 轴旋转角。此逻辑已在 `StartCamTestTask` 中验证完毕，待并入 `Host_Task`。

## 二十一、2026-06-19 会话更新 — 电机去重BUG修复 + 替换工具升级

### 21.1 handle_debug_cmd 去重范围收窄

**问题：** `handle_debug_cmd` 的去重逻辑 `if (cmd->cmd == g_last_cmd && cmd->param == g_last_param)` 对所有命令生效。离散移动命令（`MOVE_UP/DOWN/LEFT/RIGHT`）执行一次后，`g_last_cmd` 和 `g_last_param` 被设置为该命令，后续相同命令被静默丢弃，导致「每个方向只能动一次」。

**根因：** `g_last_cmd`/`g_last_param` 是 static 变量，仅在收到不同命令时才会被覆盖。同方向同步长连续发送时永远命中去重条件。

**修复（[`Task/app_host.c`](Task/app_host.c:256)）：** 去重条件改为仅对 JOG 连续移动的 START 命令生效：

```c
// 改前
if (cmd->cmd == g_last_cmd && cmd->param == g_last_param) {
    return;
}

// 改后
if ((cmd->cmd == HCMD_MOVE_UP_START   || cmd->cmd == HCMD_MOVE_DOWN_START ||
     cmd->cmd == HCMD_MOVE_LEFT_START || cmd->cmd == HCMD_MOVE_RIGHT_START) &&
    cmd->cmd == g_last_cmd && cmd->param == g_last_param) {
    return;
}
```

**效果：**
- 离散移动 `MOVE_UP/DOWN/LEFT/RIGHT` — 每次点击都执行
- 连续 JOG `MOVE_*_START` — 同方向同速度双击防抖保留
- `MOVE_STOP` 或换方向自动复位去重状态

### 21.2 tools/_replace.ps1 升级到 v2

**原问题：**
1. `@'...'@` here-string 的换行符取决于 `.ps1` 文件自身格式，与目标文件 CRLF/LF 不一致时匹配失败
2. node 脚本内嵌在 `@"..."@` 中，`\"` 链条过长时 PowerShell 解析报错
3. 失败时仅输出 `NOT FOUND`，无诊断信息

**v2 改进：**

| 改进项 | 说明 |
|--------|------|
| 4 种 CRLF/LF 回退 | 精确→双 LF→old LF+file CRLF→old CRLF，自动适配 |
| 临时文件执行 | node 脚本写入临时 `.js` 文件再执行，消除转义问题 |
| `-DryRun` 开关 | 预览替换而不写入文件 |
| 失败诊断 | NOT FOUND 时显示搜索文本前 80 字符 + 首行在文件中的位置 |
| `.Replace()` 替代 `-replace` | 避免正则转义（路径中的 `\`） |

**用法不变：** 编辑顶部三个变量 → 运行 `powershell -ExecutionPolicy Bypass -File .\tools\_replace.ps1`（加 `-DryRun` 预览）。

**涉及文件：**

| 文件 | 改动 |
|------|------|
| `Task/app_host.c` | `handle_debug_cmd` 去重条件收窄为仅 JOG START |
| `tools/_replace.ps1` | 引擎重写：CRLF 回退 + 临时文件 + DryRun + 诊断 |


---

## 二十二、2026-06-19 会话更新 — PnP 流程完善与 Bug 修复

本次会话基于 [框架及后续修改.md](E:/Desktop/qiansai/框架及后续修改.md) 的方案，完成以下改造：

### 22.1 Z 轴角度修正

- **安全高度 / P3 检测高度：** 75°（原 120°）
- **吸取高度 / 贴装高度：** 110°（原 60° / 55°）
- 修改位置：app_motion.c 的 ANGLE_UP/ANGLE_DOWN 宏，app_config.h 的 CALIB_DEFAULT_Z_* 宏
- 注意：角度越大吸嘴越低（与早期代码方向相反）

### 22.2 安全运动封装

- 新增 safe_move_to(target_x, target_y, speed, acc, cur_x, cur_y) — 所有 XY 运动统一入口，内部先调 z_safe() 抬 Z 到安全高度
- 新增 z_safe() / z_pick() / z_place() — 使用 g_calib 标定值的三高度函数
- pick_component() / place_component() 改用 z_pick()/z_place() 替代 z_down()，用 z_safe() 替代 z_up()
- app_host.c 中 10 处 move_xy_relative 调用全部替换为 safe_move_to
- 新增全局 volatile bool g_motor_error，safe_move_to 检测到返回 -2（限位/堵转）时置位

### 22.3 状态机扩充

HostState_t 新增 4 个状态，流程变更为：
`
HOST_HOME → (SET_ORIGIN) → HOST_DEBUG → (CSV下载) → HOST_DOWNLOADING
  → HOST_MARK_ALIGN (P2) → HOST_FIND_COMP (P1) → HOST_PICK
  → HOST_MOVE_TO_BOTTOM_CAM → HOST_OFFSET_CHECK (P3)
  → HOST_MOVE_TO_PCB (旋转补偿+移动) → HOST_PLACE
  → 循环至 HOST_DONE → (回零点) → HOST_DEBUG
  ⇡ HOST_WAIT_REFILL (RESUME恢复)  ⇡ HOST_ERROR (自动恢复)
`

| 状态 | 说明 |
|------|------|
| HOST_HOME | 上电等待 SET_ORIGIN，收到后自动发 DEBUG_MODE 并进入 HOST_DEBUG |
| HOST_MOVE_TO_BOTTOM_CAM | 吸取完成后移动到下相机站，复位 g_p3_offset |
| HOST_MOVE_TO_PCB | 旋转补偿 + PCB 原点平移 + P3 偏移 → R 轴旋转 → XY 移动到贴装位 |
| HOST_WAIT_REFILL | 连续 3 个元件 P1 失败，发 REFILL_NEEDED，等待上位机 RESUME |

### 22.4 P2 引导式扫描 + 建系 + 旋转补偿

**网格扫描（Mark0）：**
- download_done 中根据 g_calib.pcb_area_min/max 计算扫描网格（步长 9mm ≈ 29491 步）
- 蛇形扫描：偶数行左→右，奇数行右→左，减少空行程
- 每格位超时 ~3s（P2_SCAN_TIMEOUT=300），超时后 Vision_Start+Go 重发搜索
- PCB 区域未标定（全 0）时退化为当前位置单点扫描
- 网格耗尽未找到 → HOST_ERROR

**跳转（Mark1/Mark2）：**
- Mark0 对齐完成后，从 g_marks_actual[prev] + CSV 理论间距跳转到下一 Mark 预估位置
- 跳转后相机搜索超时 → HOST_ERROR（防止挂死）
- g_mark_just_jumped 标志防止同一 Mark 的二次 VISION_GOT_STOP 触发冗余跳转

**建系（VISION_DONE）：**
- 建系算法自 2026-08-03 起为三点最小二乘 + 残差剔除 + 失败重试（详见 §50），原两点法+Mark3 验证已废弃
- Mark 理论坐标来自 CSV 中 SMD="MARK" 行的 Mid X/Y，无需硬编码

**旋转补偿（HOST_MOVE_TO_PCB）：**
`c
machine_x = rotate(csv_x, theta) + pcb_origin_x + p3_offset_x
machine_y = rotate(csv_y, theta) + pcb_origin_y + p3_offset_y
`
- g_pcb_frame.valid=false 时 fallback 到 Mark 平均偏移（无旋转）

### 22.5 散料区四等分 + 子位扫描

- SCATTER_CELLS=4 个单元格（左上/右上/左下/右下），中心偏移 ±size/4
- 每格 SCATTER_SUBPOS=5 个子扫描位（中心+四角），偏移 ±size/8
- scatter_init_cells() 预计算 g_scatter_subpos[4][5][2]
- component_cell() 通过 `ootprint_to_class_id() 映射元件→单元格
- P1 err1_5（未找到）时依次尝试 5 个子位，全部失败后才计为连续失败
- scatter_size_steps=0（未标定）时全部退化为散料区中心

### 22.6 P3 偏移累积

- offset_check_step 每次 VISION_GOT_POS 移动后将 dx_s/dy_s 累加到 g_p3_offset_x/y
- HOST_MOVE_TO_BOTTOM_CAM 进入时复位（每个元件独立）
- HOST_MOVE_TO_PCB 的旋转补偿公式中叠加 P3 偏移
- 新增 VISION_GOT_ERR_RETRY 处理（err3_3 可恢复错误 → Vision_Go() 重试）

### 22.7 电机错误检测

- CAN_Process_Task 收到状态 0x03 时设 EVENT_ANY_ERROR
- safe_move_to 检测 move_xy_relative 返回 -2 时置 g_motor_error = true
- 主循环每轮结束后检测 g_motor_error，非 HOST_DEBUG/HOST_HOME 状态自动 z_safe() + HOST_ERROR

### 22.8 新增上位机命令

| 命令 | 说明 |
|------|------|
| RESUME | 从 HOST_WAIT_REFILL / HOST_ERROR 恢复，重试当前元件 |
| ABORT | 中止当前 PnP 流程（≥DOWNLOADING），回 HOST_DEBUG |

### 22.9 Bug 修复汇总

| Bug | 修复 |
|-----|------|
| Mark1/Mark2 二次 VISION_GOT_STOP 触发冗余跳转 | g_mark_just_jumped 标志，首次跳转后置位，二次跳过 |
| 跳转后相机找不到 Mark 永久挂起 | VISION_BUSY 通用超时：扫描超时→下一格，跳转超时→HOST_ERROR |
| P3 err3_3 导致挂死 | offset_check_step 新增 VISION_GOT_ERR_RETRY case |
| g_mark_just_jumped 跨下载残留 | download_done marks 路径显式清零 |
| g_pcb_frame / g_mark_avg / g_consecutive_failures 跨下载残留 | download_done 开头统一 memset + 清零 |
| HOST_ERROR 未确保 Z 轴安全 | HOST_ERROR 开头加 z_safe() |
| HOST_DONE 缺少回零点 | safe_move_to(0, 0) 回到机器零点 |

### 22.10 涉及文件

| 文件 | 改动 |
|------|------|
| Task/app_host.h | +MarkPoint_t, PCBFrame_t, ScatterCell_t, SCATTER_CELLS/SUBPOS; +HOST_HOME/Wait_REFILL/MOVE_TO_* 状态枚举; +extern g_pcb_frame |
| Task/app_host.c | +safe_move_to 全面替换; +P2 扫描+跳转+建系+旋转补偿; +散料区子位扫描; +P3 偏移累积; +电机错误检测; +RESUME/ABORT; +download_done 状态复位; +Mark 坐标从 CSV 读取; +scatter_init_cells; +component_cell |
| Task/app_motion.h | +safe_move_to, z_safe/pick/place, g_motor_error 声明 |
| Task/app_motion.c | +z_safe/pick/place/safe_move_to 实现; +g_motor_error; pick/place_component 改用三高度 |
| Task/app_uart_parser.h | +HCMD_RESUME, HCMD_ABORT |
| Task/app_uart_parser.c | +RESUME, ABORT 命令解析 |
| Task/app_config.h | CALIB_DEFAULT_Z_SAFE 120→75, Z_PICK 60→110, Z_PLACE 55→110 |
| AGENTS.md | §4.1 命令列表更新; §5 任务描述更新; §7 数据结构表扩充; 新增 §21 |

### 22.11 视觉状态覆盖矩阵

三个 step 函数对所有 VisionState_t 的处理覆盖：

| VisionState | mark_align_step | find_comp_step | offset_check_step |
|-------------|:---:|:---:|:---:|
| VISION_BUSY | 超时→扫描/跳转超时→ERROR | — | — |
| VISION_GOT_CATEGORY_QUERY | — | Vision_ClsReply() | — |
| VISION_GOT_STOP | 条件跳转+Vision_Go() | 清扫描位+Vision_Go() | — |
| VISION_GOT_POS | 移动+记录实际坐标 | 移动+Vision_Go() | 移动+累加偏移 |
| VISION_GOT_ERR_RETRY | — | — | Vision_Go() 重试 |
| VISION_DONE | 建系(三点LS)+残差验证 | R矫正→HOST_PICK | R矫正→HOST_MOVE_TO_PCB |
| VISION_ERROR | →HOST_ERROR | 分级处理(重试/子位扫描/跳过/WAIT_REFILL) | err3_8→回散料区重取 / 其他→MOVE_TO_PCB(容错) |



## 二十三、2026-06-20 会话 — PnP 架构完善与代码治理

### 23.1 代码归位（第一层地基）

将核心运动函数从测试文件迁移到正式模块：

| 函数 | 迁移前 | 迁移后 |
|------|--------|--------|
| `move_xy_relative` | `Task/app_test.c` | `Task/app_motion.c` |
| `axis_stop` | `Task/app_test.c` | `Task/app_motion.c` |
| `disable_sync_stop` | `Task/app_test.c` | `Task/app_motion.c` |
| `s_cmd_interrupted` | `Task/app_test.c` | `Task/app_motion.c` |

`app_test.h` 改为 `#include "app_motion.h"` 透传引用。

**重要发现**：`Motor_Init()` 调用 `motorSyncEnable(0)` 关闭了同步模式。三轴独立执行，每个 `positionMode3Run` 直接通过 `CAN_Transmit_Data` 发送，无需 `motorSyncTrigger`。`disable_sync_stop` 的三个 `axis_stop` 各自立即生效，函数名中的 "sync" 易误解但功能正确。§11.7/§11.8 的旧分析基于"同步模式开启"假设，与实际代码不符。修正后的注释已写入 `app_motion.c`。

### 23.2 视觉超时保护（第二层）

`app_vision.h/c` 新增：
- `Vision_IsTimedOut()` — 30s 超时检测
- `Vision_ForceIdle()` — 强制复位视觉状态机

三级降级策略：
| 进程 | 超时行为 |
|------|----------|
| P2 | → HOST_ERROR（Mark 建系不可跳过） |
| P1 | 子位扫描 → 跳过当前元件 → 连续 3 次失败进 WAIT_REFILL |
| P3 | → HOST_MOVE_TO_PCB（容错跳过，贴装优先于卡死） |

### 23.3 吸取确认（第二层）

`pick_component()` 返回值改为 `bool`，内部增加真空检测分支。`void vacuum_ok(void)` 用 `__weak` 占位（当前始终返回 true），后续接入 GPIO/ADC 传感器时覆盖即可，不修改调用侧。

HOST_PICK 状态：吸取失败 → 重启 P1 视觉搜索，不浪费 P3 周期。

### 23.4 ABORT 增强（第二层）

增强 `handle_debug_cmd` 中 `HCMD_ABORT` 处理：
- 停止 JOG（若激活）
- `disable_sync_stop()` 急停三轴
- 关气泵 + 短吹气释放元件
- Z 轴回安全角
- `Heater_SendStop()` 停止加热台
- 回到 HOST_DEBUG

### 23.5 R 轴归零修正（第二层）

**Bug**：`host_correct_r_from_vision` 调用 `r_axis_rotate(ang)` 传入的是视觉检测到的**增量角度**，但 `r_axis_rotate` 是**绝对角度**接口。P3 矫正时 `g_cur_r_angle=5.0`，视觉返回 +0.5° 残差，调用 `r_axis_rotate(0.5)` 导致 nozzle 从 5.0° 倒转 4.5°。

**修复**：`r_axis_rotate` 后调用 `r_axis_set_zero()`，P1/P3 矫正均通过同一辅助函数，一次修改两端生效。

> **2026-06-20 更新（ 24）：** `g_cur_r_angle` 已被 `Coord_Get().r` 取代，`r_axis_rotate` 改为 `int` 返回值。详见  24.2。

### 23.6 代码债务清理（D1~D5）

| 项 | 文件 | 改动 |
|----|------|------|
| D1 | `driver_motor.c` | `runFail` → 设置 `g_motor_error`；`runOK` → 空操作 |
| D2 | `driver_can.h/c` | 删除 7 个 `CAN1_0x*_Tx_Data` 数组、`CAN_Supercap`、`CAN_RxDone`、`CAN_ID`、`canCRC_ATM` 函数 |
| D2 | `driver_motor.c` | 删除 `realTimeLocation`、`runSpeed` |
| D3 | `driver_motor.c` | 删除 `MotorController_t` 结构体、`g_current_*state`、`g_motor_ctrl`、`motor_send_move_cmd` |
| D4 | — | LPUART1 `hdmarx=NULL` 是 CubeMX 配置，无需代码改动 |
| D5 | `app_motion.c` | `disable_sync_stop` 注释修正：同步模式描述 → 非同步独立急停 |

### 23.7 电机异常分级（第三层 3.2）

`app_motion.h` 新增 `MotorError_t` 枚举：`MOTOR_OK` / `MOTOR_ERR_TIMEOUT` / `MOTOR_ERR_LIMIT`。

- `move_xy_relative` 在 `return -1` 前记录 `TIMEOUT`，`return -2` 前记录 `LIMIT`
- `safe_move_to` 收到 -1 时自动重试一次（CAN 丢帧可恢复），两次超时才升级为错误
> **2026-06-20 更新（ 24）：** TIMEOUT 已改为原地重试（不进 ERROR），仅 LIMIT 进 HOST_ERROR。详见  24.3。

- Host_Task 错误日志区分 "Motor TIMEOUT" vs "Motor LIMIT/BLOCK"`r`n`r`n> **2026-06-20 更新（§24）**：TIMEOUT 行为已改为原地重试（不进 ERROR），仅 LIMIT 进 HOST_ERROR。详见 §24.3。

### 23.8 分级运动速度（第四层 4.1）

`app_host.c` 新增三级速度：

| 常量 | 值 | 场景 |
|------|-----|------|
| `PNP_SPEED_FAST` | 300 | 长距离移动（→下相机/→PCB/→零点） |
| `PNP_SPEED` | 300 | 通用（网格扫描、散料区定位） |
| `PNP_SPEED_FINE` | 100 | 视觉迭代微调 |
| `PNP_ACC_FINE` | 10 | 微调加速度（更平缓） |

3 个视觉微调点 + 4 个长距离移动点已替换对应常量。

### 23.9 加热台联动（第五层 5.3）

新增 `AUTO_HEAT ON/OFF` 命令开关。流程：
1. `HOST_DONE` 后若 `g_auto_heat=true` → `Heater_SendStart()` → 进入 `HOST_REFLOW`
2. `HOST_REFLOW` 每 200ms 轮询加热台状态
3. `COMPLETE/IDLE` → 通知上位机 `REFLOW_DONE` → 回 `HOST_DEBUG`
4. `HEATER_STATE_ERROR` → `REFLOW_ERROR` → `HOST_ERROR`
5. ABORT 在任何状态都会 `Heater_SendStop()`

新增 `HOST_REFLOW` 状态枚举值、`HCMD_AUTO_HEAT` 命令解析。

### 23.10 SPI Flash 运行日志（第五层 5.4）

新增 `Task/app_logger.h` 和 `Task/app_logger.c`。

**存储布局**：W25Q64 倒数第二扇区 `0x7FE000`（4KB），8 字节头 + 255 条 × 16 字节。满后整扇区擦除回绕。

**事件码**：
| 码 | 事件 | 数据 |
|----|------|------|
| 0x01 | LOG_PNP_START | comp_count(2B) + mark_count |
| 0x02 | LOG_PNP_DONE | comp_count(2B) |
| 0x03 | LOG_PNP_ERROR | error_type + comp_index(2B) |
| 0x04 | LOG_MOTOR_ERROR | err_detail (1=TIMEOUT, 2=LIMIT) |
| 0x05 | LOG_HEATER_START | 全 0 |
| 0x06 | LOG_HEATER_DONE | final_state |
| 0x07 | LOG_ABORT | 全 0 |

8 个调用点分布在 `app_host.c` 的 `download_done`、`HOST_DONE`、`HOST_ERROR`、电机异常检测、HOST_REFLOW 启动/完成/失败、ABORT。

W25Q64 芯片确认为 `W25Q64JV` 系列。现有驱动 `W25Q64_Read/Write/Erase` 经验证与手册一致（Sector Erase 0x20、4KB 扇区、默认无保护）。日志扇区 `0x7FE000` 不与标定扇区 `0x7FF000` 冲突。

### 23.11 涉及文件

| 文件 | 改动类型 |
|------|----------|
| `Task/app_motion.h` | 新增 `MotorError_t`、`g_motor_error_detail`、`axis_stop`/等声明、`pick_component` 改 `bool` |
| `Task/app_motion.c` | 迁移三个运动函数、`pick_component` 真空逻辑、`safe_move_to` 重试、错误类型记录、`disable_sync_stop` 注释修正 |
| `Task/app_host.h` | 新增 `HOST_REFLOW` 状态 |
| `Task/app_host.c` | 视觉超时×3、pick 失败处理、ABORT 增强、R 轴归零、速度分级×7、电机异常日志、加热台联动、Logger 调用×8 |
| `Task/app_vision.h` | 新增 `Vision_IsTimedOut`/`Vision_ForceIdle` |
| `Task/app_vision.c` | 超时跟踪 + 两个函数实现 |
| `Task/app_uart_parser.h` | 新增 `HCMD_AUTO_HEAT` |
| `Task/app_uart_parser.c` | 解析 `AUTO_HEAT` |
| `Task/app_test.h` | 运动函数声明替换为 `#include "app_motion.h"` |
| `Task/app_test.c` | 删除三个运动函数 + `s_cmd_interrupted` |
| `Task/app_logger.h` | **新增** — 事件码 + 结构体 + API |
| `Task/app_logger.c` | **新增** — 扇区管理 + Log_Init/Log_Write |
| `Drivers/ZeMCU-G4/driver_can.h` | 删除 10 行未使用 extern |
| `Drivers/ZeMCU-G4/driver_can.c` | 删除 8 个数组 + 2 个变量 + `canCRC_ATM` |
| `Drivers/ZeMCU-G4/driver_motor.c` | `runFail`/`runOK` 修正 + 删除死代码 |

### 23.12 关键设计决策

- **R 轴归零而非相对旋转**：保持 `r_axis_rotate` 绝对角度语义不变，矫正后归零。比新增相对旋转接口更简洁。
- **真空检测用 `__weak`**：硬件未就绪仍可编译运行，后续只需覆盖一个函数。
- **视觉超时降级而非统一进 ERROR**：P3 超时仍继续贴装——贴歪比不贴强，且贴装精度由 Mark 建系保证。
- **电机超时重试一次**：CAN 丢帧是偶发软故障，重试可恢复。限位/堵转不重试。
- **日志扇区满即擦**：255 条/会话绰绰有余，无需双扇区交替或磨损均衡。
- **加热台联动为可选**：`AUTO_HEAT OFF` 保持旧行为，不影响现有调试流程。


> **前置更新说明：** 以下改动修正/替换了已有文档中的部分内容：
> - §23.5（R 轴归零）→ `g_cur_r_angle` 已删除，统一由 `Coord_Get().r` 管理
> - §23.7（电机异常分级）→ TIMEOUT 行为已改为原地重试，不再进 HOST_ERROR
> - §七（关键数据结构）→ 新增 `MachineCoord_t`（app_motion.h）
> - §五（任务间通信）→ 新增 `g_coord_mutex`


## 二十四、2026-06-20 会话 — 运动控制架构治理

> **前置更新说明：** 以下改动修正/替换了已有文档中的部分内容：
> - §23.5（R 轴归零）→ g_cur_r_angle 已删除，统一由 Coord_Get().r 管理
> - §23.7（电机异常分级）→ TIMEOUT 行为已改为原地重试，不再进 HOST_ERROR
> - §七（关键数据结构）→ 新增 MachineCoord_t（app_motion.h）
> - §五（任务间通信）→ 新增 g_coord_mutex\r

### 24.1 座标核心 — MachineCoord_t 统一座标系

**问题：** `g_cur_x/y` 散落在 `Host_Task` 的 static 局部变量中，R 轴角度在 `app_motion.c` 的 `g_cur_r_angle` 单独维护，Z 轴未记录。无互斥保护，失步无感知。

**方案：** 在 `app_motion.h/c` 中新增 `MachineCoord_t` 单例 + 7 个 `Coord_*` 函数：

```c
typedef struct {
    int32_t x, y;    // XY 步数
    float   r;       // R 轴角度 (deg)
    float   z;       // Z 轴角度 (deg)
    bool    homed;   // 是否归零
    bool    valid;   // 座标是否可信
} MachineCoord_t;

void Coord_Init(void);           // 幂等，可多任务调用
MachineCoord_t Coord_Get(void);  // 返回副本，mutex 保护
void Coord_UpdateXY/R/Z(...);    // 到位后更新，同时置 valid=true
void Coord_SetHome(void);        // 归零：x=y=r=0, homed=valid=true
void Coord_Invalidate(void);     // 失步标记：valid=false，保留旧值供诊断
```

**关键设计：**
- 内部 FreeRTOS 互斥锁保护读写
- `Coord_Get()` 返回副本，调用方不持有锁——避免死锁
- `Coord_Invalidate` 不修改坐标值，只设 `valid=false`——旧值仍可用于绝对移动的 dx 计算
- `Coord_Init` 幂等——`Host_Task` 和 `StartCamTestTask` 均可安全调用

**涉及的签名变更：**
- `safe_move_to` 去掉了 `int32_t *cur_x, *cur_y` 参数
- `move_xy_relative` 同理
- 所有调用点改为读 `Coord_Get()` 获取当前位置

**涉及文件：** `app_motion.h`, `app_motion.c`, `app_host.c`, `app_test.c`

### 24.2 r_axis_rotate 改造

| 改动 | 说明 |
|------|------|
| 删除 `static float g_cur_r_angle` | 统一由 `Coord_Get().r` 管理 |
| `r_axis_set_zero()` → `Coord_UpdateR(0.0f)` | — |
| `r_axis_rotate` 改 `int` 返回值 | 0=旋转成功, -1=角度小于阈值跳过 |
| 内部读 `Coord_Get().r` 算 delta | 替代旧的 `g_cur_r_angle` |

### 24.3 错误恢复分级（重写 §23.7）

**旧行为：** TIMEOUT 和 LIMIT 都进 `HOST_ERROR`，只是日志不同。

**新行为：**

| 错误类型 | 行为 | 理由 |
|----------|------|------|
| `MOTOR_ERR_TIMEOUT` | 不改变 `g_state`，下轮主循环重试当前状态。视觉相关状态（MARK_ALIGN/FIND_COMP/OFFSET_CHECK）先调 `Vision_ForceIdle()` 重置相机 | CAN 丢帧是偶发软故障，重试可恢复 |
| `MOTOR_ERR_LIMIT` | `z_safe()` → `g_state = HOST_ERROR` | 堵转/限位是硬件故障，需人工介入 |

### 24.4 Coord_Invalidate 全覆盖

8 处调用确保所有导致电机位置不确定的操作都标记座标不可信：

| 位置 | 触发条件 |
|------|---------|
| JOG 方向切换 ×4 | 旧 JOG 被新方向打断，`disable_sync_stop` 后位置未知 |
| `HCMD_MOVE_STOP` | 用户主动停止 JOG |
| `HCMD_ABORT` | 紧急中止 |
| P3 超时 | 下相机视觉验证失败，R 轴位置未经确认 |
| P3 错误 | 同上 |

### 24.5 handle_calib_cmd 拆分

`handle_debug_cmd` 中 10 个标定命令（`SET_SCATTER_AREA` ~ `SAVE_CALIB`）抽成独立函数 `handle_calib_cmd`（~75 行），返回 `bool`：

```c
static bool handle_calib_cmd(HostParsed_t *cmd) { ... }

// handle_debug_cmd 顶部：
if (handle_calib_cmd(cmd)) { g_during_cmd = false; return; }
```

无新文件。`handle_debug_cmd` 从 ~270 行缩到 ~200 行。

### 24.6 PnP step 函数规范化

`Host_Task` 主循环中 7 个内联 case 块抽成独立 step 函数：

| 函数 | 对应状态 | 职责 |
|------|---------|------|
| `pick_step()` | HOST_PICK | 吸取元件，失败→重启 P1 |
| `move_to_bottom_step()` | HOST_MOVE_TO_BOTTOM_CAM | 移到下相机→启动 P3 |
| `move_to_pcb_step()` | HOST_MOVE_TO_PCB | 坐标变换 + R 轴旋转 + XY 移到贴装位 |
| `place_step()` | HOST_PLACE | 贴装→下一元件或 DONE |
| `done_step()` | HOST_DONE | 回零 + 可选自动回流焊 |
| `reflow_step()` | HOST_REFLOW | 轮询加热台状态 |
| `error_step()` | HOST_ERROR | 等 RESUME/ABORT，30s 超时自回 DEBUG |

主循环 switch 缩至 14 行统一风格：

```c
switch (g_state) {
case HOST_HOME:              osDelay(100);            break;
case HOST_DEBUG:             osDelay(10);             break;
case HOST_DOWNLOADING:       osDelay(10);             break;
case HOST_MARK_ALIGN:        mark_align_step();       break;
case HOST_FIND_COMP:         find_comp_step();        break;
case HOST_PICK:              pick_step();             break;
case HOST_MOVE_TO_BOTTOM_CAM: move_to_bottom_step();  break;
case HOST_OFFSET_CHECK:      offset_check_step();     break;
case HOST_MOVE_TO_PCB:       move_to_pcb_step();      break;
case HOST_PLACE:             place_step();            break;
case HOST_DONE:              done_step();             break;
case HOST_REFLOW:            reflow_step();           break;
case HOST_WAIT_REFILL:       osDelay(200);            break;
case HOST_ERROR:             error_step();             break;
}
```

### 24.7 HOST_ERROR 策略 — 选项 A

**旧行为：** `HOST_ERROR` 当场调用 `Heater_SendStop + z_safe`，清零 `g_comp_count/mark_count/index`，跳 `HOST_DEBUG`。上位机无窗口发 `RESUME`。

**新行为（选项 A）：**

```c
static void error_step(void) {
    if (!g_error_entered) {
        Heater_SendStop(); z_safe();
        Log_Write(LOG_PNP_ERROR, ...);
        host_send("ERROR");           // 通知上位机
        g_error_entered = true;
        g_error_start_tick = osKernelGetTickCount();
    }
    // 30s 超时自动回 DEBUG
    if (tick - g_error_start_tick >= 30s) {
        g_comp_count = 0; ... ; g_error_entered = false;
        g_state = HOST_DEBUG;
    }
}
```

`RESUME` 命令处理增强：
- 清除 `g_error_entered` 标志
- 检查 `g_comp_count > 0`（无元件数据时拒绝恢复）
- `ABORT` 也清除 `g_error_entered`

### 24.8 P2 Mark 建系修复

审查 `mark_align_step` 后修复了 2 个实际隐患：

**Bug 2 — 三个 HOST_ERROR 出口未重置视觉：** 网格穷尽、跳转搜索超时、VISION_ERROR 三处直接 `g_state = HOST_ERROR` 而不调 `Vision_ForceIdle()`。加上了。

**Bug 3 — VISION_DONE 未验证 Mark 数量：** 建系前直接读 `g_marks_actual[0..2]`，如果 CSV 少于 3 个 Mark，数据是垃圾。加了守卫：

```c
if (g_mark_count < P2_MARK_COUNT) {
    Vision_ForceIdle();
    g_state = HOST_ERROR;
    break;
}
```

**Bug 1（网格切换未终止旧 P2 会话）：** 审查 `Vision_Start` 实现后发现内部调了 `reset_all()`，安全——不存在。

### 24.9 启动路径守卫

| 问题 | 修复 |
|------|------|
| `HOST_HOME` 状态下可发 CSV 开始下载（Coord 未归零） | 下载入口改为只允许 `HOST_DEBUG` |
| `SET_ORIGIN` 在 PnP 途中可被意外调用，毁掉座标系 | 加状态守卫：仅 `HOST_HOME` 或 `HOST_DEBUG` 才执行归零 |

### 24.10 app_host.c 分区导航

文件顶部增加分区注释，划分 6 个逻辑区域（§1 常量 ~ §6 主循环），便于快速定位。

### 24.11 涉及文件

| 文件 | 改动 |
|------|------|
| `Task/app_motion.h` | 新增 `MachineCoord_t` + 7 个 `Coord_*` 声明；`safe_move_to`/`move_xy_relative` 去指针参数；`r_axis_rotate` 改 `int` |
| `Task/app_motion.c` | 座标核心实现（~80 行）；`move_xy_relative` 内部接入 Coord；`safe_move_to` 去指针参数；`z_*`/`r_axis_*` 接入 Coord；删除 `g_cur_r_angle`；`MotionTask_Func` 同步更新 |
| `Task/app_host.c` | `g_cur_x/y` → `Coord_Get()`；`Coord_Init/SetHome` 调用；错误分级；`handle_calib_cmd` 拆分；7 个 step 函数；主循环 14 行 switch；8 处 `Coord_Invalidate`；P2 修复；启动路径守卫；HOST_ERROR 策略；分区导航 |
| `Task/app_test.c` | 局部 `cur_x/y` → `Coord_Get()`；`cam_test_run` 参数精简；`Coord_Init` 调用 |

### 24.12 设计决策记录

- **不建新文件：** 座标核心放 `app_motion.c`，标定命令/step 函数留 `app_host.c`。单人嵌入式项目，组织良好的单文件优于互相 extern 的多文件。
- **Coord_Invalidate 只标记不修改值：** 失效的旧坐标仍可用于绝对移动的 delta 计算，到位后自动恢复可信。
- **Planner 层不做：** MKS 伺服内置加减速控制（`acc` 参数），PnP 单段移动不需要轨迹规划。
- **独立运动任务不做：** PnP 流程天然串行——每次运动后必须等视觉/传感器反馈。阻塞式原语已够用。
- **多板支持不做：** 当前一次贴一块。
- **软限位搁置：** 需要在标定流程中先加 `SET_X_RANGE`/`SET_Y_RANGE` 命令。

## 二十五、2026-06-20 会话 — CSV 解析健壮性修正

### 25.1 背景

对照上位机规范文档《单片机端 CSV 数据处理规范》与实测 CSV 文件
（PickAndPlace_Mark_贴装测试板_2026_06_14.csv），
审查 `app_host.c` 中 `parse_csv_line` 的实现，发现两个问题。

### 25.2 问题 1 — 表头检测仅匹配带引号形式（P1）

**规范描述：** 表头行为 `"Designator"\t"Device"\t...`（字段以双引号包裹）。

**实测 CSV：** 该 PCB 设计工具输出的 CSV 表头为 **无引号** 格式
`Designator\tDevice\tFootprint\t...`。
两个不同日期的 CSV 文件均保持一致，说明是工具的统一输出格式。

**原代码**（`parse_csv_line`，第 229 行）：
```c
if (len >= 12 && memcmp(line, "\"Designator\"", 12) == 0) {
```
仅匹配 `"Designator"`（12 字节含引号），
实测 CSV 的 `Designator`（10 字节无引号）无法命中。

**为何未暴露：** 代码有 fallback `g_header_parsed = true`，
之后走 SMD 字段过滤——表头第 12 列值为 `SMD`，
`strcmp("SMD", "Yes") != 0` 且 `strcmp("SMD", "MARK") != 0`，
被当作无效行丢弃。这是偶然正确，依赖列名巧合。

**修复（第 229 行）：**
```c
if ((len >= 12 && memcmp(line, "\"Designator\"", 12) == 0) ||
    (len >= 10 && memcmp(line, "Designator", 10) == 0)) {
```
- 两条分支各自带 `len >= N` 长度守卫，`len < 10` 时不执行 `memcmp`，无越界读取
- 单行复合条件，与同文件 1258-1260 行 `raw_len == N && memcmp` 模式一致
- 引号/无引号两路覆盖，不再依赖 SMD 列名巧合

### 25.3 问题 2 — Footprint 缓冲区不足以容纳长封装名（P2）

**实测数据：**
- `"LED-SMD_6P-L5.0-W5.0-P1.6-LS5.4-TL"` — 38 字符
- `"CAP-SMD_BD6.3-L6.6-W6.6-LS7.3-FD-H7.7"` — 37 字符

**原代码：**
- `Component_t.footprint` — `char[32]`（`app_host.h:24`）
- 局部 `tmp` — `char[32]`（`app_host.c:265`）

38 字符的封装名在 `csv_get_field` 中被截断到 31 字符（`out_max - 1`），
丢失后缀。当前 `footprint_to_class_id` 仅比较前 2-3 字符用于散料区分类，
截断暂不影响功能。但若后续需要完整封装名做匹配
（feeder 分配、视觉模板切换），静默截断会成为隐蔽 bug。

**修复：**

| 位置 | 旧值 | 新值 |
|------|------|------|
| `app_host.h:24` `footprint` | `char[32]` | `char[48]` |
| `app_host.c:265` `tmp` | `char[32]` | `char[48]` |

48 字节对最长 38 字符的封装名留有 26% 余量。
`tmp` 与 `footprint` 尺寸一致，
`csv_get_field(line, len, 2, tmp, sizeof(tmp))` 的截断上限与结构体字段对齐。
`MAX_COMPONENTS=128` + `MAX_MARKS=8` 共 136 实例，
结构体从 52 字节增至 68 字节（+16），总计增加约 2.1KB RAM，
占 STM32G474 128KB SRAM 的 1.7%。

### 25.4 完整流程验证

代入实测 CSV 文件逐行推演（29 元件 + 1 SMD="No" + 3 Mark），
覆盖以下场景：

| 场景 | 输入 | 结果 |
|------|------|------|
| 表头（引号） | `"Designator"\t...` | 第一分支命中，跳过 ✅ |
| 表头（无引号） | `Designator\t...` | 第二分支命中，跳过 ✅ |
| 极短行（len<10） | `"X"` | 两分支长度守卫均不满足，走 SMD 过滤后丢弃 ✅ |
| 空行 | `\n` | LineParser 不产生调用 ✅ |
| 数据行 SMD="Yes" | `"C1"\t...\t"Yes"` | 存入 `g_components[]` ✅ |
| 数据行 SMD="No" | `"H1"\t...\t"No"` | 丢弃 ✅ |
| Mark 行 SMD="MARK" | `""\t""\t"Mark1"...` | 存入 `g_marks[]` ✅ |
| 长封装名（38 字符） | `LED-SMD_6P-...` | `tmp[48]` 完整读入，不截断 ✅ |

### 25.5 涉及文件

| 文件 | 改动 |
|------|------|
| `Task/app_host.c` | 表头检测增加 `len >= 10` 守卫 + 无引号分支（第 229 行）；`tmp[32]` → `tmp[48]`（第 265 行） |
| `Task/app_host.h` | `footprint[32]` → `footprint[48]`（第 24 行） |

### 25.6 设计决策记录

- **双 memcmp 而非跳过引号方案：** 跳过引号的方案增加了局部变量和控制流，
  边界安全问题未根本解决（短行跳过引号后依然越界），
  且与代码库既有的 `memcmp + 显式长度守卫` 模式不一致。
  保持双分支 `memcmp` 各带独立长度守卫。
- **48 字节选型：** 当前最长封装名 38 字符，
  48 = 38 × 1.26，留有合理余量。不盲目扩到 64 以节省 RAM。
- **不改 `csv_get_field` 内部逻辑：**
  该函数通过 `out_max` 参数由调用方控制截断上限，接口已足够通用。


## 二十六、2026-06-20 会话 — P3 吸嘴空取检测 (err3_8)

> 历史说明：本节描述旧版 `err3_8` 回散料区 → P1 重识别流程；2026-08-12 起主控改为第一次同点重吸 → P3，第二次才强制 P1 重识别，第三次进 `HOST_ERROR`。当前行为以 HISTORY §60 / AGENTS.md §4.2 P3 为准。

### 26.1 背景

上位机（MaixCAM）对 P3 进程新增 Phase0 吸嘴圆检测。P3 启动后 Cam 先检测是否有吸嘴圆（10帧 HoughCircles，阈值≥7帧判为空取），而非直接进入元件对准。若检测到吸嘴圆（即吸嘴上无元件），Cam 发送 `err3_8` 并结束 P3，不再进入 Phase1/Phase2。

主控无需新增发送指令，仅需处理新增的接收信号。

### 26.2 协议变更

**P3 流程（更新后）：**
```
Host 发 "p3"
  → Cam Phase0：吸嘴圆检测（~100-200ms）
      ├─ 检测到吸嘴圆 → Cam 发 "err3_8" → P3 结束
      └─ 未检测到吸嘴圆 → 进入 Phase1/Phase2（与原流程相同）
```

**新增错误码：**

| 错误码 | 触发时机 | 含义 | 主控应对 |
|--------|---------|------|---------|
| `err3_8` | P3 Phase0 | 吸嘴未吸取元件（检测到吸嘴圆形） | 重新吸取该元件，不可降级贴装 |

> `err3_8` 与 `err3_1`~`err3_7` 不同：**不允许降级到 P1 精度继续贴装**，因为吸嘴上根本没有元件。

### 26.3 固件实现

**未修改的文件：** `app_vision.c` / `app_vision.h` — `process_p3_frame` 的通用错误分支已自动兼容：非 `err3_3` 的 `err*` 帧统一走 `VISION_ERROR` + `save_error(str)`，`Vision_GetError()` 可直接返回 `"err3_8"` 字符串。

**修改文件：** `Task/app_host.c`（+15 行 / -3 行），`Task/app_test.c`（+43 行 / -38 行）。

### 26.4 Host_Task 新增变量

```c
static int g_p3_nozzle_retry = 0; /* P3 吸嘴空取重试计数 */
```

位于 `g_p1_retry_count` 旁，与 P1 重试计数器平行管理。

### 26.5 offset_check_step 核心逻辑

```c
case VISION_ERROR: {
    const char *err = Vision_GetError();
    Component_t *c = &g_components[g_comp_index];
    int cl;
    if (err[0] == 'e' && err[1] == 'r' && err[2] == 'r' &&
        err[3] == '3' && err[4] == '_' && err[5] == '8' && err[6] == '\0') {
        g_p3_nozzle_retry++;
        if (g_p3_nozzle_retry >= 3) {
            PrintDebug("[HOST] P3 nozzle empty x3, check feeder!\r\n");
            g_state = HOST_ERROR;
        } else {
            PrintDebug("[HOST] P3 nozzle empty, retry pickup (%d/3)\r\n", g_p3_nozzle_retry);
            /* 回退散料区重新 P1 找取同一元件 */
            cl = component_cell(c);
            g_p1_scan_pos = 0;
            safe_move_to(g_scatter_subpos[cl][0][0], g_scatter_subpos[cl][0][1],
                         PNP_SPEED, PNP_ACC);
            Vision_Start(VCMD_P1, footprint_to_class_id(c->footprint));
            g_state = HOST_FIND_COMP;
        }
    } else {
        PrintDebug("[HOST] Offset check ERROR: %s\r\n", err);
        Coord_Invalidate();
        g_state = HOST_MOVE_TO_PCB;  /* 容错：仍然贴装 */
    }
    break;
}
```

**关键设计要点：**

1. **字符串比较含终止符**：`err[6] == '\0'` 防止误匹配 `err3_80` 等变体，与 `app_vision.c` 中 `err3_3` 的判断模式一致。

2. **必须移到散料区再启 P1**：err3_8 时机器在下相机位置（`move_to_bottom_step` 已移动），而 P1 需要上相机看向散料区。回退路径必须遵循 `component_cell → g_p1_scan_pos=0 → safe_move_to(scatter) → Vision_Start(P1)` 四步模式，与项目中所有 6 处 HOST_FIND_COMP 过渡点一致。

3. **错误分层**：err3_8 与其他 P3 错误走不同分支——err3_8 回退重取，err3_1~err3_7 降级贴装（保持原有容错逻辑）。err3_3 走独立路径（VISION_GOT_ERR_RETRY），互不干扰。

### 26.6 计数器生命周期

`g_p3_nozzle_retry` 共 8 个引用点：

| 位置 | 操作 | 说明 |
|------|------|------|
| 静态声明 | `= 0` | 编译期初始化 |
| ABORT 处理 | `= 0` | 流程中止清零 |
| P3 超时 | `= 0` | 超时降级贴装，清零重试计数 |
| P3 VISION_DONE | `= 0` | P3 成功完成清零 |
| err3_8 命中 | `++` | 吸嘴空取累加 |
| ≥3 判断 | 读 | 触发 HOST_ERROR |
| 日志输出 | 读 | 重试次数显示 |
| ERROR 超时 | `= 0` | 30s 无人干预回 DEBUG 清零 |

**设计原则：** 计数器不在 `move_to_bottom_step` 中复位（否则 err3_8 → P1 → pick → move_to_bottom 会将计数器清零，导致重试循环永远达不到 3）。仅在 P3 成功或流程终止时清零。

### 26.7 典型路径

```
正常：P3 → VISION_DONE → 清零 → 贴装 → 下一元件
重试成功：err3_8(1) → 移散料区 → P1+pick → P3 → VISION_DONE → 清零
重试耗尽：err3_8(1) → ... → err3_8(2) → ... → err3_8(3) → HOST_ERROR
超时兜底：err3_8(1) → P3超时 → 清零 → 降级贴装 → 下一元件
其他错误：err3_1~err3_7 → 降级贴装（计数器不变）
```

### 26.8 视觉状态覆盖矩阵更新

| VisionState | offset_check_step（更新后） |
|-------------|---------------------------|
| VISION_GOT_ERR_RETRY | Vision_Go() 重发（err3_3） |
| VISION_GOT_POS | 移动+累加偏移 |
| VISION_DONE | R矫正+清零计数器→HOST_MOVE_TO_PCB |
| VISION_ERROR（err3_8） | 计数器++ → <3回散料区重取 / ≥3进HOST_ERROR |
| VISION_ERROR（其他） | →HOST_MOVE_TO_PCB（容错降级） |

### 26.9 涉及文件

| 文件 | 改动 |
|------|------|
| `Task/app_host.c` | 新增 `g_p3_nozzle_retry` 计数器；扩展 `offset_check_step` 的 VISION_ERROR 分支；4 处清零复位点（VISION_DONE、超时、ABORT、ERROR_TIMEOUT） |
| `Task/app_test.c` | `cam_test_run` VISION_ERROR 增加 err3_8 识别；`StartCamTestTask` 吸取+P3 段重构为 while 重试循环 |
| `Task/app_vision.c` | 无需修改 — `process_p3_frame` 通用错误分支自动兼容 |
| `Task/app_vision.h` | 无需修改 |

### 26.10 设计决策记录

- **不新增 VisionState**：使用现有 `VISION_ERROR` + 字符串比较区分 err3_8，避免枚举膨胀。比较含 `\0` 终止符确保精确匹配。
- **计数器跨元件累积**：`g_p3_nozzle_retry` 不在 VISION_DONE 以外的地方清零（除流程终止）。`move_to_bottom_step` 不干预，确保 err3_8 → P1+pick → P3 的重试循环能正确累加。
- **err3_8 不触发降级贴装**：与其他 P3 错误不同，吸嘴空取意味着贴装头上无元件，降级贴装（HOST_MOVE_TO_PCB）无意义。直接回退到 HOST_FIND_COMP 重新 P1 找取。
- **重试上限 3 次**：与 P1 重试次数一致。连续 3 次吸嘴空取表明供料器/气泵硬件问题，需人工介入。

### 26.11 StartCamTestTask 同步更新

测试任务同步适配 err3_8 吸嘴空取检测：

**cam_test_run 改进：** VISION_ERROR 分支新增 err3_8 识别，输出 `"nozzle empty (err3_8)"` 而非通用错误信息。

**StartCamTestTask 结构重构：** 吸取+P3 验证段从平铺 if-else 改为带重试循环的结构：

```
保存散料区位置 scatter_pos
while (retry < 3 && !p3_ok) {
    if (retry > 0) { 移回 scatter_pos → P1 → R矫正 }
    吸取 → 移到下相机 → cam_test_run(P3)
    if (err3_8) { retry++; 吹气清理; continue }
    if (其他错误) { break }
    p3_ok = true
}
if (p3_ok) { P3二次矫正 + 释放 }
else if (retry >= 3) { 告警检查供料器 }
```

**关键设计：** 使用 `Coord_Get()` 在 P1 执行后保存散料区位置（`scatter_pos`），err3_8 重试时移回此处重新 P1。不依赖 `g_scatter_subpos`（测试任务中该数组可能未初始化）。

## 二十七、2026-06-21 会话 — 离散运动控制修复

### 27.1 背景

上位机离散移动命令（MOVE_UP/DOWN/LEFT/RIGHT）持续返回 `ret=-2`（堵转/限位），而连续移动（JOG）正常。经 CAN 总线帧级别调试（`driver_can.c` 临时开启 TX/RX 日志），定位到三层根因并逐一修复。

### 27.2 修复 1 — positionMode1Run/2Run 帧长度错误

**根因：** `CAN_Transmit_Data` 入口检查 `Length > 7` 直接拒绝（`driver_can.c:95`）。MKS 电机协议帧 ≤7 字节有效数据 + 1 字节 CRC = 8 字节 CAN 帧。
`positionMode2Run`（0xF4 相对定位）和 `positionMode1Run`（0xFD）均传 `Length=8` 被拒，指令从未上总线。

**修复：** `driver_motor.c` 两处 `CAN_Transmit_Data(..., 8)` → `CAN_Transmit_Data(..., 7)`，与 `positionMode3Run` 一致。

**涉及文件：** `Drivers/ZeMCU-G4/driver_motor.c:232,255`

### 27.3 修复 2 — 到位阈值导致小步长不回报完成

**根因：** `motorSetArrivalThreshold` 默认 200 步（`0x00C8`）。从零点发绝对/相对 100 步移动，距离 100 < 阈值 200，MKS 电机判定"已在目标"——不动作，不发 `F4/F5 02` 完成响应。`CAN_Process_Task` 永远收不到完成标志，等待循环超时。

**修复：** 到位阈值 200 → 50 步（`0x0032`），兼顾检测灵敏度与伺服稳定性。50 步 @ 100步/mm = 0.5mm，对贴片机足够精确且不会引起电机振荡。

**涉及文件：** `Drivers/ZeMCU-G4/driver_motor.c:361`

### 27.4 修复 3 — 同步触发从逐个电机改为广播

**根因：** `move_xy_relative` 向 X1/X2 **分别**发送 `0x4B` 同步触发：
```c
CAN_Transmit_Data(&hfdcan1, X1_ADDR, tx, 1);  // X1 先动
CAN_Transmit_Data(&hfdcan1, X2_ADDR, tx, 1);  // X2 后动
```
双 X 轴机械耦合——X1 先启动时 X2 仍锁死，X1 堵转回报 `0x03`，`CAN_Process_Task` 设 `EVENT_ANY_ERROR` → `ret=-2`。

MKS 官方文档明确规定：`0x4B` 同步执行指令**必须发广播地址 `0x00`**，所有等待同步的电机同时启动。

**修复：** 三条逐个触发替换为 `motorSyncTrigger(0)`（广播），与 JOG 一致。

**涉及文件：** `Task/app_motion.c:218`

### 27.5 修复 4 — 等待策略从事件标志改为时长估算

**根因：** `osEventFlagsWait` 事件标志在调试中反复出现异常（`flags=0xE` 凭空出现，包含 `X2_DONE|Y_DONE|ANY_ERROR`）。同时在 ISR 或任务上下文中调用大量 `PrintDebug` 会阻塞 CAN 帧处理、诱发栈/数据踩踏。即使修复以上三项，MKS 电机对 `F4 02` 完成响应的回报行为因固件版本而异且不可靠 **（后来发现是 CAN TX mailbox 机制问题，非固件版本差异，详见 §45）**。

**修复方案：** 彻底绕开 RTOS 事件标志和电机完成响应依赖。

**架构变更：**

| 组件 | 旧（事件标志） | 新（volatile 轮询） |
|------|--------------|-------------------|
| `CAN_Process_Task` | `osEventFlagsSet(evtAxesDone, ...)` | 写 `g_axes_done_bits`（位 0/1/2 对应 X1/X2/Y）和 `g_axes_error` |
| `move_xy_relative` | `osEventFlagsWait(..., 200ms)` 轮询 | `osDelay(估算时长)` + 堵转前检测 `g_axes_error` |
| `move_to` (static) | `osEventFlagsWait(..., ACK_TIMEOUT_MS)` | 10ms 轮询 `g_axes_done_bits` / `g_axes_error` |
| `MotionTask_Func` | 同上 | 同上 |
| `safe_move_to` | 超时 retry 逻辑 | 移除死代码（`move_xy_relative` 不再返回 -1） |

**时长估算公式：**
```c
max_steps = fmaxf(fabsf(dx), fabsf(dy));
move_ms   = max_steps / (speed × MKS_PULSES_PER_REV / 60000) + 80;
// 钳位：50ms ≤ move_ms ≤ 5000ms
```

其中 `MKS_PULSES_PER_REV = 16384.0f`（MKS SERVO42D 编码器每圈脉冲数），以命名常量定义于 `app_motion.c:49`。+80ms 为加减速安全余量。

**新增全局变量：**
```c
volatile uint32_t g_axes_done_bits = 0;  // bit0=X1, bit1=X2, bit2=Y (EVENT_X1_DONE/EVENT_Y_DONE 宏复用)
volatile bool     g_axes_error    = false; // F4/F5 0x03 堵转标志
```

`CAN_Process_Task` 单写，`move_xy_relative` / `move_to` 双读。单任务调用（Host_Task）场景下无竞态，无需互斥锁。

**涉及文件：** `Task/app_motion.c` — `CAN_Process_Task`（~15行）、`move_xy_relative`（~50行）、`move_to`（~15行）、`safe_move_to`（-7行）、`MotionTask_Func`（~5行）

### 27.6 修复 5 — 移除 UART 中断检测

`move_xy_relative` 等待循环中原有 `UART_PeekData(UART_CH1)` 检查，用于在运动期间检测上位机 `MOVE_STOP`。该检测过度敏感——串口残留数据（回显、换行）均触发误判 `ret=-3`。由于 `Host_Task` 在阻塞期间本就不接收新命令且无法同时处于两个状态，此检查无实际保护价值。

**修复：** 移除 UART 检查段，保持等待循环简洁。

### 27.7 完整修改清单

| 文件 | 行 | 修改 | 类别 |
|------|-----|------|------|
| `driver_motor.c` | 232 | `CAN_Transmit_Data(..., 8)` → `7` (positionMode1Run) | Bug 修复 |
| `driver_motor.c` | 255 | `CAN_Transmit_Data(..., 8)` → `7` (positionMode2Run) | Bug 修复 |
| `driver_motor.c` | 355-361 | 到位阈值 200→50，注释同步 | 参数优化 |
| `app_motion.c` | 49 | 新增 `MKS_PULSES_PER_REV 16384.0f` | 常量定义 |
| `app_motion.c` | 26-27 | 新增 `g_axes_done_bits` / `g_axes_error` | 新全局变量 |
| `app_motion.c` | 172-183 | `move_xy_relative` 文档注释更新 | 文档 |
| `app_motion.c` | 203-241 | `move_xy_relative` 重写：广播触发 + 时长估算 + volatile 检测 | 重构 |
| `app_motion.c` | 278-292 | `move_to` 同步改为 volatile 轮询 + 超时常量化 | 重构 |
| `app_motion.c` | 353 | `safe_move_to` 移除 -1 重试死代码 | 清理 |
| `app_motion.c` | 467-479 | `MotionTask_Func` 同步改为 volatile 轮询 | 重构 |
| `app_motion.c` | 554-567 | `CAN_Process_Task` 改为写 volatile 全局变量 | 重构 |

### 27.8 设计决策记录

- **不恢复事件标志：** RTOS 事件标志在 `PrintDebug` 密集场景下存在不可靠行为。`volatile uint32_t` + `volatile bool` 极简方案对单任务写入场景完全足够，零中间层、零堆分配。
- **不等待 F4 02 完成响应（已过时，参见 §45）：** 早期开发时认为 MKS 0x02 不可靠，采用时间估算。2026-07-27 发现 0x02 实际能被 0x31 编码器查询 flush 出来（CAN TX mailbox 机制），现已改为硬件到位确认 + 0x31 ping 轮询。
- **到位阈值 50 而非更低：** 5 步（~0.05mm）会导致伺服闭环振荡不稳定。50 步（~0.5mm）兼顾到位确认与稳定性。
- **移除 MOVE_STOP 检测而非保留：** 运动期间 `Host_Task` 处于阻塞态，上位机无法发送需要即时响应的命令。`MOVE_STOP` 由上位机通过 JOG 状态机自行管理（`MOVE_*_START` 的第二次点击）。
- **坐标映射正确性：** 电机 X1/X2 控制机器垂直轴（上下），电机 Y 控制水平轴（左右）。上位机坐标系约定 X=水平、Y=垂直，与电机坐标系差 90° 旋转 + 水平轴取反：`host_x = -motor_y`，`host_y = +motor_x`。`handle_debug_cmd` 离散移动表和 JOG 方向的电机映射已确认正确，`MOVE_TO` 的 `ty` 需取反（-cmd->param）以匹配此约定。标定 SET 命令存电机坐标、读取也用电机坐标，往返闭环正确，Display 层统一转上位机坐标显示。详见 §9.17。


## 二十八、2026-06-23 会话 — 下相机补光灯 + Flash 标定数据诊断与防御

### 28.1 12VO2 下相机补光灯

**硬件：** 12VO2 = PE12（DRV8803 U12 芯片 IN3）。DRV8803 逻辑 IN=HIGH → OUT=LOW，PE12 高电平时 12V 输出导通。

**新增接口**（`driver_drv8803.h`）：
```c
#define LIGHT_LOWERCAM_PORT  (&Port_12VO2)   // 12VO2/PE12 接下位相机补光灯

static inline void LowerCam_Light_On(void)  { DRV8803_SetOutput(&Port_12VO2, true); }
static inline void LowerCam_Light_Off(void) { DRV8803_SetOutput(&Port_12VO2, false); }
```

**使用**（`app_test.c` StartCamTestTask）：在 P3 `cam_test_run(VCMD_P3, ...)` 前开灯，结束后关灯。
err3_8 重试循环内灯保持开启，循环退出后关灯。当前 P3 块处于注释状态，补光灯调用以 `//` 注释保持一致。

### 28.2 Flash 标定数据的潜在危险

**问题：** `Calib_Load` 仅校验 magic + CRC32，不校验字段值的合理性。只要 magic 和 CRC 通过，
Flash 中的任何数据（包括 110° 的安全高度、0.0 的视觉系数）都会被原样加载。
`calib_set_defaults` 只有在 magic 不匹配或 CRC 失败时才会触发。

**根因链：** 旧 Flash 数据中 `z_safe=110.0` 的场景：
1. 某次标定时舵机在 110° 位置执行了 `SET_Z_SAFE` → `g_calib.z_safe_angle = 110.0`
2. `SET_Z_PICK` / `SET_Z_PLACE` 从未执行，对应字段为 0.0 或旧值
3. `SAVE_CALIB` → 整份数据（含 110.0 / 0.0 / 0.0 + 正确 CRC）写进 Flash
4. 之后每次 `Calib_Load` magic+CRC 通过 → 照单全收

**Host_Task 同样受影响：** `handle_debug_cmd` 的 JOG START 直接调用 `z_safe()`，
离散移动走 `safe_move_to` → `z_safe()`，都使用 `g_calib.z_safe_angle`（即 Flash 值）。
如果 Flash 里是 110°，Host_Task 点动时也会把舵机设到 110°，只是没有先设 78° 的对比，不易察觉。

### 28.3 标定结构体字段与默认值

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `scatter_x/y_steps` | int32_t | 0 | 散料区原点（步数） |
| `scatter_size_steps` | int32_t | 0 | 散料区边长（步数） |
| `heat_platform_x/y_min/max` | int32_t | 0 | 加热台平台区域（步数） |
| `bottom_cam_x/y_steps` | int32_t | 0 | 下相机位置（步数） |
| `cam_to_nozzle_dx/dy_steps` | int32_t | 0 | 摄像头→吸嘴偏置（步数） |
| `z_safe_angle` | float | 78.0f | 安全高度（°） |
| `z_pick_angle` | float | 116.0f | 吸取高度（°） |
| `z_place_angle` | float | 116.0f | 贴装高度（°） |
| `cam_p1_val_to_steps` | float | 3.277f | P1 上摄像头：视觉值→步数比例 |
| `cam_p3_val_to_steps` | float | 3.277f | P3 下摄像头：视觉值→步数比例 |

> **重要：** `cam_p1/p3_val_to_steps` 为 0 时视觉闭环矫正完全失效——摄像头检测到偏移后乘以 0 得 0 步，电机不补偿。

### 28.4 诊断日志

Host_Task 和 StartCamTestTask 均在 `Calib_Load` 后输出 4 行完整标定数据诊断日志：

```
[HOST] Calib: z_safe=77.9 z_pick=115.9 z_place=115.9
[HOST] Calib: scatter=(58880,-25600) size=37888
[HOST] Calib: heat=(25600,-82432)-(71680,-131072) botcam=(-103,-25650)
[HOST] Calib: nozzle_off=(-5991,-24988) cam_p1=-0.000 cam_p3=0.000
```

零值字段 = 未标定 = 垃圾数据，非零值 = 有效。


### 28.5 StartCamTestTask 的 Flash 数据风险

**注意：StartCamTestTask 在初始化时也调用了 Calib_Load(&g_calib)（app_test.c:1097），与 Host_Task 共享同一个全局标定变量。**

StartCamTestTask 有两处潜在破坏性操作：

1. **Calib_Load 调用（第 1097 行）：** 如果 Flash 读取偶发错误导致 magic/CRC 校验失败，calib_set_defaults 会将 g_calib 全部重置为默认值（位置全 0，Z 轴 78.0/116.0/116.0，cam 比例 3.277/3.277）。该函数仅返回 0（不返回 -1），不会触发 "Flash read failed" 日志，**数据丢失完全静默**。

2. **Z 轴高度覆写（第 1113-1115 行）：**
   `c
   g_calib.z_safe_angle  = 78.0f;
   g_calib.z_pick_angle  = 116.0f;
   g_calib.z_place_angle = 116.0f;
   `
   如果 Calib_Load 已静默失败并将 g_calib 重置为默认值（z_safe=78.0），这三行覆写不会改变任何值（78.0 → 78.0），**用户完全无法察觉标定数据已经丢失**。

**安全特性：** StartCamTestTask 不会调用 Calib_Save，因此其对 g_calib 的修改只存在于 RAM 中，Flash 中的标定数据不会被永久破坏。正常情况下 Host_Task 和 StartCamTestTask 不应同时激活。

### 28.6 Calib_Load 返回值语义

| 场景 | 返回值 | g_calib 内容 | 用户可见 |
|------|--------|-------------|---------|
| Flash 读成功 + magic OK + CRC OK | 0 | Flash 原始数据 | 正常 |
| Flash 读成功 + magic 不匹配 | 0 | calib_set_defaults 结果 | **无任何提示**（现已加诊断打印） |
| Flash 读成功 + CRC 不匹配 | 0 | calib_set_defaults 结果 | **无任何提示**（现已加诊断打印） |
| Flash 读失败（硬件错误） | -1 | calib_set_defaults 结果 | "Flash read failed, using defaults." |

> 返回值 0 不代表数据正确，仅代表操作完成。**magic 和 CRC 校验失败返回 0 而非 -1，是设计缺陷**——调用方（Host_Task）通过 Calib_Load(&g_calib) != 0 判断失败，这两种静默失败情况完全无法检测。

### 28.7 Calib_Load magic/CRC 失败诊断（新增，2026-06-23）

在 driver_spiflash_w25q64.c 的 Calib_Load 中新增诊断打印：

`c
// magic 不匹配时
if (calib->magic != CALIB_MAGIC) {
    PrintDebug("[CALIB] Magic mismatch: 0x%08lX\r\n", (unsigned long)calib->magic);
    calib_set_defaults(calib);
    return 0;
}

// CRC 不匹配时
if (expected != computed) {
    PrintDebug("[CALIB] CRC mismatch: exp=0x%08lX got=0x%08lX\r\n",
               (unsigned long)expected, (unsigned long)computed);
    calib_set_defaults(calib);
    return 0;
}
`

下次启动时如果标定数据静默丢失，日志会明确显示是 magic 不匹配还是 CRC 校验失败。

### 28.8 Calib_Save 写后读回验证（新增，2026-06-23）

在 driver_spiflash_w25q64.c 的 Calib_Save 中，W25Q64_Write 完成后立即读回比对：

`c
/* 诊断：写后读回比对 */
{
    CalibrationData_t verify;
    if (W25Q64_Read(CALIB_FLASH_ADDR, (uint8_t *)&verify, sizeof(verify)) >= 0) {
        if (memcmp(&buf, &verify, sizeof(buf)) != 0) {
            PrintDebug("[CALIB] WRITE VERIFY FAILED!\r\n");
            uint8_t *a = (uint8_t *)&buf, *b = (uint8_t *)&verify;
            for (int i = 0; i < (int)sizeof(buf); i++) {
                if (a[i] != b[i])
                    PrintDebug("  off=%d: wrote 0x%02X, read 0x%02X\r\n", i, a[i], b[i]);
            }
        } else {
            PrintDebug("[CALIB] Write verify OK.\r\n");
        }
    }
}
`

可区分两种根因：
- WRITE VERIFY FAILED → Flash 硬件写入异常（W25Q64 损坏、SPI 时序问题）
- Write verify OK + 下次启动 Magic/CRC 失败 → Flash 读取偶发错误

### 28.9 RESTORE_CALIB 命令（新增，2026-06-23）

新增 RESTORE_CALIB 上位机命令，用于紧急恢复标定数据。涉及文件：

| 文件 | 改动 |
|------|------|
| Task/app_uart_parser.h | 新增 HCMD_RESTORE_CALIB 枚举值 |
| Task/app_uart_parser.c | 新增 "RESTORE_CALIB" 字符串匹配 |
| Task/app_host.c | handle_calib_cmd 新增处理分支（硬编码标定值 → scatter_init_cells → Calib_Save） |

用法：上位机发送 RESTORE_CALIB（无参数，末尾带换行），固件将预置的标定数据写入 Flash。当前硬编码值：

`c
g_calib.scatter_x_steps       = 58880;
g_calib.scatter_y_steps       = -25600;
g_calib.scatter_size_steps    = 37888;
g_calib.heat_platform_x_min   = 25600;
g_calib.heat_platform_y_min   = -82432;
g_calib.heat_platform_x_max   = 71680;
g_calib.heat_platform_y_max   = -131072;
g_calib.bottom_cam_x_steps    = -103;
g_calib.bottom_cam_y_steps    = -25650;
g_calib.cam_to_nozzle_dx_steps = -5991;
g_calib.cam_to_nozzle_dy_steps = -24988;
g_calib.z_safe_angle           = 77.9f;
g_calib.z_pick_angle           = 115.9f;
g_calib.z_place_angle          = 115.9f;
g_calib.cam_p1_val_to_steps    = 0.0f;
g_calib.cam_p3_val_to_steps    = 0.0f;
`

### 28.10 SPI3 Flash 访问注意事项

- SPI3（W25Q64）无互斥锁保护。Host_Task 和 StartCamTestTask 均在初始化阶段调用 Calib_Load，两个任务同优先级（Normal），存在 SPI 总线竞争风险。正常使用时不应两个任务同时激活。
- app_logger.c（Log_Init / Log_Write）也访问 W25Q64，使用独立扇区 LOG_SECTOR_ADDR = 0x7FE000，不与标定扇区 CALIB_FLASH_ADDR = 0x7FF000 冲突。
- SPI3 中断已使能（stm32g4xx_it.c 中 HAL_SPI_IRQHandler(&hspi3) 已注册），但 W25Q64 驱动使用阻塞/轮询模式（HAL_SPI_Transmit/Receive），不使用中断/DMA。

### 28.11 涉及文件（汇总更新）

| 文件 | 改动 |
|------|------|
| driver_drv8803.h | 新增 LowerCam_Light_On/Off 内联函数 + LIGHT_LOWERCAM_PORT 宏 |
| Task/app_test.c | P3 补光灯调用；4 行完整标定诊断日志；Z 轴高度覆写 |
| Task/app_host.c | 4 行完整标定诊断日志 + RESTORE_CALIB 处理分支 |
| Task/app_uart_parser.h | 新增 HCMD_RESTORE_CALIB 枚举 |
| Task/app_uart_parser.c | 新增 "RESTORE_CALIB" 命令匹配 |
| `driver_spiflash_w25q64.c` | Calib_Save 写后读回验证；Calib_Load magic/CRC 失败诊断打印 |

### 28.12 文件编辑注意事项

- Node.js 的 
eadFileSync 读取 UTF-8 + CRLF 文件时保留 \r\n，替换时需匹配正确的换行符。
- 当替换文本中包含 % 字符时，PowerShell inline 
ode -e 会解析失败，应将 JS 写入临时 .js 文件再执行。
- git checkout 会丢弃未提交的工作区改动，执行前应确认无重要修改。git stash 可保护工作区改动。
- **项目文件编码均为 UTF-8 + CRLF (\r\n)**。用 [System.IO.File]::ReadAllText 读、[System.IO.File]::WriteAllText 写，确保字节精确。
## 二十九、2026-06-24 会话 — TMC2209 R 轴旋转修复

### 29.1 问题背景

StartCamTestTask 中 P3 视觉检测完成后，r_axis_rotate 被调用但 R 轴电机不旋转。
Host_Task 中同样的 SET_R_AXIS 命令能让电机正常旋转。两个任务调用的是同一个 r_axis_rotate 函数。

### 29.2 根因 1：RAMPMODE 方向锁（仅允许负向速度）

**文件：** driver_tmc2209.h

RAMPMODE_VELOCITY_HOLD 宏定义为 2，注释误导性地写"velocity hold"，实际含义是 **Velocity mode, negative only**（TMC2209 RAMPMODE 寄存器 bit0-1：0=positioning, 1=positive only, 2=negative only, 3=hold）。

TMC_Init() 写入 RAMPMODE=2 后，TMC2209 永远只接受负向 VACTUAL。角度为正时（相机检测到正偏角），TMC_SetSpeed 写入正 VACTUAL → TMC2209 忽略 → 电机不转。角度为负时反而能转。

**修复：** TMC_SetSpeed 中根据 velocity 符号动态切换 RAMPMODE：
```c
uint8_t rampmode = (velocity >= 0) ? 1 : 2;  // 正→1，负→2
TMC_WriteReg(TMC_REG_RAMPMODE, rampmode);
```

### 29.3 根因 2：TMC2209 要求写 RAMPMODE 前 VACTUAL=0

**文件：** driver_tmc2209.c — TMC_SetSpeed()

**根因：** TMC2209 数据手册明确要求 "Write RAMPMODE while VACTUAL=0"。原代码先写 RAMPMODE 再写 VACTUAL，TMC2209 内部状态机判定当前在运动状态而拒绝 RAMPMODE 写入 → RAMPMODE 卡在 0（positioning 模式）→ VACTUAL 在 positioning 模式下为只读 → 所有调速写入被静默忽略。

**诊断验证：** 在 TMC_SetSpeed 中加入 RAMPMODE 回读：
```
[TMC] RAMPMODE: wr=1 rd=0 MISMATCH   ← 写入 1 但寄存器仍为 0
```
证实 RAMPMODE 写入完全不生效。

**修复：** 改为正确的写入顺序：
```c
TMC_WriteReg(TMC_REG_VACTUAL, 0);       // 1. 先确保速度为 0
vTaskDelay(pdMS_TO_TICKS(2));
TMC_WriteReg(TMC_REG_RAMPMODE, rampmode); // 2. 再切换模式
vTaskDelay(pdMS_TO_TICKS(2));
TMC_WriteReg(TMC_REG_VACTUAL, vactual);   // 3. 最后设目标速度
```

### 29.4 根因 3：诊断回读代码的延迟扰乱了运动时序

**文件：** driver_tmc2209.c, app_motion.c

**根因：** 在排查过程中加入了大量回读验证代码（读 IOIN、GSTAT、RAMPMODE、VACTUAL），每次读操作包括 4 字节读请求 + 等待应答 + 逐字节回声处理，耗时约 15-30ms。在大角度路径中（usteps > 2560），ramp 阶段与 full-speed 阶段之间两次 TMC_SetSpeed 调用叠加了约 60ms 的诊断延迟，导致 ramp 阶段的 vTaskDelay(50ms) 等实际运动时间被严重压缩，电机来不及启动就被下一轮 VACTUAL=0 关闭。

**关键证据：** Host_Task 的 SET_R_AXIS 同样调用 r_axis_rotate，日志中同样显示 RAMPMODE MISMATCH（回读本身不可靠，见 §29.5），但电机能转。区别仅在于 StartCamTestTask 多了一大段诊断回读代码。

**修复：** 剥离所有回读验证，TMC_SetSpeed 精简为最小编写开销（~6ms/次）：
```c
void TMC_SetSpeed(int32_t velocity) {
    TMC_WriteReg(TMC_REG_VACTUAL, 0);
    vTaskDelay(pdMS_TO_TICKS(2));
    uint8_t rampmode = (velocity >= 0) ? 1 : 2;
    TMC_WriteReg(TMC_REG_RAMPMODE, rampmode);
    vTaskDelay(pdMS_TO_TICKS(2));
    if (velocity == 0) return;
    // ... 计算 vactual ...
    TMC_WriteReg(TMC_REG_VACTUAL, (uint32_t)vactual);
}
```

### 29.5 TMC2209 UART 回读不可靠问题

**现象：** 对 TMC2209 的寄存器回读经常收到 7 字节（丢失 CRC 字节）或 CRC 不匹配。规律是：上电后前 1-3 次读通信正常（8 字节 + 正确 CRC），之后退化到 7 字节 / CRC 错误。尽管回读数据不对，但 **寄存器写入本身是生效的**（证据：Host_Task 中电机能正常旋转，尽管日志同样显示 MISMATCH）。

**结论：** TMC2209 的 UART 回读不可作为寄存器写入是否成功的可靠判断依据。开发调试时可临时使用回读辅助排查，但生产代码中不应依赖回读验证。应通过实际物理效果（电机是否旋转）来验证。

**注意：** 此现象与 §16.2 问题 4/5 中记录的"7 字节应答格式 + 非标准 CRC"可能为同一根因——回读退化，而非 TMC2209 本身的格式差异。

### 29.6 r_axis_rotate 语义约定

**文件：** app_motion.c

r_axis_rotate(float angle, float speed_rpm) 的 angle 参数含义是 **绝对目标角度**（机器坐标系中的 R 轴位置），而非相对偏移：
```c
int r_axis_rotate(float angle, float speed_rpm) {
    float delta = angle - c0.r;   // 计算从当前 R 坐标到目标的差值
    // ... 旋转 delta ...
    Coord_UpdateR(angle);         // 更新 R 坐标为绝对目标值
}
```

**调用约定：** 在 Host_Task 和 StartCamTestTask 中，P1/P3 视觉矫正后调用 r_axis_rotate(offset_angle, R_SPEED_RPM) 时，offset_angle 是相机的相对偏角。首次调用时 c0.r == 0，delta == offset_angle，碰巧正确。但矫正后必须调用 r_axis_set_zero() 重置 R 坐标为 0，确保后续矫正不受累计偏移影响。

Host_Task 中的正确模式：
```c
host_correct_r_from_vision(r, "P1");  // 内部调用 r_axis_rotate
r_axis_set_zero();                     // 重置 R 坐标
```

StartCamTestTask 中已通过本次会话补上 r_axis_set_zero() 调用。

### 29.7 vacuum_ok 永远返回 true

**文件：** app_motion.c

```c
__weak bool vacuum_ok(void) {
    return true;   // 无硬件真空传感器，永远返回 true
}
```

pick_component() 依赖 vacuum_ok() 判断吸取是否成功，但因无实际传感器，该函数永远返回 true。这意味着即使吸嘴上没有元件，pick_component() 也会报告成功，P3 下相机检测到空吸嘴后返回 err3_8。在有真空传感器的硬件上应覆盖此弱函数。

### 29.8 涉及文件

| 文件 | 改动 |
|------|------|
| driver_tmc2209.c | TMC_SetSpeed 重写：VACTUAL=0 → RAMPMODE → VACTUAL=目标 的正确顺序；剥离回读诊断 |
| driver_tmc2209.h | RAMPMODE_VELOCITY_HOLD 宏已不再使用（TMC_SetSpeed 内部用字面量 1/2） |
| app_motion.c | r_axis_rotate 剥离 IOIN/GSTAT 诊断读；保留 delta 一行打印 |
| app_test.c | P3 矫正后新增 r_axis_set_zero()；P1 retry 矫正后新增 r_axis_set_zero() |


## 三十、2026-06-25~26 会话 — P2 对齐竞态修复 + 单位统一 + 轴映射修正

### 30.1 物理轴约定



| 电机 | CAN ID | 代码中命名 | 实际物理轴 | 正方向 |
|------|--------|-----------|-----------|--------|
| X1 | 0x01 | X 电机 | Y 轴（前后） | 上正下负 |
| X2 | 0x02 | X 电机 | Y 轴（前后） | 上正下负 |
| Y  | 0x03 | Y 电机 | X 轴（左右） | 左负右正 |

**关键映射规则（所有涉及设计坐标 → 电机坐标的地方必须遵守）：**

```
设计坐标 target_x  → 物理 X 轴（左右） → Y 电机   → machine_y / marks_actual[][1]
设计坐标 target_y  → 物理 Y 轴（上下） → X1+X2 电机 → machine_x / marks_actual[][0]
相机 X 偏移 (r->dx) → Y 电机   (物理 X)  【取反: dy = -(r->dx * scale)】
相机 Y 偏移 (r->dy) → X1+X2 电机 (物理 Y)  【不取反: dx = +(r->dy * scale)】
```

**方向验证（Mark 跳转实测确认）：**
- Mark 0(5,5)→Mark 1(20,5): tdx=+15mm → 相机 X 正向 → Y 电机负向(-7680步) → 机器右移 ✓
- Mark 0(5,5)→Mark 2(5,20): tdy=+15mm → 相机 Y 正向 → X1+X2 正向(+7680步) → 机器上移 ✓

**各公式中的符号汇总：**

| 环节 | dx/X1+X2 公式 | dy/Y电机 公式 |
|------|-------------|-------------|
| Mark 跳转 | `+(tdy * 512)` | `-(tdx * 512)` |
| P2 偏移修正 | `-(r->dy * scale)` | `-(r->dx * scale)` |
| P1 偏移修正 | `-(r->dy * scale)` | `-(r->dx * scale)` |
| P3 偏移修正 | `+(r->dy * scale)` 不取反 | `-(r->dx * scale)` |

> P3 的 dx 不取反是因为下相机图像左右镜像（相机朝上拍摄），X 轴自然反转。

**涉及位置（共 5 处，已全部修正）：**

1. Mark 跳转（cam_p2_full_test_run + mark_align_step）
2. 仿射建系 actual_ang 计算
3. 仿射建系 origin 计算
4. 仿射建系 Mark3 预测
5. move_to_pcb_step 贴装位置计算

### 30.2 P2 对齐死循环竞态修复

**文件：** Task/app_test.c

**问题：** cam_p2_full_test_run 和 cam_test_run 的 GOT_POS 分支调用 Vision_Go() 后有 vTaskDelay(50ms)，在此期间 ISR 可能已收到 Cam 的完整响应（pos+N:dx+N:dy+end），将 g_state 切回 VISION_GOT_POS。但循环末尾 prev = state 保存的是**进入 switch 时缓存的旧值**，导致下一轮 state == prev，switch 被跳过，任务死循环直至 120s 超时。

**修复：** prev = state → prev = VISION_BUSY（两处：cam_p2_full_test_run 和 cam_test_run）。因为 Vision_Go() 总是将状态转到 VISION_BUSY，用 VISION_BUSY 作为 prev 可确保 ISR 设回的任何新状态都能被下一轮 state != prev 检测到。

### 30.3 P2 输入单位从 mm×10000 改为 px

**涉及文件：** Task/app_test.c、Task/app_host.c

| 位置 | 改前 | 改后 |
|------|------|------|
| cam_test_run P2 分支 (app_test.c) | CAM_MM10000_TO_STEPS | CAM_PX_TO_STEPS |
| cam_p2_full_test_run GOT_POS (app_test.c) | CAM_MM10000_TO_STEPS | CAM_PX_TO_STEPS |
| mark_align_step GOT_POS (app_host.c) | /10000.0f | /1000.0f |
| g_mark_avg_dx/dy 贴装修正 (app_host.c) | /10000.0f | /1000.0f |
| 所有 P2 偏移日志 | mm10000 | px |

换算系数统一为 CAM_PX_TO_STEPS = STEPS_PER_MM / 1000.0f（与 P1 一致），**前提是 Cam 端同步将 P2 的 pos 数据从 mm×10000 改为像素输出**。

### 30.4 P2 运动参数统一为与 P1 一致

| 位置 | 改前 | 改后 |
|------|------|------|
| cam_p2_full_test_run GOT_POS | PNP_SPEED_FINE(100), PNP_ACC_FINE(10) | CAM_MOVE_SPEED(100), CAM_MOVE_ACC(25) |
| mark_align_step GOT_POS | PNP_SPEED_FINE(100), PNP_ACC_FINE(10) | 100, 25 |

### 30.5 涉及文件

| 文件 | 改动 |
|------|------|
| Task/app_test.c | 竞态修复（2处）、P2 单位 px 化、P2 速度统一、Mark 跳转 swap、仿射建系 swap、测试坐标调整 |
| Task/app_host.c | P2 单位 px 化、P2 速度统一、Mark 跳转 swap、仿射建系 swap、move_to_pcb_step swap |


## 三十一、2026-06-27 会话 — 坐标系修正 + 仿射建系修复 + Host_Task 对齐

### 31.1 Mark 跳转方向修正

**问题：** 识别 Mark0 后本应向右跳到 Mark1，实际向左移动。根因是跳转公式中 Y 电机的符号错误——跳转用 `+(tdx * scale)`，但偏移修正用 `-(r->dx * scale)`，两者方向矛盾。经实测确认偏移修正方向正确，跳转需与之统一。

**修复（app_test.c + app_host.c）：**
```c
// 跳转公式（修正后）
dx = +(tdy * STEPS_PER_MM);   // 相机 Y → X1+X2，不取反
dy = -(tdx * STEPS_PER_MM);   // 相机 X → Y 电机，取反
```

经历了两次迭代才定稿：第一次同时对 dx/dy 取反 → Y 轴反了；第二次只对 dy 取反 → 正确。

### 31.2 仿射建系修复

**问题：** 建系代码直接用电机坐标算 `atan2(a2x-a1x, a2y-a1y)`，但 `a2x-a1x` 是 X1+X2 差（相机 Y），`a2y-a1y` 是 Y 电机差（相机 X 取反）。参数位置错位导致实际轨迹（相机右移 15mm）被算成 ~178°。

**修复（app_test.c + app_host.c）：** 先将实际 Mark 电机坐标转换为相机坐标再计算角度：
```c
// 电机坐标 → 相机坐标
cam_X = -(Y_motor);    // 取反
cam_Y = X1+X2;         // 直通

// 用相机坐标计算实际角度
actual_ang = atan2f(cam_Y_diff, cam_X_diff);

// 建系完成后，origin 转回电机坐标存储
origin_x_steps = cam_origin_Y;      // → X1+X2
origin_y_steps = -cam_origin_X;     // → Y 电机（取反）
```

**验证结果（CamTest 实测）：** theta=0.04°, Mark3 验证误差=0.079mm。

### 31.3 move_to_pcb_step 旋转公式修正

**问题：** 贴装定位使用 `cy*cos-cx*sin` / `cy*sin+cx*cos` 而非标准 `(cx,cy)` 旋转，与建系 origin 的坐标系不一致。

**修复（app_host.c）：** 改为标准旋转变换：
```c
float rcx = cx * cos_t - cy * sin_t;   // 旋转后相机 X
float rcy = cx * sin_t + cy * cos_t;   // 旋转后相机 Y
machine_x = (int32_t)rcy + origin_x_steps;       // 相机 Y → X1+X2
machine_y = (int32_t)(-rcx) + origin_y_steps;    // 相机 X → Y 电机（取反）
```

### 31.4 P1/P3 偏移修正改用固定换算

**问题：** `g_calib.cam_p1_val_to_steps` 和 `cam_p3_val_to_steps` 未标定时值为 0，导致偏移修正量为 0，P1/P3 完全靠相机迭代硬扛，G4 不做任何补偿移动。

**背景：** 比例换算在摄像头端完成（P1 值×6.0, P3 值×1.5），G4 不需要区分上/下相机的像素比例差异，统一用一个换算系数即可。CamTest 已验证 `STEPS_PER_MM / 1000.0f = 0.512` 可正常工作。

**修复（app_host.c）：**
- `find_comp_step` P1 GOT_POS: `g_calib.cam_p1_val_to_steps` → `(STEPS_PER_MM / 1000.0f)`
- `offset_check_step` P3 GOT_POS: `g_calib.cam_p3_val_to_steps` → `(STEPS_PER_MM / 1000.0f)`

### 31.5 补加 Vision_Go 后延迟

**问题：** Host_Task 的 `mark_align_step` GOT_POS 和 `offset_check_step` GOT_ERR_RETRY 在 `Vision_Go()` 后缺少 50ms 延迟。CamTest 在调试中发现此延迟可避免 go 帧与相机响应碰撞。

**修复（app_host.c）：**
- `mark_align_step` GOT_POS: `Vision_Go()` 后加 `vTaskDelay(pdMS_TO_TICKS(50))`
- `offset_check_step` GOT_ERR_RETRY: 同上

### 31.6 电机调试日志关闭

将 noisy 的电机 CAN 帧日志用 `#ifdef` 包裹，默认关闭：

| 文件 | 宏 | 包裹的日志 |
|------|----|----------|
| `driver_motor.c` | `DEBUG_MOTOR` | `[MOTOR] syncTrigger` / `pos2run` ×4 |
| `app_motion.c` | `DEBUG_MOTION` | `[R] delta` / `Move done` ×2 |

`Emergency stop!` 和 `Move timeout!` 保留（仅异常时触发，有诊断价值）。

### 31.7 涉及文件

| 文件 | 改动 |
|------|------|
| Task/app_test.c | Mark 跳转 dx 去反（2次迭代）；仿射建系相机坐标转换；`DEBUG_MOTOR` 已存在 |
| Task/app_host.c | Mark 跳转 dx 去反；仿射建系相机坐标转换；move_to_pcb_step 标准旋转；P1/P3 固定换算；两处 vTaskDelay(50ms) |
| Drivers/ZeMCU-G4/driver_motor.c | `DEBUG_MOTOR` 宏包裹 4 处 PrintDebug |
| Task/app_motion.c | `DEBUG_MOTION` 宏包裹 2 处 PrintDebug |
---
## 三十二、TouchGFX 屏幕对接 — 主控端 bridge 层（2026-06-28）

> **【2026-08-01 校正】** 本节描述的 `app_touchgfx_bridge.c/h` 已随 GUI 独立板迁移删除，所有 `Bridge_*` 调用已由 `GUI_SPI_*` 替代（见 HISTORY.md §49）。本节仅作历史参考。
> **状态：已完成。** 两轮代码审查已通过，5 个问题已修复（1 P0 + 3 P1 + 1 P2）。
### 32.1 背景
主控端与 TouchGFX GUI 原先仅通过 `dataTransferQueue` 单向通知（主控→GUI），无 GUI→主控命令通道，且 `app_host.c` 中未调用任何 `DT_Notify*` 函数。
目标：建立双向解耦通信，主控端通过统一 `Data_Transfer` 接口与 GUI 通信，不直接耦合 View/Presenter/Widget。
### 32.2 TouchGFX 侧修改（4 个文件）
| 文件 | 改动 |
|------|------|
| `TouchGFX/gui/include/gui/model/Data_Transfer.h` | 枚举新增 `DT_WIFI_STATUS = 0x08`；声明 `DT_NotifyWifiStatus(uint8_t connected)` |
| `TouchGFX/gui/src/model/Data_Transfer.c` | (a) 实现 `DT_NotifyWifiStatus()`；(b) 7 个 handler 均改为 `static` 壳函数委托 `extern Bridge_*`（移除 TODO 占位、`#include "driver_motor.h"` 等旧依赖）；(c) `motorReset_Start()`/`motorReset_IsDone()`/`motor_reset_done` 保留未动（向后兼容） |
| `TouchGFX/gui/src/model/Model.cpp` | `processQueue()` switch 新增 `case DT_WIFI_STATUS` 分发到 `onNotifyWifiStatus()` |
| `TouchGFX/gui/include/gui/model/ModelListener.hpp` | 新增虚函数 `onNotifyWifiStatus(uint8_t connected)` |
### 32.3 主控侧新增文件
| 文件 | 说明 |
|------|------|
| `Task/app_touchgfx_bridge.h` | 对接层头文件：声明 `Bridge_Notify*`（System→GUI，9 个）和 `Bridge_*` handler（GUI→System，7 个），以及全局标志变量 |
| `Task/app_touchgfx_bridge.c` | 对接层实现：封装 `DT_Notify*` 调用 + 实现 7 个 GUI→System 命令处理器。内部含温度/状态/进度三重去重逻辑 |
### 32.4 主控侧修改
| 文件 | 改动 |
|------|------|
| `Core/Src/app_freertos.c` | 新增 `extern guiCmdQueue` 声明 + 创建 `guiCmdQueue = osMessageQueueNew(16, sizeof(DT_Msg_t), NULL)` |
| `Task/app_host.c` | 添加 `#include "app_touchgfx_bridge.h"`；初始化调用 `Bridge_Init()` + `Bridge_NotifyMotorSpeed()`；主循环每轮 `Bridge_ProcessHeaterStatus()` 温度更新；`HOST_DEBUG` 内处理 GUI 启动贴片/暂停/电机命令（消费 `motion_cmd_queue`）；各状态转换点调用进度/状态通知；`s_bridge_done_notified` 文件级静态变量控制 HOST_DONE 一次性通知 |
### 32.5 Bridge 通知（System→GUI）调用点
| 通知 | 触发位置 | 去重策略 | 说明 |
|------|----------|----------|------|
| `Bridge_NotifyMotorSpeed()` | `Host_Task` 初始化末尾 | 无（仅调一次） | 上报 PNP_SPEED_FAST |
| `Bridge_NotifyDownloadStatus(1)` | 进入 `HOST_DOWNLOADING` | 有 | 下载开始 |
| `Bridge_NotifyDownloadStatus(0)` | `download_done()` | 有 | 下载完成 |
| `Bridge_NotifySMTStatus(1)` | `download_done()` | 有 | 贴片开始 |
| `Bridge_NotifySMTStatus(0)` | `HOST_DONE` 首次（`s_bridge_done_notified` 控制） | 有 | 贴片完成 |
| `Bridge_NotifySMTProgress()` | 每元件放置后 + 全部完成时 | **有（第二轮新增）** | 进度更新，值未变则跳过 |
| `Bridge_ProcessHeaterStatus()` | 主循环每轮 | 有（`s_temp_valid`+值比较） | 温度去重更新 |
| `Bridge_NotifyLog(code,param)` | 关键事件（启停/错误/暂停） | 无（事件型） | code=1下载 2完成 3电机错误 4暂停 |
### 32.6 GUI→System 命令（7 个 handler）
| 命令 | Bridge 函数 | 实现方式 | 执行上下文 |
|------|-----------|----------|-----------|
| `DT_CMD_MOTOR_MOVE` | `Bridge_MotorMove()` | 坐标(mm×100)→步数，入 `motion_cmd_queue` | TouchGFX Task → `HOST_DEBUG` 消费 |
| `DT_CMD_MOTOR_STOP` | `Bridge_MotorStop()` | 入队 `MOTION_CMD_STOP` | 同上 |
| `DT_CMD_MOTOR_HOME` | `Bridge_MotorHome()` | 入队 `MOTION_CMD_HOME`（移至原点 0,0） | 同上 |
| `DT_CMD_SMT_START` | `Bridge_SMTStart()` | 置 `g_gui_smt_start_req=1`，`HOST_DEBUG` 检测后发送 `DOWNLOAD_READY` | TouchGFX Task 写标志 → Host_Task 消费 |
| `DT_CMD_SMT_PAUSE` | `Bridge_SMTPause()` | 置 `g_gui_smt_pause_req=1`，`HOST_FIND_COMP` 检测后回 `HOST_DEBUG` | TouchGFX Task 写标志 → Host_Task 消费 |
| `DT_CMD_HEATER_SET` | `Bridge_HeaterSet()` | 直接调用 `Heater_SetTemperature()` | TouchGFX Task（CAN 发送线程安全） |
| `DT_CMD_SYSTEM_RESET` | `Bridge_SystemReset()` | 调用 `NVIC_SystemReset()` | TouchGFX Task |
### 32.7 数据流
```
主控 (Host_Task)                    TouchGFX Task
    │                                     │
    ├─ Bridge_NotifyTemp() ──→ dataTransferQueue ──→ Model::processQueue() ──→ View
    ├─ Bridge_NotifySMTStatus() ──→     同上
    ├─ Bridge_NotifySMTProgress() ──→   同上
    │   ...                               │
    │                              guiCmdQueue ←── Model::sendCommand() ←── Presenter
    │                                     │
    └── DT_Dispatch() ←── Model::tick() ──┘
              │
              └── Bridge_MotorMove/Stop/Home/SMTStart/... 
                  (委托给主控端 bridge 实现，跨任务通过队列/标志同步)
```
### 32.8 两轮审查修复记录（2026-06-28）
**第一轮（结构完整性审查）：**
| 级别 | 问题 | 位置 | 修复 |
|------|------|------|------|
| P0 | `motorReset_Start()`/`motorReset_IsDone()`/`motor_reset_done` 被 handler 区段替换误删，导致链接错误 | `Data_Transfer.c` | 在 `smt_Start()` 前恢复完整实现 |
| P1 | handler 替换后残留旧注释头，形成双重标题 | `Data_Transfer.c` | 移除旧注释头，保留新 bridge 委托说明 |
| P1 | `Bridge_MotorMove` 中 `(void)steps_x; (void)steps_y;` 纯死代码 | `app_touchgfx_bridge.c` | 直接移除 |
| P1 | `Bridge_MotorMove` 硬编码 `300`/`25` 魔数无注释 | `app_touchgfx_bridge.c` | 注释标注对应 `PNP_SPEED`/`PNP_ACC` 语义 |
| P2 | `s_bridge_done_notified` 缩进 8 空格（周围为 4 空格） | `app_host.c` | 统一为 4 空格 |
**第二轮（逻辑鲁棒性审查）：**
| 级别 | 问题 | 位置 | 修复 |
|------|------|------|------|
| P1 | `s_last_temp = -1` 哨兵值与合法温度 -0.1°C（int16=-1）碰撞 | `app_touchgfx_bridge.c` | 改用独立 `s_temp_valid` bool 标志 + `s_last_temp = 0` |
| P1 | `Bridge_NotifySMTProgress` 无去重，高频下冲刷 16 深队列 | `app_touchgfx_bridge.c` | 增加 `s_last_progress_cur`/`s_last_progress_total` 去重 |
| P1 | `Bridge_Init()` 对已初始化的 static 变量重复赋相同值，语义模糊 | `app_touchgfx_bridge.c` | 增加注释「支持运行中重初始化（如看门狗恢复）」使之成为有意的防御设计 |
### 32.9 已知设计限制（非 Bug，文档记录）
| 限制 | 位置 | 影响 | 缓解 |
|------|------|------|------|
| `safe_move_to` 阻塞调用 | `HOST_DEBUG` motion 消费 | GUI 长距离移动时 Host_Task 主循环冻结（温度/GUI 刷新暂停） | 与原 UART 调试命令行为一致；GUI 移动为低频手动操作 |
| `Bridge_MotorMove` R 参数静默丢弃 | `app_touchgfx_bridge.c` | `MOTION_CMD_MOVE_TO` 不处理 R 轴，GUI 无法通过此路径控制旋转 | GUI 端尚未实现 R 轴控制 UI；未来需扩展为 `MOTION_CMD_R_ROTATE` |
| 暂停检测仅 `HOST_FIND_COMP` | `app_host.c` | PICK/PLACE 中间态不响应暂停 | 在元件边界暂停是有意设计，避免半途吸嘴悬空 |
| `Bridge_NotifyTemp` int16→uint16 强制转换 | `app_touchgfx_bridge.c` | 热电偶断开等负温异常场景下产生大数 | 加热台从机协议当前不支持负温上报，暂不影响 |
| `motion_cmd_queue` 满时静默丢弃（timeout=0） | `app_touchgfx_bridge.c` | GUI 快速连续点按可能丢失命令 | 队列 20 深 + 每 10ms 消费一轮，人工操作难触发 |
## 三十三、2026-06-29 会话 — G4↔G0 加热台 CAN 通信调试
### 33.1 初始问题
Host_Task 中测试加热台程序，`HEATER_QUERY`/`HEAT_ON`/`HEAT_OFF` 全部 TX 成功但无任何状态帧回报。从机串口也无 `CAN CMD:` 打印。
### 33.2 G4 侧修复
**问题 1：`heater_rx_queue` 创建时序晚于 CAN 中断使能**
`Host_Task` 初始化顺序原为 `CAN_Init()` → `Motor_Init()` → ... → `Heater_Init()`。
`CAN_Init()` 中 `HAL_FDCAN_Start()` 使总线激活，CAN ISR 即可触发。
但 `heater_rx_queue` 在 `Heater_Init()` 中才创建，中间有 500ms+ 窗口期。
CAN ISR 中路由逻辑：
```c
if (pkt.ID == HEATER_STATUS_ID && heater_rx_queue != NULL) {
    osMessageQueuePut(heater_rx_queue, &pkt, 0, 0);
}
```
队列为 NULL 时帧仅进入 `motor_event_queue`，`Heater_ProcessStatus()` 永远看不到。
**修复：** 将 `Heater_Init()` 移到 `CAN_Init()` 之前调用。（Task/app_host.c）
**问题 2：CAN 滤波器配置不准确**
`can_filter_mask_config()` 中 `FilterID2 = 0x1FFC0000` 超出 HAL 要求的 11-bit 范围（max 0x7FF）。
Release 编译下断言关闭，低 11 位截断为 0x000，掩码全 0 → 偶然实现全通。
同时 CubeMX 设 `StdFiltersNbr = 0`，滤波器槽位未分配，写入落在 Message RAM 偏移 0（RxFIFO0 区域）。
由于全局滤波器默认放行，通信未受影响。若重新生成 CubeMX 将 `StdFiltersNbr` 改为非零值，
此 bug 会导致滤波器仅通过 ID 低 8 位为 0 的帧（0x000/0x100/...），所有通信中断。
**修复：** 暂无（当前恰好工作）。如需修正：将 `FilterID2` 改为 `0x000`，确保 `StdFiltersNbr` ≥ 1。
### 33.3 G4 关键硬件参数（实测确认）
| 参数 | 文档旧值 | 实际值 | 来源 |
|------|---------|--------|------|
| HSE | 16 MHz | **25 MHz** | `Core/Inc/stm32g4xx_hal_conf.h:118` |
| SYSCLK | 170 MHz | 170 MHz | PLLM=5, PLLN=68, PLLR=2: 25/5×68/2=170 |
| PLLQ (FDCAN 时钟) | — | 170 MHz | PLLQ=2 |
| CAN 波特率 | 1 Mbps | **500 kbps** | 170MHz÷(17×20)=500k |
| FDCAN Prescaler | — | 17 | CubeMX |
| FDCAN TS1/TS2 | — | 15/4 | CubeMX |
### 33.4 涉及文件
| 文件 | 改动 |
|------|------|
| Task/app_host.c | `Heater_Init()` 移到 `CAN_Init()` 之前 |

---

## 三十四、2026-06-30~07-01 会话 — ESP32 通信测试任务创建

### 34.1 背景

工程中 ESP32 仅有生产任务 `ESP_Task`（SPI4 周期数据推送），无独立的 ESP 通信链路测试任务。
本次会话创建了 `StartESPTestTask`，涵盖硬件复位、SPI 收发、协议自检、WiFi 控制、状态查询、数据推送共 8 项测试。

### 34.2 ESP 测试任务（8 项）

| 编号 | 名称 | 测试内容 | 依赖硬件 |
|------|------|----------|----------|
| T1 | HardReset | GPIO 初始化 + 硬件复位时序（CS=HIGH, RST 100ms LOW→HIGH, 1s 等待） | 是 |
| T2 | SPI | 心跳包 SPI4 全双工收发 3 次，检测 MISO 是否为全 0xFF（浮空=从机无响应） | 是 |
| T3 | Protocol | 离线自检：数据包/控制包/查询包组包校验、解包模拟、FormatTemp/FormatProgress/StateToString 输出验证 | 否 |
| T4 | WiFi ON | 发送 0x20 0x01，轮询等待 ESP 回传 WIFI_STATUS（超时 10s） | 是 |
| T5 | WiFi OFF | 发送 0x20 0x02，清零连接标志，1.5s 后心跳确认 | 是 |
| T6 | Fault Query | 发送 0x30 0x01，等待 FAULT 或 COMPOUND 响应（超时不判失败） | 是 |
| T7 | WiFi Query | 发送 0x30 0x02，等待 WIFI_STATUS 响应 | 是 |
| T8 | Data Push | 依次发送 4 种子命令（进度/状态/加热台状态/温度），验证 SPI 收发无报错 | 是 |

### 34.3 任务属性与注册

**`espTestTask_attributes` 定义位置：** `Task/app_test.c`（不在 app_freertos.c 中），栈 1024 字节，优先级 Normal。

**启用方式：** 在 `app_freertos.c` 中添加：
```c
osThreadNew(StartESPTestTask, NULL, &espTestTask_attributes);
```
`espTestTask_attributes` 通过 `app_test.h` 的 `extern` 声明暴露，`app_freertos.c` 已包含该头文件。

**启用前必须禁用：** `ESP_Task`（生产任务，同样使用 SPI4），否则两个任务争抢 SPI4 总线。`StartCamTestTask` 使用 USART2+SPI3，与 SPI4 不冲突，无需禁用。

### 34.4 SPI 总线分配（纠正）

本次会话纠正了一个错误认知：`StartCamTestTask` 使用 **USART2**（摄像头）和 **SPI3**（W25Q64 Flash），不使用 SPI4。
ESP32 是 SPI4 的唯一使用者。

| 外设 | 总线 | 使用者 |
|------|------|--------|
| ESP32 通信模块 | **SPI4** (PE2/PE5/PE6, CS=PE3) | `ESP_Task`、`StartESPTestTask` |
| W25Q64 Flash | **SPI3** (PC10/PC11/PC12, CS=PA15) | `StartCamTestTask`（读取标定值） |
| MaixCam 摄像头 | **USART2** (PD5/PD6) | `StartCamTestTask`、`Host_Task`（视觉通信） |
| LCD (ST7306) | **SPI2** (PB13/PB15) | TouchGFX |

### 34.5 预期输出

硬件正常时预期 PASS=8 FAIL=0。若 ESP32 未焊接，T1 和 T3 仍可通过（纯 GPIO + 离线协议），PASS=2 FAIL=6。T2 是分水岭：通过说明 SPI 链路物理通，后续失败大概率是 ESP 固件侧问题。

### 34.6 涉及文件

| 文件 | 改动 |
|------|------|
| Task/app_test.h | ①修复 `#endif` 在 Cam 声明之前的 bug，移到末尾；②新增 `StartESPTestTask` 声明 + `espTestTask_attributes` extern |
| Task/app_test.c | ①新增 3 行 `#include`（app_esp_task/protocol/driver_esp32）；②追加 `espTestTask_attributes` 定义 + `StartESPTestTask` 完整实现（~270 行） |
| Core/Src/app_freertos.c | 未修改（用户自行添加任务创建行） |
| Task/app_esp_test.c/h | 曾创建后删除，代码合并到 app_test.c/h 中 |
---

> **校正（2026-08-06）：** §34 为历史测试任务记录。当前默认启用 `ESP_Task`，`StartESPTestTask` 默认停用；v3.1 无硬复位线，T1 HardReset 描述不再适用。


## 三十五、2026-06-30~07-01 会话 — R轴停稳、电机电流、Flash默认值、延时同步

### 35.1 R 轴开环定时停稳不足 — 吸取前加延时

**问题：** P1 识别成功后，`host_correct_r_from_vision` → `r_axis_rotate` 是开环定时控制（TMC2209 VACTUAL 模式）。停止后仅 `R_ACCEL_DELAY=50ms` 延时即认为停稳，TMC2209 内部加减速 + 机械惯量导致物理停稳滞后。气泵打开时 R 轴仍在微振，元件可能在吸嘴上偏移。

**修复（StartCamTestTask，app_test.c）：**
- P1 VISION_DONE 分支：`host_correct_r_from_vision` 后加 `osDelay(200)` 再 `r_axis_set_zero`（等待 R 轴物理停稳）
- 吸取前：`osDelay(300)` 改为 `osDelay(800)`（等待电机完全停止）

> 延时值为保守估计，实测后可调整。根本解决方案是在 `r_axis_rotate` 中改用到位反馈（查询 TMC2209 VACTUAL=0）而非开环定时。

### 35.2 MKS SERVO42D 工作电流未初始化

**问题：** `setIWorkMode()`（功能码 `0x83`）已在 driver_motor.c:338 定义，但 `Motor_Init()` 从未调用。电机保持默认 160mA，用户期望 1400mA（=1.4A）。

**修复（driver_motor.c）：** `Motor_Init()` 中使能电机后插入：
```c
// 2.5 设置工作电流 (mA)
setIWorkMode(0x01, 1400);  // X1
setIWorkMode(0x02, 1400);  // X2
setIWorkMode(0x03, 1400);  // Y
osDelay(20);
```

> `0x83` 写入 RAM，断电恢复默认；需永久保存则额外发 `0x40`（写 Flash）。

### 35.3 Flash 标定默认值补全 — 摄像头→吸嘴偏置

**问题：** `calib_set_defaults()` 仅设 Z 角度和 `cam_p1/p3_val_to_steps`，`cam_to_nozzle_dx/dy_steps` 由 `memset` 归零。首次上电无 Flash 数据时偏置补偿无效。

**修复：**

| 文件 | 改动 |
|------|------|
| `app_config.h` | 新增 `CALIB_DEFAULT_NOZZLE_DX_STEPS (-5529)` / `CALIB_DEFAULT_NOZZLE_DY_STEPS (-25041)` |
| `driver_spiflash_w25q64.c` | `calib_set_defaults` 中新增两条赋值 |


> `CALIB_DEFAULT_CAM_P1` / `CALIB_DEFAULT_CAM_P3` 定义在 app_config.h，但当前所有 P1/P3 偏移修正实际使用硬编码 `STEPS_PER_MM / 1000.0f`，标定字段未接入。

### 35.4 Host_Task 延时同步

将 CamTest 中的 R 轴停稳延时同步到 Host_Task 生产流程（app_host.c）：

| 位置 | 改动 |
|------|------|
| `find_comp_step` P1 VISION_DONE | `host_correct_r_from_vision` 后加 `osDelay(500)` → `r_axis_set_zero` |
| `offset_check_step` P3 VISION_DONE | 同上（顺手补全） |
| `pick_step` | `pick_component()` 前加 `osDelay(800)` |

### 35.5 涉及文件

| 文件 | 改动 |
|------|------|
| Task/app_motion.c | 无改动（R轴开环定时问题仅文档记录，根本修复待后续） |
| Drivers/ZeMCU-G4/driver_motor.c | `Motor_Init()` 中新增 `setIWorkMode` 调用（3 轴） |
| Task/app_config.h | 新增 `CALIB_DEFAULT_NOZZLE_DX/DY_STEPS` 宏 |
| Drivers/ZeMCU-G4/driver_spiflash_w25q64.c | `calib_set_defaults` 新增 nozzle offset 赋值 |
| Task/app_host.c | P1/P3 VISION_DONE 各加 `osDelay(500)`；`pick_step` 加 `osDelay(800)` |
| Task/app_test.c | P1 VISION_DONE 加 `osDelay(200)`；吸取前 `osDelay(300→800)` |

### 35.6 编辑注意事项补充

- **文件换行符不一致：** 项目规范声明 UTF-8 + CRLF，但 `driver_motor.c` 实测为 LF。编辑前须字节检测确认。
- **Base64+Node.js 路径：** 使用正斜杠（`E:/...`）传入 `node -e`，反斜杠会被转义导致 `ENOENT`。
- **实际编辑方法：** 使用单行 `node -e` 模式（而非 AGENTS.md 开头推荐的多行拼接），PowerShell 多行拼接中换行符处理不稳定。

## 三十六、2026-07-01 会话 — TMC2209 上电默认使能修复

### 36.1 问题

CubeMX 生成的 `MX_GPIO_Init()` 将 TMC1_EN（PD15）初始化为 `GPIO_PIN_RESET`（LOW）。
TMC2209 的 ENN 引脚低有效（LOW=驱动使能），因此上电后 TMC2209 即处于使能状态，
R 轴电机持续通电发热。

正常情况下 `Host_Task` 启动后会调用 `TMC_SetEnable(false)` 将 PD15 拉高关闭驱动
（[app_host.c:1337](E:/Desktop/qiansai/pnp_1/Task/app_host.c:1337)）。
但当 `Host_Task` 被注释掉、仅运行 `StartESPTestTask` 等测试任务时，
无任何代码关闭 TMC2209 驱动。

### 36.2 修复

在 `MX_FREERTOS_Init()` 的 `USER CODE BEGIN Init` 区域末尾添加 `TMC_SetEnable(false)`，
在所有任务创建之前将 PD15 拉高：

```c
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */
	UART_Driver_Init();
	Event_Init();
	Key_Init();
	TMC_SetEnable(false);  /* ENN=HIGH, 关闭TMC2209驱动 */
  /* USER CODE END Init */
```

该文件已包含 `#include "driver_tmc2209.h"`，无需额外引入。

### 36.3 影响分析

所有用到 TMC2209 的代码均自行管理 EN 状态，提前关闭无影响：

| 使用者 | 行为 | 影响 |
|--------|------|------|
| `Host_Task` | 启动时调用 `TMC_SetEnable(false)` | 无（已关闭，重复关闭无操作） |
| `StartCamTestTask` | `TMC_Init()` 流程：关→开(写寄存器)→关 | 无（入口即 `TMC_SetEnable(false)`） |
| `r_axis_rotate` | 旋转前 `TMC_SetEnable(true)` → 旋转后 `TMC_SetEnable(false)` | 无（自行管理） |

### 36.4 涉及文件

| 文件 | 改动 |
|------|------|
| Core/Src/app_freertos.c | `MX_FREERTOS_Init()` 中 `Key_Init()` 后新增 `TMC_SetEnable(false)` |
---

## 三十七、2026-07-01 会话 — 文档拆分与同步

### 37.1 背景

原 AGENTS.md 已膨胀至 207KB / 36 章节，混合了当前状态参考和历史会话日志，每次加载消耗大量上下文。

### 37.2 拆分

- **AGENTS.md**（~64KB）：§1~§10 核心参考——协议、任务架构、数据结构、已知问题、快速参考。每次会话自动加载。
- **HISTORY.md**（~143KB）：§11~§36 全部历史会话记录。按需查阅。

### 37.3 同步修正（23 处）

- 7 处交叉引用断裂修复（AGENTS.md 中指向已迁移章节的 §XX.Y 更新为自包含描述或 HISTORY.md §XX.Y）
- 补回 9 项因拆分丢失的关键信息：物理轴约定、RESTORE_CALIB 命令、StartESPTestTask、初始化顺序约束、CAN 滤波器隐患、工作电流、速度常量分级、AUTO_HEAT、SET_ORIGIN 状态守卫
- §6 数据流图重写（从 4 步简化为完整 14 状态流程）
- §3 目录结构更新（补 9 个缺失文件）
- §4.2 P1 补 footprint→class_id 映射和错误分级重试
- §4.2.1 物理轴约定补核心矛盾警告框和变量语义注解
- §4.3 补 0xFD 功能码

### 37.4 使用约定

- AGENTS.md = 活地图（当前状态），HISTORY.md = 档案室（历史记录）
- 每次代码改动后同步更新两份文档：当前状态变化 → AGENTS.md，完整记录 → HISTORY.md
---

## 三十八、2026-07-11 会话 — CAM 协议 v2→v3 迁移：单次检测 + 增益移除

### 38.1 背景

MaixCAM 视觉固件升级（基于 `vision_test_5/main.py` 2026-07-11 版本），协议从 v2（迭代 Phase2）迁移到 v3（单次检测 + CAM 侧预乘增益）。
G4 固件需同步修改以匹配新协议。

### 38.2 CAM 侧协议变更摘要

| 项目 | 旧协议 (v2) | 新协议 (v3) |
|------|-----------|-----------|
| P1 对齐模式 | Phase0→Phase1→Phase2 迭代→ok+角度 | Phase0→Phase1 单次检测，pos 含全部数据 |
| P3 对齐模式 | Phase0→Phase1→Phase2 迭代→ok+角度 | Phase0→Phase1 单次检测，pos 含全部数据 |
| P1 pos 字段 | 3 字段（dx, dy, class_id），Phase2 补角度 | 4 字段（dx, dy, angle, class_id） |
| P3 pos 字段 | 2 字段（dx, dy），Phase2 补角度 | 3 字段（dx, dy, angle） |
| P1 增益 | CAM 输出 px×6.0，G4 再乘 0.512 | CAM 输出已乘 47.077 = **电机步数** |
| P2 增益 | CAM 输出像素，G4 乘 0.512 | CAM 输出已乘 48.5 = **电机步数** |
| P3 增益 | CAM 输出 px×1.5，G4 再乘 0.512 | CAM 输出已乘 13.5554 = **电机步数** |
| P1 角度符号 | `angle * 100` | `-angle * 100`（取反） |
| P3 角度符号 | `angle * 100` | `angle * 100`（不变） |
| P3 错误码 | err3_1/3_2/3_3/3_5/3_6/3_7/3_8 | err3_1/3_5/3_8（err3_3 等移除） |

详细 CAM 协议文档：`E:/聊天记录/通讯接口文档 (1).md`

### 38.3 G4 固件修改

#### app_vision.c — 视觉协议状态机

**P1/P3 状态枚举精简：**
- P1：删除 `P1_S2_ITERATING`、`P1_S_WAIT_ANGLE`，只保留 `P1_S_CATEGORY` / `P1_S0_WAIT_STOP` / `P1_S1_WAIT_POS`
- P3：删除 `P3_PHASE2`、`P3_S_WAIT_ANGLE`，只保留 `P3_PHASE1`

**P1 帧处理 (`process_p1_frame`)：**
- 字段数 3→4：新增 `g_tmp_ao` 暂存角度，字段分配为 case 0=dx, 1=dy, 2=angle, 3=class_id
- `end` 帧处理：直接设 `VISION_DONE` + `angle_valid = true`（不再设 `VISION_GOT_POS`）
- 删除 `ok` / `P1_S_WAIT_ANGLE` 的帧分发分支

**P3 帧处理 (`process_p3_frame`)：**
- 字段数 2→3：新增 angle 字段
- `end` 帧处理：直接设 `VISION_DONE` + `angle_valid = true`
- 删除 `ok` / `P3_S_WAIT_ANGLE` 的帧分发分支
- 删除 `err3_3` 可重试错误处理（新 CAM 协议不再发此码）
- 删除 P3 Phase1 auto-complete 模式（改为依赖 `end` 帧终止）

**`Vision_Go()` 简化：**
- P1：只处理 `P1_S0_WAIT_STOP → P1_S1_WAIT_POS` 转换，Phase2 迭代分支全部删除
- P3：改为空操作（P3 走 VISION_DONE 直接完成，不再经 GOT_STOP/GOT_POS）
- P1/P3 分支均增加 `return` 防止穿透到 `g_state = VISION_BUSY`
- 删除 `VISION_GOT_ERR_RETRY` 的 err3_3 重试分支（死代码）

#### app_host.c — PnP 流程层

**增益缩放全部移除：**
- P2 `mark_align_step`：`* CAM_PX_TO_STEPS` → 直接使用 r->dx/r->dy
- P1 `find_comp_step`：`* (STEPS_PER_MM / 1000.0f)` → 直接使用
- P3 `offset_check_step`：`* (STEPS_PER_MM / 1000.0f)` → 直接使用
- 删除 `#define CAM_PX_TO_STEPS` 宏（已无引用）

**P1/P3 的 VISION_DONE 处理合并：**
- P1：原 `VISION_GOT_POS`（偏移移动） + 原 `VISION_DONE`（cam-to-nozzle 补偿）→ 合并为一个 `VISION_DONE` case
- P3：原 `VISION_GOT_POS`（偏移移动+累积） + 原 `VISION_DONE`（角度矫正+PCB 过渡）→ 合并为一个 `VISION_DONE` case
- 删除 `offset_check_step` 中 `VISION_GOT_ERR_RETRY` case（死代码）

**轴映射保持不变：**
| 环节 | dx/X1+X2 | dy/Y电机 | 说明 |
|------|---------|---------|------|
| P1 | `-(r->dy)` | `-(r->dx)` | 两轴均取反 |
| P2 | `-(r->dy)` | `-(r->dx)` | 两轴均取反 |
| P3 | `+(r->dy)` | `-(r->dx)` | r->dy 不取反（下相机镜像） |

### 38.4 代码审查发现与修复

| 严重度 | 问题 | 位置 | 修复 |
|--------|------|------|------|
| 🔴 致命 | `Vision_Go` P3 else-if 块内嵌 CRLF 导致多行挤成一行 + 重复 `g_state = VISION_BUSY;` | `app_vision.c:631` | 完整重写 Vision_Go 函数 |
| 🟡 中等 | `Vision_Go` P1 非 P1_S0_WAIT_STOP 子状态穿透到 g_state = VISION_BUSY | `app_vision.c:609` | 添加 `return` |
| 🟢 低 | `err3_3` 处理为死代码（新 CAM 协议不再发送） | `app_vision.c` + `app_host.c` | 删除所有 err3_3 相关分支 |

### 38.5 涉及文件

| 文件 | 改动 |
|------|------|
| `Task/app_vision.c` | P1/P3 状态枚举精简；process_p1/p3_frame 单次检测+字段数调整+直接 VISION_DONE；Vision_Go 简化；新增 g_tmp_ao；删除 err3_3 死代码 |
| `Task/app_host.c` | 三处增益缩放移除；P1/P3 VISION_DONE 合并；删除 CAM_PX_TO_STEPS 宏 |
| `AGENTS.md` | §4.2 P1/P2/P3 协议描述重写；§4.2.1 轴映射表去 scale |
| `HISTORY.md` | 新增 §38 |


---

## 三十九、2026-07-11~12 — R 轴 KTH7823 闭环控制实现

### 39.1 背景

原先 R 轴使用 TMC2209 VACTUAL 速度模式开环控制（定时盲估到位，R_ACCEL_DELAY=50ms），无位置反馈。
吸取前需额外 osDelay(800) 等待物理停稳 (§35.1)。本次会话实现了 KTH7823 14-bit 磁编码器的
PWM 输入捕获驱动 + 闭环 PID 位置控制，从根本上解决开环丢步和停稳不确定问题。

### 39.2 KTH7823 规格参数

| 参数 | 值 | 来源 |
|------|-----|------|
| 输出模式 | PWM 绝对位置 | 产品手册 |
| PWM 频率 | 910 Hz (~1099μs 周期) | 同上 |
| 分辨率 | 14-bit (16384 counts/rev) | 同上 |
| 0° 脉宽 | 32/16448 占空比 (~2.14μs) | 同上 |
| 360° 脉宽 | 16416/16448 占空比 (~1097μs) | 同上 |
| 角度公式 | Ang = (360/16384) * [(16448 * tON) / period - 32] | 同上 |

### 39.3 硬件接口

KTH7823 PWM 输出 → PB2 (TIM5_CH1)。PB2 原先在 CubeMX 中配置为 24V_C1 PWM，
同时作为 DRV8803 Port_24VO4 的 PWM 引脚。经确认：MX_TIM5_Init() 实际仅配置 CH3 (PE8 舵机)，
CH1 虽有 AF 配置但从未使能 PWM 通道；DRV8803 驱动仅用 pins[0] (PC5 开关)，PWM 引脚引用无实际操作。
**PB2 可安全改为输入捕获。**

### 39.4 TIM5 共用：CH1 输入捕获 + CH3 舵机 PWM

TIM5 同时服务于 CH3(PE8) 舵机 PWM 和 CH1(PB2) 编码器捕获。
原 Prescaler=169 (1MHz) 对舵机完美，但对编码器 0° 脉宽仅 2 ticks，无法分辨 14-bit 数据。

**解决方案：TIM5 PSC 从 169 降至 0 (170MHz)。**

| 对比 | PSC=169 (旧) | PSC=0 (新) |
|------|-------------|-----------|
| 定时器时钟 | 1 MHz | 170 MHz |
| 舵机 ARR (50Hz) | 19,999 | 3,399,999 |
| 舵机 500μs/2500μs CCR | 500 / 2,500 | 85,000 / 425,000 |
| 编码器周期 (910Hz) | ~1,099 ticks | ~186,813 ticks |
| 编码器 0° 脉宽 | 2 ticks ❌ | ~364 ticks ✓ |
| 编码器分辨率 | 0.07 ticks/step ❌ | ~11.4 ticks/step ✓ |

TIM5 为 32-bit 定时器，ARR=3,399,999 和 CCR=425,000 均不溢出。
舵机驱动 driver_servo.c/h 的 pulse 字段已是 uint32_t，无需修改。

**CubeMX 修改：** pnp_1.ioc 中 TIM5 Prescaler=0, Period=3399999, CH1=Input Capture (Both Edges),
CH3=PWM Generation, TIM5 IRQ 使能。重新生成后 Core/Src/tim.c 和 Core/Src/stm32g4xx_it.c 自动更新。

### 39.5 软件架构

`
KTH7823 PWM (910Hz, 14-bit)
    → PB2 (TIM5_CH1) 双边沿捕获 @170MHz
    → ISR: 读 CCR1 + 引脚电平判边沿 → 计算 tON + period
    → 角度公式解算 → g_kth.angle_deg + data_ready
    → r_axis_rotate() 每 5ms 读角度
    → PID (P=2.5, I=0.05, D=0.3, 位置式) 输出速度
    → TMC_SetSpeed() → TMC2209 VACTUAL
    → |error| < 0.2° 到位 → 停机 → 关断
`

**新增文件：**

| 文件 | 说明 |
|------|------|
| Drivers/ZeMCU-G4/driver_kth7823.h | 编码器常量 (KTH7823_UNIT_*) + API 声明 |
| Drivers/ZeMCU-G4/driver_kth7823.c | 输入捕获 ISR + 角度解算 + HAL_TIM_IC_CaptureCallback 重写 + KTH7823_WaitData |

**修改文件：**

| 文件 | 改动 |
|------|------|
| Core/Src/tim.c | CubeMX 重新生成: TIM5 PSC=0, Period=3,399,999, CH1 IC + CH3 PWM |
| Core/Src/stm32g4xx_it.c | 新增 TIM5_IRQHandler → HAL_TIM_IRQHandler(&htim5) |
| Drivers/ZeMCU-G4/driver_drv8803.c | Port_24VO4: num_pins=2→1, 移除 PB2 PWM 引脚引用 |
| Task/app_config.h | 新增 R_CLOSED_KP/KI/KD/MAX_SPEED/THRESHOLD/TIMEOUT/KICK_MS/LOOP_MS |
| Task/app_motion.c | 
_axis_rotate() 改为闭环 PID (static 复用); 
_axis_set_zero() 加 KTH7823_WaitData; 新增 #include "driver_kth7823.h" + "pid.h"; g_r_encoder_zero_offset |
| Task/app_host.c | 新增 #include "driver_kth7823.h"; KTH7823_Init() 调用 + 错误检查; host_correct_r_from_vision 中 R_CORRECTION_THRESHOLD_DEG → R_CLOSED_THRESHOLD |

**闭环 PID 参数（app_config.h）：**

| 常量 | 值 | 说明 |
|------|-----|------|
| R_CLOSED_KP | 2.5f | 比例系数 (速度/度误差) |
| R_CLOSED_KI | 0.05f | 积分系数 |
| R_CLOSED_KD | 0.3f | 微分系数 |
| R_CLOSED_MAX_SPEED | 50000.0f | PID 最大速度输出 (μsteps/s) |
| R_CLOSED_THRESHOLD | 0.2f | 到位判定阈值 (度) |
| R_CLOSED_TIMEOUT | 3000 | 超时保护 (ms) |
| R_CLOSED_KICK_MS | 10 | 初始开环起步时长 (ms) |
| R_CLOSED_LOOP_MS | 5 | PID 控制周期 (ms) |

### 39.6 三轮代码审计修复记录

**第一轮（初始实现 → 用户自审）：**
- CubeMX 硬件配置完成：TIM5 PSC=0, IC 配置, IRQ 使能
- Port_24VO4 释放 PB2
- 缺失：驱动层 driver_kth7823.c/h、闭环 
_axis_rotate()、PID 参数

**第二轮（实现 + 首轮审计 → 8 项修复）：**

| # | 严重度 | 问题 | 修复 |
|---|--------|------|------|
| 1 | P1 | memset((void*)&g_kth...) 强转丢弃 volatile | 移除 memset，依赖 static 编译期零初始化 + 显式复位关键字段 |
| 2 | P1 | GetAngle() 先清 data_ready 再读 ngle_deg | 先快照值再清标志 |
| 3 | P1 | ICFilter=0 无硬件毛刺抑制 | KTH7823_Init 中写 CCMR1 设 IC1F=8 (~47ns) |
| 4 | P1 | 
_axis_set_zero 无编码器数据等待 | 新增 KTH7823_WaitData(timeout_ms) |
| 5 | P2 | 硬编码 GPIO_PIN_2 / GPIOB | 新增 KTH7823_PWM_PORT / KTH7823_PWM_PIN 宏 |
| 6 | P2 | PID 首拍 D-term 尖峰 (MeasurementPrev=0) | 首次读数调用 PID_Compute 初始化历史 (丢弃输出) |
| 7 | P2 | host_correct_r_from_vision 阈值不一致 | 统一为 R_CLOSED_THRESHOLD |
| 8 | P2 | ngle_to_usteps 残留 doxygen 注释 | 清理 |

**第三轮（深度审计 → 4 项修复）：**

| # | 严重度 | 问题 | 修复 |
|---|--------|------|------|
| 1 | P0 | PID_Init 每次调用 osMutexNew → FreeRTOS 堆泄漏 | 改为 static PID_Controller_t r_pid，仅首次 PID_Init，后续 PID_Reset + PID_SetParams 复用 |
| 2 | P2 | R_CLOSED_KICK_MS / R_CLOSED_LOOP_MS 定义在 app_motion.c | 移到 pp_config.h 集中管理 |
| 3 | P3 | KTH7823_Init() 返回 void，失败无感知 | 改为返回 ool，pp_host.c 检查并打印 FAILED |
| 4 | P1 | 
_axis_set_zero 忽略 KTH7823_WaitData 返回值 | 检查返回值，超时时 offset 保持 0 + 注释说明 |

**第四轮（最终审计 → 2 项）：**

| # | 严重度 | 问题 | 修复 |
|---|--------|------|------|
| 1 | Bug | 
_axis_set_zero 仍忽略 WaitData 返回值 (第三轮修复未正确处理) | 重新修复：if (KTH7823_WaitData(100)) { ... } |
| 2 | Style | 多余空行 | 清理 |

### 39.7 关键技术决策

1. **static PID 复用：** 
_axis_rotate 内使用 static PID_Controller_t r_pid，首次调用 PID_Init 创建 mutex，后续调用 PID_Reset + 参数更新复用同一实例。避免每次旋转分配/泄漏 FreeRTOS mutex 对象。

2. **角度解绕 (unwrap)：** PID 工作在连续空间。编码器角度 (0~360°) 被"解绕"到 setpoint ±180° 范围后再送入 PID，避免 359°→1° 跨越被误认为 358° 误差。

3. **首拍尖峰消除：** PID 首次调用前用首个编码器读数喂入 PID_Compute 初始化 MeasurementPrev，消除微分项从 0 跳变到实际值产生的尖峰。该次输出丢弃。

4. **ICFilter=8：** 在 KTH7823_Init 中直写 TIM5->CCMR1 覆写 CubeMX 默认值 0。8 级滤波 ~47ns @170MHz，远小于编码器最短脉宽 ~2μs，不影响测量但有效抑制电磁干扰毛刺。

5. **GetAngle 读取顺序：** loat val = angle_deg; data_ready = false; return val; — 先快照再清标志，防止 ISR 在清标志与读值之间更新数据导致本轮返回旧值且丢失新数据标志。

### 39.8 已知限制

- PID 参数为理论值，需实机整定（通过阶跃响应调整 KP/KI/KD）
- 
_axis_rotate 返回值 (-1=跳过, -2=超时) 在 Host_Task 调用点未检查，与原开环行为一致
- R_CORRECTION_THRESHOLD_DEG (0.1°) 在 pp_config.h 仍定义但已无引用，被 R_CLOSED_THRESHOLD (0.2°) 替代
- 编码器断线时 
_axis_set_zero offset=0，后续角度即编码器原始值（非静默错误，行为可预测）


---

## 四十、2026-07-14 会话 — P2 连续速度扫描改造

### 40.1 背景

原先 P2 Mark 搜索采用离散网格扫描：每个格子 `safe_move_to` 停稳 → Cam 检测 → 超时后跳到下一格。此方案效率低（频繁启停）、Cam 识别窗口受限。

改为 X1+X2 同步速度模式 (0xF6) 蛇形连续扫描：Cam 在移动中实时检测 Mark，搜到立即停电机并记录位置。

### 40.2 设计约束

- ~~CAN ID 0x02 (X2 电机) 的到位响应不可靠~~ **（已过时，见 §45）**：MKS 0x02 实际可稳定接收，需 0x31 ping 周期性 flush CAN TX mailbox（详见 §45）。当前已改为 0x02 硬件到位 + 时间估算兜底。
- 扫描速度 40 RPM（Cam 需要低速才能准确识别），加速度 15
- X1+X2 必须一同移动（龙门双驱）
- 蛇形扫描方向与离散网格完全一致：偶列下→上，奇列上→下，列间 Y 电机右移 5mm
- 仅修改 P2 连续扫描部分，其余所有流程（P1/P3/贴装/回流焊/调试命令）不变

### 40.3 新增函数 (app_motion.c)

| 函数 | 说明 |
|------|------|
| `p2_scan_start(dir, speed, acc)` | X1+X2 同步 speedModeRun (0xF6) + motorSyncTrigger |
| `p2_scan_stop()` | X1+X2 axis_stop (0xF7) + motorSyncTrigger + osDelay(5) |
| `p2_scan_estimate_x(start_x, sign, speed, elapsed_ms)` | 时间 → 位移 → 当前 X 坐标 |
| `p2_scan_step_y(dy_steps, speed, acc)` | Y 电机 positionMode2Run 侧移 (0x02 硬件到位 + 编码器验证) |

### 40.4 连续扫描状态机 (mark_align_step)

**VISION_BUSY 分支重写：**
```
进入 g_p2_scanning:
  ├─ 列初始化: p2_scan_start(dir, 40, 15) → 记录起点坐标+时刻
  ├─ 每 ~100ms: p2_scan_estimate_x → Coord_UpdateXY (位置跟踪)
  └─ 列超时检查:
      ├─ elapsed < col_time → 继续扫描
      └─ elapsed >= col_time:
          ├─ p2_scan_stop() → 最终位置估算
          ├─ g_p2_col++ → 检查是否耗尽
          ├─ p2_scan_step_y(5mm) → 侧移到下列
          └─ g_in_busy=false → 下一 tick 重新初始化新列

进入非扫描 (对齐/跳转):
  └─ 原 P2_SCAN_TIMEOUT 超时逻辑不变
```

**VISION_GOT_STOP 修改：**
```
if (g_p2_scanning):
  p2_scan_stop() → g_p2_scanning=false
  时间估算停止位置 → Coord_UpdateXY
  PrintDebug 输出 est_x 和 elapsed
后续跳转逻辑不变 (Mark→Mark 偏移跳转)
```

**VISION_ERROR 修改：**
```
if (g_p2_scanning):
  p2_scan_stop() → g_p2_col++ → 侧移到下列
  Vision_BackToSearch() → 继续扫描
else:
  原错误处理不变
```

**VISION_DONE：** 新增 `g_p2_scanning = false`。建系算法不变。

### 40.5 位置精度分析

时间估算公式：`x = start_x + sign * speed_rpm * 16384 / 60000 * elapsed_ms`

| 因素 | 值 |
|------|-----|
| 更新间隔 | 100 ms |
| 最坏位置误差 | ~2.1 mm (1092 步) |
| P2 Cam FOV | ~9.2 mm (448px / 48.5 steps/px) |
| 对齐捕获范围 | 远大于误差 → P2 对齐迭代自动修正 |

### 40.6 涉及文件

| 文件 | 改动 |
|------|------|
| `Task/app_motion.h` | 新增 4 个 P2 扫描函数声明 (lines 103-107) |
| `Task/app_motion.c` | 新增 p2_scan_start/stop/estimate_x/step_y 实现 (~100 行) |
| `Task/app_host.c` | #define P2_SCAN_SPEED/ACC/DIR_UP/DOWN/DIR_DOWN/COL_PAD_MS/POS_UPDATE_MS |
| | 新增静态变量 g_p2_scanning/col/col_start_x/col_start_tick/col_time_ticks/last_pos_update |
| | download_done(): P2 初始化新增 g_p2_scanning/col/last_pos_update 赋值 |
| | mark_align_step(): VISION_BUSY 完全重写 (~100行) + VISION_GOT_STOP 增停止逻辑 + VISION_ERROR 增列恢复 |
| | VISION_DONE: 增 g_p2_scanning = false |
| | uncalibrated else: 增 g_p2_scanning = false |

### 40.7 待上机验证

- **方向映射：** `P2_SCAN_DIR_UP=0` (CCW), `P2_SCAN_DIR_DOWN=1` (CW)。方向反了就交换这两个值重新编译。
- 扫描速度 40 RPM 是否适配 Cam 当前识别帧率

---


---

## 四十一、2026-07-15 会话 — R 轴 KTH7823 编码器 → 时间积分开环

### 41.1 背景

KTH7823 磁编码器的偏心问题（磁铁安装偏心的正弦误差）一直未解决。
本次会话讨论并实施了从编码器闭环到时间积分开环的完整迁移。
期间也探索了 TMC2209 MSCNT 寄存器作为位置反馈源，
但因 MSCNT 采样 Nyquist 混叠和电机共振问题未采用。

### 41.2 方案演变

| 阶段 | 方案 | 结果 |
|------|------|------|
| 1 | MSCNT + PID 闭环 (diff >/<-512 回绕检测) | accum 始终为负, 方向误判 |
| 2 | 修正 dir_sign 取反 | 电机方向正确但 200→20000Hz 瞬间跳变丢步 |
| 3 | 加加速度限制 500→1000 Hz/周期 | 电机平稳但 accum 仍不收敛 (Nyquist) |
| 4 | R_MAX_SPEED 逐步降 (51200→40000→20000) | accum 仍 ~1024 而非目标值 |
| 5 | MSCNT_TEST 诊断 (5000Hz 定速采样) | raw 交替跳变, 确认为电机共振 |
| **6** | **时间积分开环 (最终方案)** | **电机正常旋转, 方向正确, 到位稳定** |

### 41.3 时间积分开环核心设计

```
position += spd_cmd × dt_ms / 1000  (每周期累加)
err = target - position
spd = |err| × R_PID_KP + R_MIN_SPEED
spd 经加速度限制 (1000Hz/周期) + max_spd 钳位
TMC_SetSpeedDirect(spd_cmd)  每轮强制写入 VACTUAL
```

**安全网 (保留):**
- DRV_STATUS.stst (bit31) — 硬件卡死即时判定 (~20ms)
- SG_RESULT — 堵转检测 (当前 R_SG_THRESHOLD=0 禁用)

### 41.4 关键参数 (app_config.h)

| 常量 | 值 | 说明 |
|------|-----|------|
| R_SPEED_RPM | 60 | R 轴最大转速 |
| R_STEPS_PER_REV | 51200 | 200全步×256微步 |
| R_PID_KP | 4.0f | 比例系数 (usteps→Hz) |
| R_MIN_SPEED | 200 | 最小 VACTUAL 频率 (Hz), 底速防静摩擦 |
| R_MAX_SPEED | 20000 | 最大 VACTUAL 频率 (≈23 RPM) |
| R_POLL_INTERVAL_MS | 8 | 控制周期 (ms) |
| R_POS_TOLERANCE | 5 | 到位容差 (usteps, ≈0.035°) |
| R_STABLE_COUNT | 3 | 连续稳定次数 = 3×周期 |
| R_SG_THRESHOLD | 0 | SG_RESULT 堵转检测: 0=禁用 |
| R_SG_MIN_SPEED | 800 | SG 有效最低速度 (保留, 当前无效) |
| R_TIMEOUT_MS | 8000 | 整体旋转超时 |

### 41.5 新增/修改文件

| 文件 | 改动 |
|------|------|
| `Task/app_config.h` | 删除 R_VERIFY_THRESHOLD/R_VERIFY_MAX_ITER, 新增全套 R_* 常量 |
| `Task/app_motion.c` | 删除 KTH7823 include + 静态变量; 重写 r_axis_rotate() 为时间积分 |
| `Task/app_motion.h` | 删除 r_axis_read_encoder() 声明 |
| `Task/app_host.c` | 删除 KTH7823 include 和 KTH7823_Init() 调用; 新增 HCMD_MSCNT_TEST case |
| `Task/app_uart_parser.h` | 枚举新增 HCMD_MSCNT_TEST |
| `Task/app_uart_parser.c` | MATCH("MSCNT_TEST") → HCMD_MSCNT_TEST |
| `Task/app_test.c/h` | 新增 MSCNT_Test() — 5 秒定速 MSCNT 采样诊断 |
| `Drivers/ZeMCU-G4/driver_tmc2209.h` | 新增 TMC_GetMSCNT(), TMC_GetDRVStatus(), TMC_SetSpeedDirect() 声明 |
| `Drivers/ZeMCU-G4/driver_tmc2209.c` | 新增上述三个函数实现 (~40 行) |
| `Drivers/ZeMCU-G4/driver_kth7823.c/h` | **未修改** (文件保留, 无编译引用) |

### 41.6 关键技术细节

**1. TMC_SetSpeed vs TMC_SetSpeedDirect:**
`TMC_SetSpeed()` 内部执行 VACTUAL=0 → RAMPMODE → VACTUAL, 耗时 ~13ms,
每轮调用会导致电机反复停机。`TMC_SetSpeedDirect()` 只写 VACTUAL (~3ms),
适用于闭环内调速。r_axis_rotate 仅在首轮用 TMC_SetSpeed 初始化方向,
后续全部用 SetSpeedDirect。

**2. MSCNT 采样 Nyquist 限速:**
MSCNT 为 10-bit (0~1023), diff 检测用 ±512 窗口。每周期 MSCNT 变化必须 <512,
否则方向误判。约束: `R_MAX_SPEED × 实际周期 < 512`。当前 20000Hz×~16ms=320<512。

**3. 加速度限制:**
每周期速度增幅 ≤1000 Hz, 防止 VACTUAL 跳变导致步进电机丢步。
0→20000Hz 约 300ms。

**4. 方向符号:**
VACTUAL>0 使 MSCNT 递增, 但电机物理旋转方向取决于相序接线。
当前 `dir_sign = (target>=0) ? 1 : -1`, 即 target 为正 → VACTUAL 为正 → 顺时针。

**5. MSCNT_TEST 诊断:**
串口发送 `MSCNT_TEST` 启动独立测试: TMC2209 定速 5000Hz, 每 20ms 读 MSCNT, 运行 5s。
输出 raw/prev/diff/dt, 用于验证 MSCNT 寄存器行为。
已知 5000Hz 下该电机存在共振, raw 值交替跳变但非硬件故障。

### 41.7 精度评估

时间积分精度依赖 TMC2209 VACTUAL 速度准确性。VACTUAL 频率精度由内部 12MHz
振荡器决定 (典型 ±5~10%)。90° 旋转 (12800 步) 的位置误差预估 ≤1.8°,
对 R 轴贴装角度要求可接受。

### 41.8 已知限制

- 无真实位置反馈: 丢步无法被检测和补偿 (仅 stst 卡死检测)
- KTH7823 驱动文件保留但未编译引用, 若将来需要可恢复
- MSCNT_TEST 在 5000Hz 下触发共振, 测试时应使用其他速度
- R_PID_KP=4.0 为理论值, 可能需要实机微调


### 42. P2 编码器定位与坐标系统一（2026-07-16）

#### 42.1 问题背景

P2 扫描使用 MKS 速度模式(0xF6)。旧方案用时间估算停止位置，误差 2mm+。
引入 31H CAN 编码器真值读取后，发现 P2_ENC_RATIO 标定严重错误
(100/679 ≈ 0.147)，实际硬件比值接近 1:1（10000步 → ~10004 encoder 单位）。

旧 ratio 导致 encoder 坐标系被压缩到电机脉冲坐标系的 1/6.79，
建系后散料区、下相机等绝对坐标移动整体偏移约 15mm
(7680步，恰好一个 Mark 间距)。

#### 42.2 修改文件

| 文件 | 改动 |
|------|------|
| `Task/app_config.h` | P2_ENC_RATIO_NUM/DEN: 100/679 → 10000/10004（CALIB_ENC标定） |
| `Task/app_motion.c` | `p2_stop_and_read_pos()` 重写：enc_detect→停止→enc_stop→overshoot回退(clamp ±3mm)→算real_x |
| `Task/app_motion.c` | `p2_scan_step_y` 补回 Coord_UpdateXY |
| `Task/app_host.c` | VISION_BUSY: 列起点读31H基准; VISION_GOT_STOP: 调用 p2_stop_and_read_pos |
| `Task/app_host.c` | 新增 HCMD_CALIB_ENC：读enc0→positionMode2Run 10000步→读enc1→输出精确ratio |
| `Task/app_uart_parser.h` | 枚举新增 HCMD_CALIB_ENC |
| `Task/app_uart_parser.c` | MATCH("CALIB_ENC") → HCMD_CALIB_ENC |
| `Drivers/ZeMCU-G4/driver_motor.c` | Motor_Init 显式 setWorkMStep 锁定 256 微步 |

#### 42.3 p2_stop_and_read_pos 流程

```
收到stp → 读enc_detect(X1+X2)           // 31H, 电机仍在速度模式
       → axis_stop + motorSyncTrigger + osDelay(200)
       → 读enc_stop(X1+X2)              // 停止后编码器
       → overshoot_enc = enc_stop - enc_detect
       → back_steps = -P2_ENC2STEP(overshoot_enc), clamp ±1500步(≈3mm)
       → positionMode2Run(X1+X2, back_steps)  // 位置模式回退
       → real_x = coord_start + P2_ENC2STEP(enc_detect - enc_start)
       → 返回 real_x
```

#### 42.4 CALIB_ENC 命令

发送 `CALIB_ENC`（需在 HOST_DEBUG 状态）：
1. 读 encoder0 (X1+X2)
2. positionMode2Run 10000 步
3. 读 encoder1
4. 输出 ratio = (enc1-enc0)/10000

6次标定结果：9999, 10009, 10008, 9997, 10007, 10001 — 平均 10003.5。
采用 10000/10004。

#### 42.5 根因分析

旧 P2_ENC_RATIO(100/679) 将 encoder 坐标压缩到 1/6.79。
`safe_move_to` 用 Coord 算相对位移时，Coord 值(encoder系)和电机脉冲坐标系不一致：
```
safe_move_to(scatter):  dx = scatter_target - Coord.x
  Coord.x 用 encoder 比值算 → 值偏小(压缩了6.79倍)
  → 算出的 dx 偏大 → 电机多走了 ≈15mm → 散料区偏了
```
修复后 ratio=1:1，Coord 和电机脉冲坐标系统一。
实测 delta = (12,6) 步 ≈ 0.02mm。

#### 42.6 已知限制

- MKS 速度模式不更新内部位置计数器，绝对移动(HOME/send_axis_abs)可能不准。PnP 自动流程全用相对移动(safe_move_to → positionMode2Run)不受影响。
- overshoot 回退用位置模式相对移动。正常 overshoot 仅 5-9 步(≈0.01mm)，上限 3mm 防编码器异常。
- P2_ENC_RATIO 依赖机械参数固定，更换电机/驱动器后需重新 CALIB_ENC 标定。

## 四十二、2026-07-18 会话 — R 轴性能优化与阻塞回归

### 42.1 背景

R 轴自 commit 15b19ae 改为非阻塞状态机（`r_axis_start` + `r_axis_poll` + `r_axis_state`）
后存在两个问题：（1）旋转速度慢，500ms 死等待过长；
（2）到位精度差，物理旋转量仅 ~50% 命令值。

### 42.2 探索过程

| 阶段 | 尝试方案 | 结果 |
|------|----------|------|
| 1 | PID 参数提速：Kp=15, R_MIN_SPEED=300, 去 osDelay(500)→10 | 电机只能发出白噪音不转 — stst 误判卡死 |
| 2 | 起步冲量 2000→5000 Hz + stst 去抖 5 次 + 加速限制 5000 | 能转但角度只有命令的 ~50% |
| 3 | 真实 dt_ms 时间积分（替代固定 8ms） | 积分准确了, 电机物理位置仍不准 |
| 4 | TMC2209 硬件位置模式（RAMPMODE=0, XTARGET） | XACTUAL 始终为 0, 电机不动 |
| 5 | XACTUAL 做位置反馈 + VACTUAL 调速（混合模式） | XACTUAL 读取静默失败, 电机无限旋转 |
| 6 | MSCNT 10-bit 溢出追踪做位置反馈 | MSCNT 读取同样静默失败, 电机无限旋转 |
| 7 | MSCNT 优先 + 时间积分 dt 兜底 | 仍然无限旋转 — 兜底也没生效 |

**根本原因：** TMC2209 单线 UART 在主循环交织环境下连续读写不可靠。
MSCNT/XACTUAL/DRV_STATUS 读取随机静默失败，位置永远不更新，
PID 保持极速输出。

### 42.3 最终方案：阻塞式 r_axis_rotate 回归

回到 commit 427759a 验证过的阻塞实现。`r_axis_rotate` 独占 CPU
执行 `while(1)` + `vTaskDelay(8ms)` 的 PID 循环，UART 读写不被打断。

非阻塞 API 保留为兼容层：
- `r_axis_start(angle, speed)` → 内部调 `r_axis_rotate(angle, R_SPEED_RPM)`，阻塞返回
- `r_axis_poll()` → 空函数
- `r_axis_state()` → 始终返回 `R_DONE`
- `host_start_r_correction()` → 调 `r_axis_rotate`，返回 `false`（调用方无需 phase 等待）

### 42.4 性能优化

| 参数 | 旧值 | 新值 | 说明 |
|------|------|------|------|
| `R_MAX_SPEED` | 20000 Hz (23 RPM) | 50000 Hz (58 RPM) | 极速 2.5x |
| `R_MIN_SPEED` | 200 Hz (0.23 RPM) | 1000 Hz (1.2 RPM) | 消除末端微步蠕动 |
| `R_POS_TOLERANCE` | 5 步 (0.035°) | 150 步 (~1°) | 容差放宽到 1~2° |
| `R_STABLE_COUNT` | 3 | 2 | 减少确认等待 |
| 加速限制 | 1000 Hz/周期 | 3000 Hz/周期 | 3x 提速 |
| 末端提前退出 | 无 | err<300 步 + 最低速 → 强制 DONE | 消除秒级蠕动 |
| 旋转后延迟 | vTaskDelay(20ms) | 移除 | 立即失能 |

### 42.5 修改文件

| 文件 | 改动 |
|------|------|
| `Task/app_motion.c` | 恢复 commit 427759a 的 `r_axis_rotate`；添加非阻塞兼容层 wrapper；末端提前退出；移除 20ms 延迟 |
| `Task/app_motion.h` | 添加 `R_State_t` 枚举 + `r_axis_start`/`r_axis_poll`/`r_axis_state`/`r_axis_rotate` 声明 |
| `Task/app_config.h` | R_MAX_SPEED→50000, R_MIN_SPEED→1000, R_POS_TOLERANCE→150, R_STABLE_COUNT→2 |
| `Task/app_host.c` | `r_axis_poll()` 从主循环移除；`host_start_r_correction` 改为调 `r_axis_rotate` 返回 false；osDelay(500)→10 |

### 42.6 已知限制

- 旋转期间 Host_Task 阻塞（90° ≈ 0.5s, 360° ≈ 2s），上位机命令暂不响应
- CAN 中断和 FreeRTOS 其他任务（TouchGFX, CAN_Process_Task）不受影响
- 容差 1~2° 对贴片角度可接受，需批量验证
- 时间积分精度依赖 TMC2209 内部振荡器（典型 ±5~10%）

---

## 四十三、2026-07-18 会话 — P2 31H 编码器响应解码修复

### 43.1 背景

P2 建系完成后机器坐标整体偏移约 7.5mm（约为 Mark 间距的一半），导致散料区等绝对坐标定位不准。31H 之前 P2 正常，加入 31H 编码器定位后出现此问题。

### 43.2 根因分析

**直接原因：** `p2_stop_and_read_pos()` 中的 31H 编码器读取始终失败（100ms 超时），回退到 `p2_scan_estimate_x()` 估算。估算使用速度模式脉冲常数 `16384`（实际应为 `32768`），导致估算位移约为实际位移的 1/2。Coord 被设为错误估算值，而电机停在物理正确位置，Coord 与电机物理位置之间产生不可消除的偏差。

**偏差传播链：** 偏差产生后，pos-detect 对齐使用相对移动（电机从物理位置移动到正确中心位置）但 Coord 基于错误估算做绝对更新，偏差无法消除 → `g_marks_actual` 全部偏移 → PCB Frame 内部一致但全局偏移 → 散料区 `safe_move_to()` 使用 Coord 算相对位移，偏差暴露。

**根本原因：** `CAN_Process_Task` 仅处理 0xF4/0xF5（位置模式完成/错误），未处理 0x31 编码器响应。MKS 电机正确返回了 31H 编码器数据，但响应在 `CAN_Process_Task` 中被丢弃。`g_enc_ready[id]` 和 `g_enc_pos[id]` 在整个项目中没有任何写入路径。

### 43.3 MKS 31H 响应格式

| 字节 | 内容 |
|------|------|
| Data[0] | 功能码 0x31 |
| Data[1] | 编码器值 byte 5 (MSB) |
| Data[2] | 编码器值 byte 4 |
| Data[3] | 编码器值 byte 3 |
| Data[4] | 编码器值 byte 2 |
| Data[5] | 编码器值 byte 1 |
| Data[6] | 编码器值 byte 0 (LSB) |
| Data[7] | CRC（校验和） |

48-bit 有符号大端编码器值。固件取 Data[3..6] 低 32 位组装 `int32_t`（实际值远小于 2^32）。

### 43.4 修复

**修复 1：`CAN_Process_Task` 增加 0x31 解码分支**（`Task/app_motion.c`）

在 CAN_Process_Task 主循环中，0xF4/0xF5 错误处理分支后增加解码逻辑：
取 Data[3..6] 低 32 位 big-endian 组装为 int32_t，存入 g_enc_pos[pkt.ID] 并置 g_enc_ready[pkt.ID] = true。

**修复 2：速度模式脉冲常数 16384→32768**（三处）

| 位置 | 文件 | 函数/用途 |
|------|------|----------|
| p2_scan_estimate_x() | app_motion.c:555 | 扫描位置估算 |
| p2_scan_step_y() | app_motion.c:574 | Y轴步进延时（位置模式，仅影响padding）**（2026-07-27 已修正为 MKS_PULSES_PER_REV=16384，见 §45）** |
| 列超时计算 | app_host.c:861 | 列超时 tick 计算 |

### 43.5 关键经验

- 速度模式(0xF6)电机实际位移对应 32768 脉冲/转，位置模式(0xF4/0xF5)为 16384（`MKS_PULSES_PER_REV`）。两者相差 2 倍。
- 31H 响应需要 CAN_Process_Task 显式解码后设置 `g_enc_ready`/`g_enc_pos`，否则所有轮询等待均 100ms 超时。
- 当 Coord 与电机物理位置不一致时，`safe_move_to()` 的相对移动正确（电机从物理位置移动），但 Coord 绝对更新基于错误基准，偏差持续存在且无法通过后续 pos-detect 消除。

### 43.6 涉及文件

| 文件 | 改动 |
|------|------|
| `Task/app_motion.c` | CAN_Process_Task 新增 0x31 解码分支；p2_scan_estimate_x()/p2_scan_step_y() 速度常数 16384→32768 |
| `Task/app_host.c` | 列超时计算速度常数 16384→32768 |
| `AGENTS.md` | §4.2 更新 31H 响应格式+处理器文档 + 速度常数说明；§10.2 新增 0x31 指令 |
| `HISTORY.md` | §43 新增本记录 |

---

## 四十四、2026-07-18 会话 — P4 下相机基线/校验流程

### 44.1 背景

P2 建系过程中电机停止仍存在 1~2mm 残余误差。该误差不影响 PCB 建系内部一致性，但会使机器内部坐标系与真实物理坐标系之间产生平移偏移，导致散料区、下相机站位等 flash 标定坐标定位不准。

为在软件层面补偿该误差，引入 P4 流程：用下相机对吸嘴中心做两次圆形标定，一次在 P2 建系前（基线），一次在 P2 建系后（校验）。两次测量得到的坐标差即为 P2 引入的整体漂移，随后做整体平移补偿。

### 44.2 新流程

```
下载 CSV 完成
  → HOST_P4_BASELINE（P4 基线校验）
  → HOST_MARK_ALIGN（P2 建系）
  → HOST_P4_VERIFY（P4 漂移校验）
  → 补偿整体漂移
  → HOST_FIND_COMP（P1 找元件）→ HOST_PICK → ...
```

| 阶段 | 状态 | 行为 |
|------|------|------|
| 基线 | `HOST_P4_BASELINE` | 移动到下相机站位，启动 P4 迭代对准吸嘴中心，记录 `g_p4_base_x/y` |
| 建系 | `HOST_MARK_ALIGN` | 原 P2 流程，建系结果写入 `g_pcb_frame` |
| 校验 | `HOST_P4_VERIFY` | 再次移动到下相机站位，P4 迭代对准，计算 `e = coord - base` |
| 补偿 | — | `Coord_UpdateXY` + 修正 `g_pcb_frame.origin` / `g_mark_avg_dx/dy` |
| 贴装 | `HOST_FIND_COMP`... | 原 P1/P3 循环 |

### 44.3 P4 协议（MaixCAM → G4）

| 帧 | 方向 | 说明 |
|----|------|------|
| `p4` | Host → Cam | 启动下相机圆形标定对位 |
| `pos` | Cam → Host | 未对准，后跟 `N:{dx} N:{dy}`（已是电机步数） |
| `go` | Host → Cam | Host 按偏差移动补偿后通知相机复测 |
| `ok` | Cam → Host | `|dx|<5px 且 |dy|<5px`，对准完成 |
| `err4_4` | Cam → Host | 5 轮未对准 |
| `err4_5` | Cam → Host | 等 `go` 超时 30s |

### 44.4 补偿算法

```
e_x = Coord_Get().x - g_p4_base_x
e_y = Coord_Get().y - g_p4_base_y

Coord_UpdateXY(Coord_Get().x - e_x, Coord_Get().y - e_y)   // 机器坐标回归真实
g_pcb_frame.origin_x_steps -= e_x                          // PCB 坐标系同步平移
g_pcb_frame.origin_y_steps -= e_y
g_mark_avg_dy -= e_x                                        // 降级路径同步平移
g_mark_avg_dx -= e_y
```

PCB 贴装坐标在补偿前后保持物理正确；散料区、下相机站位等 flash 标定坐标恢复正确。

### 44.5 错误处理

- P4 基线/校验阶段 `Vision_IsTimedOut()` 或 `err4_x` → 进入 `HOST_ERROR`，等待 `RESUME`/`ABORT`。
- 基线阶段本身只记录坐标，不判定“偏差是否过大”；偏差大小在校验阶段统一处理。

### 44.6 修改文件

| 文件 | 改动 |
|------|------|
| `Task/app_vision.h` | 新增 `VCMD_P4` |
| `Task/app_vision.c` | 新增 P4 子状态、帧解析 `process_p4_frame`、启动/Go 分支 |
| `Task/app_host.h` | 新增 `HOST_P4_BASELINE`、`HOST_P4_VERIFY` |
| `Task/app_host.c` | `download_done` 后进入 P4 基线；P2 完成后进入 P4 校验；新增 `start_p2_mark_align`、`start_p1_find_first`、`p4_baseline_step`、`p4_verify_step`；主循环 switch 增加两个状态 |
| `AGENTS.md` | §4.2 新增 P4 协议；§五任务架构状态表新增两态；§六数据流更新 |
| `HISTORY.md` | 新增 §44 本记录 |

### 44.7 已知限制

- P4 补偿假设 P2 引入的误差是**纯平移**，不补偿旋转或缩放误差。
- 补偿值仅在 P2 建系后应用一次，后续贴装循环中不再重新测量。
- P4 本身依赖下相机检测吸嘴圆，吸嘴必须位于相机视野内且 Z 高度与标定条件一致。

---

## 四十五、2026-07-27 会话 — MKS 0x02 到位信号发现与 0x31 Ping 方案

### 45.1 背景

长久以来所有电机到位都是时间估读，对长期优化不友好。用户要求根据 MKS CAN 协议实现
0x02 到位信号检测。此前多次尝试使用到位检测均以失败告终。本次会话通过系统排查，
最终发现根因并实现稳定接收。

### 45.2 排查过程

| 阶段 | 假设 | 验证方式 | 结论 |
|------|------|---------|------|
| 1 | 到位阈值太小导致 0x02 不发 | `motorSetArrivalThreshold` 50→150 步 | 无效 |
| 2 | 0x31 编码器请求挤占 CAN 总线，0x02 被延迟 | 关掉编码器读取 | 无效 |
| 3 | 同步模式导致 0x02 乱序 | 关掉同步模式 | 无效 |
| 4 | `motor_event_queue` 深度不够导致 0x02 丢失 | 扩大队列 | 无效 |
| 5 | 编码器读取后续帧冲掉了 0x02 | 注释掉编码器读取 | 无效 |
| 6 | CAN catch-all 日志确认 0x02 根本没发出 | 打开 [CAN] RX 全量日志 | **确认**：0x01 有、0x05 有，0x02 无 |
| 7 | 重新审视成功日志，发现规律 | 对比有/无 0x31 轮询时的日志 | **发现**：0x02 只在有 0x31 持续轮询时出现 |
| 8 | 验证 0x31 ping 主动 flush 方案 | `motion_wait_done` 每 50ms 发 0x31 | **稳定生效**：0x02 ~360ms 到位 |

### 45.3 根因

**MKS SERVO42D CAN TX mailbox 机制：**

- 0x01 响应：收到 0xF4 命令后**同步发送**（在命令处理 ISR 中），立即发出
- 0x02 响应：电机运行完成后**异步生成**，放入 CAN TX mailbox 等待发送
- MKS 的 CAN 控制器 TX mailbox 深度有限（推测 1~2 个），且**不会主动发送**——需要
  收到下一个 CAN 帧后才能触发 TX 缓冲区 flush

当只有 0xF4 命令而无后续 CAN 交互时，0x02 将永远卡在 TX mailbox 中。
持续 0x31 编码器查询充当了"踢邮箱"的角色——每收到一个 0x31 请求，电机回复编码器值，
同时会把 TX mailbox 中的所有待发帧（包括 0x02）一起推出。

**因此，0x02 其实一直都在，只是没有被"逼出来"。**

### 45.4 解决方案：0x31 Ping

在 `motion_wait_done()` 中每 50ms 向等待中的电机发送一次 0x31 编码器查询：

```c
ping_tick += 10;
if (ping_tick >= 50) {
    ping_tick = 0;
    if (need & EVENT_X1_DONE) readRealTimeLocation(1);
    if (need & EVENT_X2_DONE) readRealTimeLocation(2);
    if (need & EVENT_Y_DONE)  readRealTimeLocation(3);
}
```

`readRealTimeLocation` 纯发 CAN 帧不阻塞。0x31 返回的编码器值由 `CAN_Process_Task`
消费，不影响等待逻辑。

`motion_wait_done` 同时输出诊断日志：

```
[POLL] ok: bits=0x04 waited=360ms       ← 0x02 命中
[POLL] timeout: bits=0x00 need=0x04 ... ← 超时
```

### 45.5 实测结果

10mm @ 100 RPM 点动测试，每条指令 ~360ms 到位，0x02 成功率 100%。

| 指标 | 旧（纯时间估算） | 新（0x02 + 0x31 ping） |
|------|-----------------|------------------------|
| 到位判定 | 靠猜（g_move_pad_ms=3000, ~3.3s） | 靠 0x02 信号（~0.36s） |
| 可靠性 | 无硬件反馈 | 电机硬件回报确认 |
| g_move_pad_ms | 3000 | 500（0x02 可靠，fallback 仍够） |

### 45.6 副作用修正：p2_scan_step_y 速度常数

在 §43 中 `p2_scan_step_y()` 的速度常数被从 16384 改为 32768，声称是"修复"。
实际上位置模式(0xF4)的正确常数是 16384（`MKS_PULSES_PER_REV`），32768 是
速度模式(0xF6)的常数。

本次在重构 `p2_scan_step_y()` 时将其改回 `MKS_PULSES_PER_REV`（16384）。
同时将原有的 `osDelay(move_ms)` 盲等改为 `motion_wait_done` + 编码器验证。

---

## 四十六、2026-07-27 会话 — app_motion.c 运动函数全面重构

### 46.1 背景

在 0x02 到位信号稳定后，对 `app_motion.c` 的运动函数进行了一次系统重构。
统一了编码器读取、到位等待、错误处理逻辑。

### 46.2 新增辅助函数（static 文件内部）

| 函数 | 说明 |
|------|------|
| `motion_drain_queue()` | 排空 `motor_event_queue` 中旧帧 |
| `motion_read_done_bits()` | 原子读 `g_axes_done_bits` |
| `motion_clear_done_bits()` | 清零 `g_axes_done_bits` + `g_axes_error` |
| `motion_set_done_bits()` | 原子写（`|=`）`g_axes_done_bits` |
| `motion_read_encoder()` | 发 0x31 + 轮询 `g_enc_ready`（100ms 超时），替代分散的 encoder 读代码 |
| `motion_wait_done()` | 0x02 位轮询 + 0x31 ping + 堵转检测，替代原有的被动 `osDelay` 等待 |

### 46.3 重构函数

| 函数 | 旧机制 | 新机制 | 调用方 |
|------|--------|--------|--------|
| `move_xy_relative` | 内联 encoder 读 + 被动 poll 循环 + 时间估算 | `motion_wait_done` (0x02+ping) + 编码器验证 | `safe_move_to` → 全 PnP 流程 |
| `move_to` (static) | 内联被动 poll + 静默超时 | `motion_wait_done` + 超时自动刹车 + 编码器验证 | `MOTION_CMD_HOME` (TouchGFX 回零) |
| `p2_scan_step_y` | `osDelay(move_ms)` 盲等 + 32768 速度常数 | `motion_wait_done` + `MKS_PULSES_PER_REV` (16384) + 编码器验证 | P2 Mark 扫描 Y 轴步进 |
| `CAN_Process_Task` | 直接写 `g_axes_done_bits`、仅判 0xF4/0xF5 | 统一用 `motion_set_done_bits`、新增 0xFD 判定、`MotorDiag_CANRxHook` 诊断钩子、`DEBUG_MOTION` 日志守卫 | FreeRTOS 任务 |

### 46.4 其他修改

| 改动 | 说明 |
|------|------|
| `disable_sync_stop()` 移除 `motorSyncTrigger(0)` | 停止时不应触发同步执行 |
| `p2_scan_start()` `osDelay(2)→(20)` | X1→X2 speedModeRun 间隔保守化 |
| `g_move_pad_ms 3000→500` | 0x02 已可靠，fallback 时间缩短 |
| `[POLL] ok/timeout` 诊断日志 | 无需 DEBUG_MOTION 即可看到到位状态 |
| 两阶段到位修正 | 主 move 后编码器校验偏差 > ENC_TOLERANCE_STEPS 时，自动低速微调一枪（speed=50, acc=30），修正后重验 |
| `motorSetArrivalThreshold 150→125` | 与 ENC_TOLERANCE_STEPS 对齐，电机内部 PID 停止阈值 |
| `ENC_TOLERANCE_STEPS 100→125` | 与 arrival threshold 对齐，避免 0x02 通过但编码器误判 |

### 46.5 遗留

`MotionTask_Func` 中的 `MOTION_CMD_MOVE_TO` 分支（第 874 行）仍使用内联被动 poll
循环，未迁移到 `motion_wait_done`。此路径由 TouchGFX 桥接层触发（`app_touchgfx_bridge.c:171`），
在 0x31 ping 方案生效后仍会 2s 超时。非本次引入问题，可在后续统一迁移到 `move_to()`。

> **【2026-08-01 校正】** `app_touchgfx_bridge.c` 已随 GUI 独立板迁移删除；该路径对应功能已由 `gui_process_cmd` 复用 `handle_debug_cmd` 接管，本遗留项仅作历史参考。

### 46.6 涉及文件

| 文件 | 改动 |
|------|------|
| `Task/app_motion.c` | 新增 6 个 static 辅助函数；重构 move_xy_relative / move_to / p2_scan_step_y / CAN_Process_Task；disable_sync_stop / p2_scan_start 调整；g_move_pad_ms 3000→500 |
| `Task/app_motion.h` | 无接口变更 |

## 四十七、2026-07-27 会话 — Jog 实时位置感知功能

### 47.1 背景

Jog（手控连续运动）使用 `positionMode3Run` 以极远绝对位置（�}8388607）驱动电机，
停止后仅发 `0xF7` 急停，从未更新 `MachineCoord_t` 坐标——坐标停留在 Jog 开始前的值。

### 47.2 方案

"先分析后行动"模式，用户确认后实施。三步走：
1. Jog START 时读 MKS 编码器起始值（X1+X2 或 Y 轴）
2. MOVE_STOP 时急停 → 读编码器 → 算 delta → `Coord_UpdateXY` 增量更新
3. X1+X2 龙门偏差 > `ENC_TOLERANCE_STEPS`(125) → 低速 F4 微调（speed=50, acc=30）→ 日志报告残留偏差

日志格式沿用离散移动风格，坐标转换规则一致（`host_x = -motor_y`, `host_y = +motor_x`）：
```
[HOST] JOG_UP STOP -> (-150,320) 12345ms
```

### 47.3 修改文件

| 文件 | 改动 |
|------|------|
| `Task/app_motion.h` | 新增 `motion_read_encoder` 声明；新增 `jog_stop_update_coord` 声明 |
| `Task/app_motion.c` | `motion_read_encoder` 去 `static` 供跨文件调用；新增 `jog_stop_update_coord()` — 编码器 delta 计算 + 龙门偏差检测 + 低速微调 + `Coord_UpdateXY` |
| `Task/app_host.c` | 新增 `#include "timestamp.h"`；新增 `g_jog_name` / `g_jog_enc_x1_start` / `g_jog_enc_x2_start` / `g_jog_enc_y_start` 静态变量；四个 `MOVE_*_START` 分支各加 `motion_read_encoder` 起始读取；`MOVE_STOP` 重写为调用 `jog_stop_update_coord` + 时间戳日志 |

### 47.4 设计决策

| 决策 | 理由 |
|------|------|
| Jog 保持 `positionMode3Run`，不切速度模式 | 用户选方案 A，不改运动方向 |
| `jog_stop_update_coord` 放在 `app_motion.c` | 靠近编码器逻辑，不在 `app_host.c` 内联 |
| 龙门微调复用 `move_xy_relative` 框架 | 低速 F4（speed=50, acc=30），偏差 >300 步放弃 |
| 时间戳用 `TIM2_Get_Current_Timestamp_64b() / 1000` | 毫秒精度，与 timestamp.h 一致 |
| 日志不包含运动方向——因为 Jog 方向名已嵌在日志中 | `JOG_UP` / `JOG_DOWN` / `JOG_LEFT` / `JOG_RIGHT` |



### 48. P1 扫描模式升级：运动中识别 + 成功位置记忆（2026-07-31）

#### 48.1 背景

P1（上摄像头找元件）原有流程：`safe_move_to` 阻塞移动到子位置 → `Vision_Start` 启动 P1
→ 等 `stp` → 等 `VISION_DONE`。每次切子位置都有"运动等待时间 + 视觉检测时间"串行开销。
同时，后续同 cell 元件总是从中心（子位置 0）开始扫描，不利用之前成功找到的位置信息。

#### 48.2 需求

1. **运动中识别**：从子位置 A 移动到子位置 B 期间，P1 视觉同步运行。若途中锁定元件则立即停机检测。
2. **成功位置记忆**：同 cell 内存放同类型元件，找到后下一个同 cell 元件从该成功子位置开始。

#### 48.3 方案

`find_comp_step()` 重构为三级子状态机（`FIND_IDLE` → `FIND_MOVING` → `FIND_WAITING`），
运动使用 `move_start_async()` 非阻塞启动，状态机同时轮询视觉状态和运动完成标志。

#### 48.4 设计决策

| 决策 | 选择 | 理由 |
|------|------|------|
| 途中 stp 归属 | 目标子位置（`g_p1_found_pos = g_p1_scan_pos`） | 代码最简，间距仅 size/4，视觉偏移补偿 |
| wraparound | 末尾→0→记忆位置前停止 | 覆盖所有子位置 |
| 运动完成坐标 | 设为目标坐标（同 `move_xy_relative` 逻辑） | 位置模式可靠 |
| 途中停机坐标 | 读编码器算实际位移增量 | 任意位置都能恢复精确坐标 |
| 扫描速度 | 100 RPM（`P1_SCAN_SPEED`） | 低速保证运动中图像不模糊 |
| 记忆重置 | 新 PnP 开始 + RESUME 补料后 | 补料后元件分布已变 |

#### 48.5 代码变更

| 文件 | 变更 |
|------|------|
| `Task/app_motion.h:93` | 新增 `move_start_async()` 声明 |
| `Task/app_motion.c:614` | 新增 `move_start_async()` 实现（排空帧+清零+发指令+同步触发） |
| `Task/app_host.h:21` | 新增 `#define P1_SCAN_SPEED 100` |
| `Task/app_host.c:158~171` | 新增全局变量：`g_scan_start_pos[4]` / `g_p1_wrapped` / `FindSubState_t` / `g_find_sub` / 运动追踪上下文 |
| `Task/app_host.c:1347` | 新增 `p1_restore_coord()` — 运动中停机读编码器恢复坐标 |
| `Task/app_host.c:1376` | 新增 `p1_try_next_subpos()` — 子位置推进 + wraparound |
| `Task/app_host.c:1394~1490` | 重写 `find_comp_step()` — 三级子状态机 |
| `Task/app_host.c:447~463` | 修改 `start_p1_find_first()` — 改为设 FIND_IDLE，移除阻塞 move+Vision_Start |
| `Task/app_host.c:790` | 修改 RESUME 路径 — 重置 `g_scan_start_pos[]` |

#### 48.6 已知问题

`place_step`、RESUME 路径、P3吸入重试、PICK重试 等过渡路径仍使用旧模式 `safe_move_to` + `Vision_Start`。
`Vision_Start` 调用被 `FIND_IDLE` 的第二次 `Vision_Start` 覆盖（`reset_all`），功能正常但浪费一次 P1 启动。后续可逐步迁移。

---

## 四十九、2026-08-01/02 会话 — GUI 独立板迁移 + 代码审查修复

### 49.1 背景

屏幕（ST7306 LCD + TouchGFX）工作全部迁移到独立 G0B1 板，主控 G4 不再直接驱动屏幕；G4 与 G0B1 之间通过 SPI 文本命令协议通信（G0B1 SPI1 ↔ G4 SPI2），协议以《01-GUI通信接口清单.md》为准。

### 49.2 物理层与协议

- G4 SPI2：PB13(SCK)/PB14(MISO)/PB15(MOSI)，CS=PD10，INT=PD8（下降沿），10.625 Mbps（分频 16）
- ASCII 行协议，`\n` 结尾，单条 ≤128 字节；G0B1 未使用字节必须填充 0x00
- G0B1→G4：`MOVE_*` / `HOME` / `SET_*` / `HEAT` / `PUMP` / `LIGHT` / `VALVE` / `WIFI_CONNECT` / `WIFI_DISCONNECT` / `IMPORT_ENTER` / `IMPORT_EXIT`
- G4→G0B1：`TEMP_HEAT` / `TEMP_PCB` / `SMT_PROGRESS` / `LOG` / `WIFI_STATUS` / `IMPORT_DATA` / `IMPORT_TOTAL`
- `GUI_PROTO_VERSION 1` 预留

> **校正（2026-08-06）：** 本节为 GUI 独立板迁移初版（SPI v1.5）。当前 v1.6：PD8=DATA_RDY（主控→GUI）、PD9=REQ_TX（GUI→主控）、PD10=CS、PB12=IRQ（恒高不响应）；固定 128 字节帧、以 `\n` 结束并 `0x00` 填充；新增 `HANDSHAKE_REQ → HANDSHAKE_ACK`。当前实现与调试见 HISTORY.md §56、AGENTS.md §4.5。

### 49.3 代码变更

| 文件 | 改动 |
|------|------|
| `Task/app_gui_spi.c/h` | 新增：SPI2 收发、互斥锁、行解析、gui_cmd_queue、共享状态变量、GUI_SPI_Task |
| `Core/Src/spi.c`、`spi.h`、`main.c` | SPI2 恢复为 G0B1 GUI 通信（阻塞收发，去 DMA/IRQ），速率 10.625 Mbps |
| `Core/Src/dma.c` | 删除 SPI2 TX 用 DMA1_Channel1 |
| `Core/Src/tim.c`、`tim.h`、`stm32g4xx_it.c` | 删除 TIM7（TouchGFX VSYNC）、SPI2/DMA1/TIM7 残留中断；新增 EXTI9_5 与 HAL_GPIO_EXTI_Callback |
| `Task/app_host.c` | Bridge_* 全部替换为 GUI_SPI_*；gui_process_cmd 消费 GUI 命令；IMPORT_ENTER/EXIT、WIFI_CONNECT/DISCONNECT、LIGHT_ON/OFF 映射；温度 1s 限频；if_DOWNLOAD_READY 生命周期 |
| `Task/app_uart_parser.c/h` | 删除 SCREEN_TEST/MOTOR_DIAG；新增 LIGHT_ON/OFF、WIFI_CONNECT/DISCONNECT、IMPORT_ENTER/EXIT；所有命令保留 raw 原文 |
| `Task/app_esp_task.c` | Data_Transfer 改为 app_gui_spi；WiFi 状态回传 WIFI_STATUS（去重） |
| `Drivers/ZeMCU-G4/lcd.*`、`st7306/lcd.*`、`Task/app_screen_test.*`、`Task/app_touchgfx_bridge.*` | 删除 |
| `MDK-ARM/pnp_1.uvprojx`、`pnp_1.ioc` | 移除 TouchGFX/st7306 编译组与配置，SPI2/GUI 引脚同步 |
| `Core/Src/app_freertos.c` | guiSpi 任务（栈 4096）、gui_cmd_queue、gui_spi_mutex；移除无消费者的 Key_Task/keyEventQueue |

### 49.4 审查与修复要点

- 致命项：SPI2 多任务并发无锁 → `gui_spi_mutex`；GUI_SPI_Task 栈 1024 → 4096
- 严重项：队列满静默丢命令 → 10ms 超时+计数；温度无节流 → 1s 限频；`if_DOWNLOAD_READY` 从未置位 → 下载生命周期修复；`WIFI_CONNECT` 无法解析含空格密码 → 冒号前缀解析 + SSID/密码校验；SPI 速率 21.25 Mbps 与约束不符 → 降到 10.625 Mbps
- 建议项：重复 WIFI_STATUS 去重、bufsize==0 防护、HAL 返回值检查、static_assert、GUI_PROTO_VERSION、从机 0 填充约束文档化、旧命名清理、移除死 Key_Task/Temp

### 49.5 已知限制与遗留

- `WIFI_CONNECT` 的 SSID/密码目前只做校验并触发 ESP WiFi 开关，未透传 ESP32，需扩展 ESP 协议

> **校正（2026-08-06）：** 上述 `WIFI_CONNECT` 未透传的限制已解决，见 §55.6；当前通过 `ESP_SendWifiConnect()` 下发 `0x20/0x03 SSID\0PASSWORD`。
- IWDG 未启用，喂狗点待定
- `TouchGFX/`、`st7306/` 目录保留但不再编译，需手动清理
- 本机无 Keil，使用 ARM GCC 14.3.1 全量编译+链接通过；Keil 需另行实机确认
- 旧 TouchGFX 章节（§12、§32）已标注废弃；§46.5 对 bridge 文件的引用已校正

### 49.6 涉及文档

| 文件 | 改动 |
|------|------|
| `AGENTS.md` | 硬件/目录/任务/GPIO/编码规则更新；新增 §4.5 GUI SPI 协议 |
| `HISTORY.md` | 新增 §49；§12/§32 加废弃校正；§46.5 补校正说明 |

---

## 五十、2026-08-03 会话 — P2 三点最小二乘建系 + 残差剔除 + 移动失败处理 + 编码器兜底

### 50.1 背景

实测日志出现：Mark2 对齐移动超时（`[POLL] timeout: bits=0x03 need=0x07 waited=720ms`，0x02 卡 MKS CAN TX mailbox）被忽略——`VISION_GOT_POS` 未检查 `safe_move_to()` 返回值，旧坐标进入 `g_marks_actual[1]`（误差约 150 步）→ 两点法建系产生伪 theta → Mark3 verify err=0.513mm FAIL（阈值 0.3mm），而代码在 `valid=false` 时静默降级为一条错误公式继续贴片。

### 50.2 方案

1. **三点最小二乘建系（2D Procrustes 解析解，无矩阵库）：** 实测机器坐标 a_i = `g_marks_actual[i]`（步数）→ cam 坐标 `ac=(-a_y, a_x)`；理论点 t_i = `g_marks[i].target`（mm × STEPS_PER_MM=512）。去质心后 `dot_sum=Σ(pcx*ptx+pcy*pty)`、`cross_sum=Σ(pcx*pty-pcy*ptx)`，`theta = -atan2f(cross_sum, dot_sum)`；平移 `(o_cx, o_cy)` 由均值差求得；origin 写入 `(o_cy, -o_cx)`、`rotation_rad = theta`（与 §4.2.1 映射一致）。逐点残差（mm）= 理论点经 R(theta)+origin 变换回机器坐标与实际 a_i 距离 ÷512。
2. **残差剔除 + 回退两点法：** max 残差 < `MARK_VERIFY_ERR_MM`(0.3) → valid；否则剔除残差最大点，按**原两点法公式**（`theta=actual_ang-theory_ang` + 中点平移，2ccd13f/15b19ae 一系）重算，被剔除点残差 <0.3 → valid（打印 DEGRADED）；仍超阈值 → valid=false。
3. **FAIL 处理（不再静默降级）：** valid=false → 重置 `g_mark_count_done/g_mark_offsets/g_marks_actual/g_mark_just_jumped/g_p2_pos_iter` 后 `start_p2_mark_align()` 重跑一次（`g_p2_retry_cnt`，上限 `P2_RETRY_MAX=1`，下载新文件时清零）；重试仍 FAIL → `Vision_SendEnd + Vision_ForceIdle + HOST_ERROR`。
4. **根因修复：** `VISION_GOT_STOP`（Mark 跳转）与 `VISION_GOT_POS`（对齐移动）检查 `safe_move_to()` 返回值，失败重试一次，仍失败 → ERROR，旧坐标不再进入 `g_marks_actual`。
5. **编码器兜底（app_motion.c `move_xy_relative` 超时路径）：** 超时后 `osDelay(80)` + 读 31H 编码器，各轴 |实际-指令| ≤ `ENC_TOLERANCE_STEPS`(125) 且 X1/X2 互差 ≤125 → 按成功处理（`Coord_UpdateXY(target)` 返回 0），避免 0x02 卡邮箱误判失败。
6. **顺手修复：** 降级贴片公式补原点+符号（`machine_x=cy+origin_x+p3_offset_x; machine_y=-cx+origin_y+p3_offset_y`，与框架路径 θ=0 一致）；`Applied frame shift` 打印双负号修复。
7. **临时诊断开关移除（验证通过后）：** `g_can_rx_diag/g_can_tx_diag`（P4 基线 / P2 jump / P2 align 三处 + driver_can.c 打印 + driver_can.h extern）全部删除。

### 50.3 代码变更

| 文件 | 改动 |
|------|------|
| `Task/app_host.c` | 新增 `P2_RETRY_MAX` / `g_p2_retry_cnt`；VISION_GOT_STOP / VISION_GOT_POS 移动失败重试一次 + ERROR；VISION_DONE 重写为三点 LS + 残差剔除 + 两点回退 + 重试；降级贴片公式修正；frame shift 打印修复；移除三处诊断开关 |
| `Task/app_motion.c` | `move_xy_relative` 超时路径 31H 编码器验证兜底；移除 `[CANRX]` 诊断打印块 |
| `Drivers/ZeMCU-G4/driver_can.c/h` | 移除 `g_can_rx_diag/g_can_tx_diag` 定义 / 声明 / `[CANTX]` 打印 |

### 50.4 验证

- **离线数值自检（日志数据反推）：** a=[[33988,-86224],[33988,-93904],[41804,-86449]]（步）、t=[[5,5],[20,5],[5,20]]mm：LS theta=-1.091° origin=(31572,-83642) 残差 [0.192,0.112,0.132]mm OK；剔除 mark2 回退 0.296mm DEGRADED；θ=0/5° 合成数据精确还原。
- **真机日志（2026-08-03）：** `P2 3-point fit OK (max res=0.160mm)`（残差 [0.099,0.062,0.160]，theta=1.05°）；`[MOTION] timeout but encoder verified` 生效 2 次（P4 基线移动、Mark2 对齐移动）；P4 verify drift=(-576,-22) `Applied frame shift (576,22)` 正常；无 FAILED/ERROR。
- **语法检查：** ARM GCC 14.3.1 `-fsyntax-only` 三文件通过（本机无 Keil / 无 make；CMakeLists.txt / CMakePresets.json 已由用户删除，不再使用 CMake）。

### 50.5 遗留问题

- `[DIAG] After P2` 诊断 delta 约 -15mm：旧诊断拿 `g_marks[2]` 推算位置，蛇形映射后最后一个对齐 mark 不是 mark3，公式失效，仅打印误导，建议后续修复或删除。
- `app_test.c cam_p2_full_test_run`（CAM_TEST P2）仍是两点法建系，未同步三点 LS（仅测试诊断用，不影响生产）。
- 旧文档处理：§22.4 / §22.11 / §40.7 / §43.2 / §44.1 中「Mark3 验证」旧描述已删除或更新（用户确认删除旧记录）；§30 / §31 仿射建系 swap 修复清单保留为历史 commit 事实。

## 五十一、2026-08-03 会话 — P1/P3 由单次检测改为 5 次迭代对齐（回归 ok/go 复测模式）

### 51.1 背景

P1/P3 自 2026-07-11 迁到单次检测后，主控对每个元件只修正一次视觉偏移，无收敛校验：散料偏移大、吸嘴偏心、元件倾斜时，单次检测残留误差直接进入拾取/贴装坐标；而 P2（≤7 轮）与 P4（≤5 轮）已是迭代对齐。决定将 P1/P3 改为**迭代对齐（上限 5 轮）**，复用 P2/P4 已验证的 `pos...end → 主控修正 → go 复测 → ok 收敛` 模式。

### 51.2 协议变化

- **pos 数据包字段不变**（仍为电机步数）：P1 `pos` `N:{dx}` `N:{dy}` `N:{final_ao}` `N:{class_id}` `end`（4 字段）；P3 `pos` `N:{dx}` `N:{dy}` `N:{final_ao}` `end`（3 字段）。
- **新增 `ok` 帧：** Cam 检测到 |dx|、|dy| 均 < 对准阈值（建议与 P4 一致 5px，cam 端可配置）→ 发 `ok`，主控置 `VISION_DONE`。
- **`go` 复测：** 主控收到 `pos...end`（VISION_GOT_POS）后按 §4.2.1 轴映射修正移动（P1：`dx_s=-(r->dy)`、`dy_s=-(r->dx)`；P3：`dx_s=+(r->dy)`、`dy_s=-(r->dx)`）→ 发 `go` → Cam 重新采集平均后复测，循环直至 `ok`。
- **5 轮上限：** 主控侧迭代计数（`g_p1_align_iter`/`g_p3_align_iter`）达 5 仍未 `ok` 即强制结束（P1 进 HOST_PICK、P3 进 HOST_MOVE_TO_PCB），防相机旧固件不发 ok 时死循环；Cam 端第 5 轮可发 `err1_9`/`err3_9` 结束会话。
- **角度取最后一轮** pos 包中的值（P1 `-angle*100`、P3 `+angle*100` 符号规则不变），R 轴修正逻辑不变。

### 51.3 主控端改动

| 文件 | 改动 |
|------|------|
| `Task/app_vision.c` | `process_p1_frame()` / `process_p3_frame()`：`end` 分支由直接置 `VISION_DONE` 改为置 `VISION_GOT_POS`（保留 dx/dy/angle/class 结果）；新增 `ok` 帧解析 → 置 `VISION_DONE`；`Vision_Go()` P1/P3 分支加迭代计数与日志（`g_p1_iter`/`g_p3_iter`，`reset_all` 清零） |
| `Task/app_host.c` | 新增 `P1_ALIGN_MAX_ITER`/`P3_ALIGN_MAX_ITER`（5）与 `g_p1_align_iter`/`g_p3_align_iter`；`find_comp_step()` 新增 `VISION_GOT_POS` 分支（P1 修正偏移→计数→超限 `p1_align_finish` 或 `Vision_Go` 复测），`VISION_DONE`（ok）重构为只收尾（新增 `p1_align_finish()`：记忆位置 + cam_to_nozzle 补偿 + HOST_PICK），FIND_IDLE 入口复位计数；`offset_check_step()` 新增 `VISION_GOT_POS` 分支（P3 修正 + `g_p3_offset_x/y` 累计→计数→超限收尾或复测），`VISION_DONE`（ok）删除偏移应用（仅角度矫正 + 过渡 PCB）；`move_to_bottom_step()` 复位计数 |
| `Task/app_test.c` | CAM_TEST 的 P1/P3 测试：`VISION_GOT_POS` 分支换算修正（去掉过时 `STEPS_PER_MM/1000.0f` 像素缩放，直接使用电机步数）+ 新增 5 次迭代上限强制结束 |

### 51.4 摄像头端配合要求（MaixCAM2 固件，其他团队）

- P1：Phase0 搜索流程不变（`stp`/`go`）；Phase1 每轮检测后未对准发 `pos` 包（4 字段）+ `end`，等 `go` 复测；对准发 `ok`；第 5 轮未对准发 `err1_9` 结束会话。
- P3：Phase0 吸嘴检查与 `err3_8` 逻辑保留；Phase1 未对准发 `pos` 包（3 字段）+ `end`，等 `go` 复测；对准发 `ok`；第 5 轮未对准发 `err3_9`。
- 每轮复测必须重新采集平均后再判定，不得用同一帧数据重复回包；角度每轮返回，主控取最后一轮。

### 51.5 风险与兼容性

- **相机固件未同步升级（旧单次固件）：** 旧固件发完一次 `pos...end` 后不再响应 `go`，主控在 5 轮计数内最多再等 10s（P1 子位置超时）/ 120s（Vision 总超时）后降级收尾。升级顺序建议**先相机、后主控**（或同版本发布）。
- **节拍影响：** P1 处于扫描流程，迭代会为每个元件增加若干轮"采集+移动"时间；`P1_SCAN_SPEED=100` 低速已就绪，需实测整板节拍。
- **err1_9 语义：** 该错误码已在 `app_host.c` recoverable 列表（每元件最多重试 3 次），与 5 轮迭代计数叠加后的重试次数需实测确认（若不想重试需单独处理）。（2026-08-03 已更新：recoverable 列表改为 `err1_6`，`err1_9` 移出，见 §52）
- **角度不参与收敛判定：** 迭代仅判 XY，R 轴角度仍以最后一轮为准，避免角度抖动拖死迭代。

### 51.6 验证

- [x] 编译：Keil UV4 (ARMCLANG V6.21) 全量编译通过，0 Error / 1 Warning（`driver_tmc2209.c` 空循环体，原有）
- [ ] 真机：P1 对散料元件、P3 对吸嘴元件各跑 10 次，记录迭代轮数与收敛率
- [ ] 旧相机固件兼容性回归（go 超时降级路径）
- [ ] `app_test.c` CAM_TEST 的 P1/P3 测试回归

### 51.7 遗留问题

- 对齐阈值（5px 建议）当前由 cam 端判定，主控无独立校验；如需主控侧二次校验需加字段。
- §38 的历史描述（v2→v3 单次检测迁移）保留为历史事实，不修改。
- `app_test.c` 的 P2 测试仍是两点法建系（CAM_TEST 用），未同步三点 LS（仅测试诊断用，不影响生产）。

> **校正（2026-08-05）：** §51/§52 记载的 P1 迭代对齐（5/8 轮）已被 cam2test3 的 P1 批量单次上报覆盖为主流程；主控保留旧迭代解析作为兼容回退。当前 P1 协议以 §54 为准。

## 五十二、2026-08-03 会话 — P1 坐标恢复漏乘符号 → P3/HOME 偏右 6.2cm 修复

### 52.1 背景

实测现象：PnP 流程中 P1 找料正常，但**移动至下相机（P3）明显偏右约 6.2cm**；随后点击 HOME 回原点也往右偏约 6.2cm。P1/P2/P4 均正常。用户确认 P1 流程本身无误，要求定位根因。

### 52.2 根因分析

**根因：`p1_restore_coord()`（`Task/app_host.c`）的 Y 轴增量漏乘编码器符号修正 `MOTOR_Y_ENC_SIGN(-1)`。**

- 出错代码（修复前）：`int32_t delta_y = (dy != 0) ? d3 : 0;`（d3 为 Y 电机 31H 编码器原始增量）
- 正确对照（同工程多处均乘符号）：`jog_stop_update_coord()`（`app_motion.c:723`）`dy_delta = MOTOR_Y_ENC_SIGN * (e3 - enc_y_start)`；`move_xy_relative()` 校验、`p2_scan_step_y()` 等。
- 结构性原因：`MOTOR_Y_ENC_SIGN` 原只定义在 `app_motion.c`（非头文件），`app_host.c` 无法引用，导致该处漏乘。
- 触发路径：P1 扫描 `FIND_MOVING` 的 `VISION_GOT_STOP` / `VISION_ERROR` / 超时分支调用 `p1_restore_coord()` 恢复坐标 → `g_coord.y` 被写入错误值（误差 = 2×|dy|）。
- 传播机制：`safe_move_to()` 内部按**相对增量**移动（`dx = target - c0`，`app_motion.c:651-652`），`g_coord` 的误差 1:1 传导到所有绝对目标移动（`move_to_bottom_step` 去下相机、HOME `safe_move_to(0,0)`）。
- 量化吻合：默认标定 cell0/subpos0 目标 `ty = -16281` 步（cam_to_nozzle≈0），`2×16281/512 ≈ 63.6mm`，与实测 6.2cm 吻合（差 1.6mm 属标定微差）。
- 为何 P1/P2/P4 正常：P1 有视觉迭代闭环（`GOT_POS` 每轮相对修正），物理位置收敛但 `g_coord` 已污染；P2 建系用编码器实测绝对值 `g_marks_actual`；P4 在 P1 之前运行；三者均不经过 `p1_restore_coord` 的污染路径。
- 溯源：`git blame` 确认由 commit `6770505a`（2026-08-01，P1 非阻塞扫描子状态机）引入，一直存在。

### 52.3 修复

| 文件 | 改动 |
|------|------|
| `Task/app_motion.h` | 新增 `#define MOTOR_Y_ENC_SIGN (-1)`（供 `app_host.c` 跨文件引用） |
| `Task/app_motion.c` | 删除本文件内重复宏定义（该文件为 LF 换行，编辑时保持） |
| `Task/app_host.c` | `p1_restore_coord()`：`delta_y = (dy != 0) ? (int32_t)(MOTOR_Y_ENC_SIGN * d3) : 0;` |

X 轴 `delta_x = (d1+d2)/2` 与 `jog_stop_update_coord()` 一致，无需符号修正（仅 Y 轴）。

### 52.4 验证

- [x] 编译：Keil UV4 全量 0 Error / 1 Warning（`driver_tmc2209.c` 空循环体，既有）
- [x] 交叉审查：2 个只读 agent 独立验证（量化 63.6mm + 传播链 + 排除项）结论一致；补丁审查 6 项全 OK
- [x] 真机：用户确认 P3 移动与 HOME 回原点恢复正常（2026-08-03）

### 52.5 相关文档同步（2026-08-03）

- AGENTS.md 同步：P1/P3 迭代上限 5→8（`P1/P3_ALIGN_MAX_ITER=8`，1 init pos + cam 7 轮）；错误码按《通讯接口(cam与主控)-P1P3.md》更新（P1 增 `err1_6`、删 `err1_9`；P3 增 `err3_6/err3_7`、删 `err3_9`）；§4.2.3 辅助函数行号更新；§10.3 标注 `app_motion.c` LF 换行例外。
- §51.7 遗留问题中「err1_9 已在 recoverable 列表」描述已过时（现为 `err1_6` 加入 recoverable、`err1_9` 移出），以本文档为准。
- 行号提醒：`p1_restore_coord()` 现位于 `app_host.c:1505`（§48 记录 1347 已过时）。

### 52.6 遗留问题

- `g_p3_nozzle_retry` 在 RESUME / 新下载时不清零（既有问题，未处理）。
- `app_test.c` CAM_TEST 的 P1/P3 测试循环仍为 5 轮上限，与新协议 8 轮不一致（测试模块按用户要求未动）。
- `MOTOR_Y_ENC_SIGN` 依赖物理接线约定，若更换电机 / 接线需重新标定符号。

> **校正（2026-08-05）：** 当前 P1 主流程以 §54 的批量单次上报为准；§52 中 P1 迭代上限/错误码描述不再代表主流程，仅保留历史。

## 五十三、2026-08-04 会话 — P4 基线更新下相机站位 + P4 dx/dy 单位确认

### 53.1 背景与需求

- 机械回原点（原点为孔位）每次存在少量误差，导致 Flash 标定的下相机站位 `g_calib.bottom_cam_x_steps/y_steps` 与当前坐标系下真实的下相机中心存在偏移。
- P4 基线流程本身会把吸嘴迭代对准到下相机中心，用户要求：**记录基线坐标的同时更新下相机站位**，吸收归零误差。

### 53.2 改动

| 文件 | 改动 |
|------|------|
| `Task/app_host.c` | `p4_baseline_step()` 的 `VISION_DONE` 分支：记录 `g_p4_base_x/y` 后同步 `g_calib.bottom_cam_x_steps = g_p4_base_x`、`g_calib.bottom_cam_y_steps = g_p4_base_y` |

- verify 移动（`app_host.c:1302`）与 P3 站位（`move_to_bottom_step()`，`app_host.c:1871`）读取同一字段，自动使用更新后的站位。
- 仅 RAM 生效、不落盘（归零误差每次开机不同，持久化无意义；如需保留可发 `SAVE_CALIB`）。
- `safe_move_to` / `move_xy_relative` 不读该字段，无内部影响；无周期性 `Calib_Load`，运行中不会被覆盖。
- 已知边界：verify 的漂移计算在 P4 对位完成后取值，与 bottom_cam 站位无关；本次更新收益主要体现在 P3 站位与下一批 baseline。

### 53.3 P4 dx/dy 单位确认

- 用户确认：cam 端直接发送处理好的数据（电机步数），G4 端直接使用，无需缩放。
- 代码证据：`app_vision.c:465`（注释“已是电机步数”）；`app_host.c:1390-1393 / 1442-1445`（无缩放直接使用）。
- `tools/_cam_doc.md`（cam 团队文档）§9.3 / §12.2 / §12.3 仍描述“发送原始像素偏差”，为过时表述，待 cam 团队同步；AGENTS.md 描述正确，无需改动。

### 53.4 验证

- [ ] 编译：待 Keil UV4 验证
- [ ] 真机：待验证（P3 去下相机站位应更贴近相机中心）

### 53.5 遗留问题

- `RESTORE_CALIB` / `SET_BOTTOM_CAM` 无状态守卫，PnP 运行中上位机仍可覆盖站位（既有行为，未处理）。

## 五十四、2026-08-04/05 会话 — MaixCAM2 帧协议升级（2B LEN + CRC-16/MODBUS）+ P1 批量队列 + 补料续跑

### 54.1 背景与协议决定

- cam 端升级帧格式：`0x7E | LEN_H | LEN_L | PAYLOAD | CRC_H | CRC_L | 0x7F`；LEN 为 2 字节大端，CRC-16/MODBUS 只对 PAYLOAD 计算，大端输出。
- 主控端同步升级；新旧帧格式不兼容，两端必须同步发布。
- P1 主流程改为批量单次上报，一次返回视野内全部目标；旧迭代对齐解析保留为兼容回退。
- P2 扫描时序维持旧流程：先移动到扫描起点，`p2` 后直接开始扫描，不等待 `rdy` 门控。
- P2/P3/P4 遇到未识别或不可恢复视觉错误时：停运动、回机械原点、进入 `HOST_ERROR` 并报错。

### 54.2 帧层改动

| 文件 | 改动 |
|------|------|
| `Task/app_vision.c` | `FRAME_PAYLOAD_MAX=512`；`send_frame()` 双字节 LEN + CRC；`feed_byte()` 增加 `LEN_H/LEN_L/CRC_H/CRC_L`；CRC 错误计数 |
| `Task/app_vision.h` | 新增 `Vision_GetFrameCrcErrors()` |
| `Drivers/ZeMCU-G4/driver_uart.c` | `RX_BUFFER_SIZE` 256 → 600，容纳 P1 批量帧 |

### 54.3 P1 批量队列

- `app_vision.c` 解析 cam 批量格式：`pos -> N:<数量> -> N1/N2... 的 dx/dy/ao -> N:<class_id> -> end`。
- `VisionResult_t` 增加 `targets[P1_MAX_TARGETS]`、`target_count`、`p1_batch_mode`，`P1_MAX_TARGETS=10`。
- `app_host.c` 新增 `g_p1_queue[]`：把 cam 返回的全部目标换算为吸嘴取料坐标，批内逐个取料，不再每次重复打开摄像头。
- 队列耗尽或下个元件类别变化后才重新启动 P1；`g_scan_start_pos` 记忆保留。

### 54.4 P1 类别与 mv 时序

- 类别流程：`p1 -> rdy -> cls -> N:{id} -> end` 后，主控先控制电机停稳，再发 `go`，cam 才开始 Phase0。
- 保留“移动中检测”能力：P1 在 `FIND_IDLE` 启动运动的同时启动视觉；移动中完成类别回复后，等电机停稳再发 `Vision_GoScan()`。
- `mv` 改为阻塞式：cam 连续 50 帧未检测到目标时发 `mv` 并等待；主控移动/切换视野，电机到位后发 `go` 继续同一次 P1；中止时发 `end`。
- 新增 `Vision_GoScan()`：类别确认或 `mv` 后发 `go` 开始 Phase0，但状态仍等待 `stp`。

### 54.5 补料错误态与 CONTINUE

- P1 扫完所有子位仍为空时，进入 `enter_p1_refill_error()`：发 `end`、停运动、回机械原点、发 `ERROR`/`REFILL_NEEDED`、保存当前元件进度。
- 新增上位机命令 `CONTINUE`（`Task/app_uart_parser.h/c`），收到后默认补料完成，从保存的元件继续识别和贴装，回复 `CONTINUE_OK`。
- `RESUME` 命令保留，行为不变。

### 54.6 代码变更

| 文件 | 改动 |
|------|------|
| `Task/app_vision.c/h` | 帧协议、CRC、P1 批量解析、`VISION_GOT_MOVE`、`Vision_GoScan()` |
| `Task/app_host.c` | P1 批量队列、类别后 `go`、`mv` 续扫、补料错误态、`CONTINUE` 处理、P2/P3/P4 未识别错误回原点 |
| `Task/app_uart_parser.c/h` | 新增 `HCMD_CONTINUE` / `MATCH("CONTINUE")` |
| `Task/app_test.c` | P1 批量测试、`Vision_GoScan()`、`mv` 测试处理、P2 测试去掉像素缩放 |
| `Drivers/ZeMCU-G4/driver_uart.c` | RX 缓冲扩容 |

### 54.7 验证

- [x] CRC 向量：`N:123` → `0x6C44`；`123456789` → `0x4B37`
- [x] ARM GCC 14.3.1 `-fsyntax-only` 通过
- [ ] Keil UV4 全量编译：本机无 UV4.exe/ARMCC/ARMCLANG，待用户环境验证
- [ ] 真机联调：待 cam 端实际合入 2B LEN + CRC 后验证

### 54.8 涉及文档同步

- `AGENTS.md`：帧协议、P1 批量流程、P2 当前时序、补料 `CONTINUE`、`VisionResult_t` 字段同步。
- `HISTORY.md`：§51/§52 追加“以 §54 为准”的校正说明，历史正文不改写。

## 五十五、2026-08-06 会话 — ESP32 SPI v3.1 协议升级与主控端实现

### 55.1 背景

- 依据《ESP32-C3 与 STM32 主控 SPI 通讯接口文档 v3.1》更新主控端 SPI 通讯模块，不改 ESP32 端、运动控制、视觉识别等其他模块。
- v3.1 相对旧记录的关键变化：固定 128 字节帧、IRQ 场景 B、CMD 0x40/0x50/0x60、CSV 上传 0x70/0x71、WiFi 凭据 0x20/0x03、PC13 做 IRQ、删除 PC2/C3RESET 硬复位。

### 55.2 硬件与 GPIO

| 项 | 变更 |
|------|------|
| SPI4 | PE2(SCK) / PE5(MISO) / PE6(MOSI)，CS=PE3 |
| ESP32 IRQ | PC13，EXTI15_10 下降沿，GPIO_PULLUP |
| ESP32 RST | 删除 PC2/C3RESET；`ESP_HardReset()` 移除 |
| pnp_1.ioc | PC13 由 `GPIO_Output/C3RESET` 改为 `GPIO_EXTI13/ESP_IRQ` |

### 55.3 帧格式与命令

- 固定 128 字节：`CMD(1B) + SUBCMD(1B) + LEN(1B) + PAYLOAD(123B) + SEQ(1B) + RESERVED(1B)`。
- `CMD=0x70` 的 DATA 帧前 2 字节为小端帧号，其后为原始 CSV 字节，必须按 LEN 精确提取，禁止按字符串或 0x00 截断。
- 命令：0x10 数据更新 / 0x20 系统控制 / 0x30 状态查询 / 0x40 贴片流程 / 0x50 日志 / 0x60 加热台 / 0x70 CSV 上传 / 0x71 文件回执。

### 55.4 双向通信与保护

- 场景 A：主动下发前检查 IRQ，IRQ 高才发送；发送后延时 2ms 并再次检查 IRQ。
- 场景 B：EXTI ISR 只置 `esp32_irq_flag`，`ESP_Task` 发送全 0xFF 哑元从 MISO 读取 ESP 数据，延时 2ms 后清标志。
- CS 拉低后 100ms 未完成传输则强制拉高；任务层所有 `ESP_SPI_Transfer()` 调用检查返回值并报异常。
- `0x30` 回复天然晚一轮，通过 SEQ 映射表匹配。

### 55.5 CSV 上传与回执

- `0x70/0x01 START`：解析 `len/frames/crc32`，保存会话参数，回 `RESULT(ok/fail:<code>)`。
- `0x70/0x02 DATA`：校验帧号连续性，按 LEN 精确写入 W25Q64 `0x100000` 128KB 区，每帧回 `NEXT(期望帧号)`。
- `0x70/0x03 END`：校验总长度、帧数、CRC32，成功才导入 Host CSV，失败回 `fail:1/2/3/4`。
- 文件会话期间不主动发送 0x30/0x50 等无关帧；W25Q64 公共接口继续由 `w25q64_mutex` 保护。

### 55.6 日志与 WiFi

- PnP 各步骤通过 `ESP_SendLog()` 入队，`ESP_Task` 发送 `0x50/0x01`。
- `WIFI_ON`（0x20/0x01）仍用于使用当前凭据连接；`WIFI_CONNECT`（0x20/0x03）透传 `SSID\0PASSWORD` 并立即切换连接。

### 55.7 代码变更

| 文件 | 变更 |
|------|------|
| `Task/app_esp_task.c/h` | 场景 A/B、SEQ、CSV 会话、WiFi 凭据队列、日志发送、超时处理 |
| `Task/app_host.c/h` | PnP 日志、`Host_CsvImportAbort()`、`WIFI_CONNECT` 透传 |
| `Drivers/ZeMCU-G4/driver_esp32.c/h` | 删除 PC2/RST，PC13 EXTI，100ms CS 超时拉高 |
| `Core/Src/gpio.c`、`Core/Inc/main.h` | 删除 C3RESET，PC13 配置 EXTI |
| `Core/Src/stm32g4xx_it.c` | EXTI15_10 保留置标志，EXTI2 不再用于 ESP32 |
| `Core/Src/app_freertos.c` | `esp_wifi_cfg_queue`、日志队列 32、`ESP_Task` 栈 4096 |
| `pnp_1.ioc` | PC13 改为 EXTI13/ESP_IRQ |

### 55.8 验证

- [x] ARM GCC 14.3.1 `-fsyntax-only` 通过。
- [x] Ninja 构建目录完成 36 个目标文件编译，并链接出 `pnp_check.elf`。
- [ ] Keil UV4 全量编译待用户环境验证。
- [ ] ESP32 端真机联调待硬件确认。

### 55.9 文档同步

- `AGENTS.md`：硬件表、GPIO 速查、任务表、§4.5 WIFI_CONNECT 状态更新，新增 §4.6 ESP32 SPI v3.1。
- `HISTORY.md`：追加本节；§15/§34/§49 为历史记录，以本节和 AGENTS.md 当前状态为准。

> **校正（2026-08-06）：** §55 为首次实现记录；后续对照 v3.1 文档的复核与修复见 §57。

## 五十六、2026-08-05/06 会话 — GUI SPI v1.6 协议升级与握手诊断

### 56.1 背景

- 按《01-GUI通信接口清单 v1.6》升级主控端 G4 SPI2 ↔ G0B1 GUI 通信接口层，保留现有运动/贴装业务逻辑。
- 原 `Task/app_gui_spi.c/h` 为 SPI v1.5 风格：PD8 曾作 INT、使用 `HAL_SPI_Receive`、≤128 字节、首个 0x00 停止解析。

### 56.2 硬件与协议变化

- 引脚：SPI2 PB13(SCK)/PB14(MISO)/PB15(MOSI)，CS=PD10；DATA_RDY=PD8（主控→GUI，低有效）；REQ_TX=PD9（GUI→主控，低有效）；IRQ=PB12（当前恒高，禁止业务响应）。
- SPI2 Mode0、8-bit MSB、10.625MHz；固定 128 字节/事务；命令以 `\n` 结束，之后 `0x00` 填充。
- 发送：DATA_RDY 低 → CS 低 → ≥1µs → 128B → CS 高 → DATA_RDY 高 → ≥100µs。
- 接收：≤10ms 轮询 REQ_TX；低时拉低 CS 并产生 128 字节时钟读 MISO；REQ_TX 仍低则继续读，变高表示队列空。
- 握手：GUI 发 `HANDSHAKE_REQ\n`（1s 重试，最多 10 次），主控解析后立即回 `HANDSHAKE_ACK\n`，重复回 ACK 无害。
- 未识别命令静默忽略；LOG 限速 ≤20 条/秒；温度仍约 1s 一次。

### 56.3 代码变更

| 文件 | 变更 |
|------|------|
| `Task/app_gui_spi.c/h` | 新增 `GUI_Send()` / `GUI_Poll()`；固定 128 字节帧；`HAL_SPI_TransmitReceive`；REQ_TX 低有效轮询；`HANDSHAKE_REQ` 即时回 ACK；每帧解析、未知命令忽略；LOG 20 条/秒限速；`GUI_SPI_Task` 按 ≤10ms 调用 `GUI_Poll()` |
| `Task/app_uart_parser.c/h` | `parse_cmd()` 按首个空格或冒号切分；新增 `HCMD_HANDSHAKE_REQ` |
| `Task/app_host.c` | 未改业务逻辑；现有 `gui_process_cmd` 继续消费 GUI 命令 |

### 56.4 握手诊断日志

- `[GUI] SPI task started, polling REQ_TX`：任务已启动。
- `[GUI] REQ_TX LOW/HIGH`：请求线状态变化。
- `[GUI] RX8: ...`：每次读取后的前 8 字节原始数据。
- `[GUI] RX parse fail...`：未找到 `\n` 或未知命令；前 3 次必打，之后每 100 次打一次。
- `[GUI] RX cmd=... raw=...`：解析成功。
- `[GUI] HANDSHAKE_REQ received, ACK send called`：进入握手应答分支。
- `[GUI] SPI2 TX/RX err=...`：HAL 收发错误。
- `[GUI] REQ_TX released before SPI read`：REQ_TX 在读事务前已变高。

### 56.5 实机诊断

- 日志表现：REQ_TX 持续低，主控连续读约 33 秒，`RX8` 全 0，`bad=33300`。
- 结论：主控读时序正常，但 G0B1/MISO 数据链路未返回有效数据；不是解析或 ACK 逻辑问题。
- 排查方向：PB14 ↔ G0B1 SPI 从机数据输出接线、共地、G0B1 SPI1 从机初始化/Mode0、逻辑分析仪观察 CS/SCK/MISO、PB14 临时上拉区分悬空。
- 当前状态：待硬件/GUI 固件侧确认。

### 56.6 验证

- [x] ARMCLANG `-fsyntax-only` 通过：`app_uart_parser.c`、`app_gui_spi.c`、`app_host.c`、`app_freertos.c`。
- 本次发现 `E:\Keil_v5\ARM\ARMCLANG\Bin\armclang.exe` 可用；§54.7 中“本机无 ARMCLANG”的描述已不适用。
- 完整 Keil 链接未运行。

### 56.7 文档同步

- `AGENTS.md`：硬件表、§4.5、§10.1 GPIO 速查更新为 v1.6。
- `HISTORY.md`：§49 增加 v1.6 校正；追加本节。

## 五十七、2026-08-06 会话 — ESP32 SPI v3.1 代码复核与修复

### 57.1 背景

- 对照 `E:/聊天记录/通讯接口(ESP与主控) (2).md`（v3.1）复核主控端 ESP32 SPI 实现，发现 CSV 上传主链路、WiFi 凭据透传和状态上报存在缺陷。
- 本轮只修改主控端，不改 ESP32 端、运动控制、视觉识别等其他模块。

### 57.2 修复项

| 项 | 修复 |
|------|------|
| CSV START/END 解析 | `ESP_ParseCsvStart/End` 返回 `1=成功`，原 `!= 0` 误判合法帧为失败；改为 `== 0` 判断失败 |
| WiFi SSID/密码校验 | 取消 SSID 仅数字限制；SSID 1~32 字节、密码 8~63 字节，仅拒绝控制字符、逗号、CR/LF |
| WiFi 周期上报 | `WIFI_CONNECT (0x20/0x03)` 发送成功后置 `g_esp_wifi_enabled=1` |
| 完成状态 | 新增 `Host_IsSmtFinished()`，`0x10/0x02` 在 `HOST_DONE` 时上报 `Finished` |
| CSV 会话超时 | 文件会话空闲 5 秒自动复位并回 `fail:4` |
| 场景 A MISO | 新增 `_handle_esp_rx()`，场景 A/B 共用 ESP→STM32 命令分发 |
| PnP 日志 | 补充 `mark点识别失败`、`达到预定温度`、`结束` |
| 重复开始日志 | `Host_FinishCsvImport()` 不再提前写 `LOG_PNP_START`，避免与 `download_done()` 重复 |

### 57.3 决策与限制

- **`0x71` 回执时序：** 文档写“同帧 MOSI 回执”，当前主控采用“读帧后下一事务回执”。核对 `SPI_web_test_1.ino` 后确认 ESP32 端事务完成后拉高 IRQ，再接收下一轮 `0x71`，两事务流程与现有 ESP32 实现兼容，因此未强行改为同帧。
- **UTF-16 CSV：** 帧级接收和 CRC 按原始字节处理，但主控 `parse_csv_line()` 仍按 UTF-8 文本解析，UTF-16/含 `0x00` 文件尚不能可靠导入，待后续按原始字节/UTF-16 改造解析器。
- **队列：** 日志和 WiFi 凭据队列仍为非阻塞入队，满时可能静默丢弃。
- **CubeMX 属性：** `pnp_1.ioc` 尚未补 PC13 上拉与 PE3 高速输出属性，重新生成可能丢失手改配置。

### 57.4 验证

- [x] ARM GCC 14.3.1 `-fsyntax-only` 通过。
- [x] Ninja 重新编译变更目标文件通过。
- [x] 手动 ARM ELF 链接通过：RAM 66744B / 128KB，FLASH 192472B / 512KB。
- [ ] Keil UV4 全量编译待用户环境验证。
- [ ] ESP32 端真机联调待硬件确认。

### 57.5 文档同步

- `AGENTS.md`：§4.5 WIFI_CONNECT 校验描述更新；§4.6 补充 CSV 超时、Finished、WiFi 启用状态、场景 A 分发、已知限制；参考文档改为 `(2).md`。
- `HISTORY.md`：追加本节；§55 为首次实现记录，复核与修复以 §57 为准。

## 五十八、2026-08-12 会话 — P1 批量目标 >6 丢失修复 + P4 补偿速度与轴映射核对

### 58.1 背景与现象

- 用户将 `P1_MAX_TARGETS` 从 10 改为 32 后，P1 批量识别仍只在视野内目标数 ≤6 时不重复开摄像头；目标数多于 6 时，放完队列后仍会重新 `Vision_Start(P1)`。
- 现象不是“第 11 个之后丢失”，而是 **第 7 个目标开始丢失**，因此队列始终只剩 6 个。

### 58.2 根因

- `Task/app_vision.c` `process_p1_frame()` 在收到第 N 个目标的 `ao` 后立即将 `g_p1_class_done=true`，等待最后类别号。
- 相机发送顺序是 `N1:dx N1:dy N1:ao ... N7:dx N7:dy N7:ao N:<class_id>`。
- 当目标数 >6 时，第 7 个目标的 `N7:dx` 被旧逻辑当成 `N:<class_id>`，`class_id` 被错误覆盖；第 7 个目标及后续目标不再解析。
- `p1_queue_fill()` 只能入队已解析的 6 个目标，放完队列后 `find_comp_step()` 重新开摄像头识别。

### 58.3 修复

| 文件 | 改动 |
|------|------|
| `Task/app_vision.c` | `g_p1_class_done` 分支增加帧格式校验：只接受 `N:<class_id>`（`str[0]=='N' && str[1]==':'`）；`N7:dx` 等带序号目标帧不再误作类别号 |
| `Task/app_vision.h` | 注释从 64 改回 32；`P1_MAX_TARGETS=32` |

- 行为结果：目标数 7~32 都能进入同一批次队列，队列耗尽或类别变化后才重新识别。
- 相机端 `MODEL_CONFIG["max_det"]` 当前参考配置仍是 10，需同步调大才能真正接收超过 10 个目标。

### 58.4 P4 排查与工作区改动

- 排查 P4 吸嘴不移动：P4 基线/校验的短距补偿原使用通用 `PNP_SPEED/PNP_ACC`（当前 400/40），与 P1/P3 视觉微调不一致。
- 工作区已把两处补偿改为 `PNP_SPEED_FINE/PNP_ACC_FINE`（100/10），并加入诊断日志：
  - `[HOST] P4 baseline/verify pos: cam=(...) move=(...) at=(...)`
  - `[HOST] P4 baseline/verify compensate ret=...`
- 轴映射核对：`dx_s=+r->dy`（cam Y → X1+X2）、`dy_s=-r->dx`（cam X → Y 电机取反），与 P3 及 AGENTS §4.2.4 一致。
- 状态：**待真机验证**，未确认“吸嘴不动”问题已彻底解决。

### 58.5 其他当前工作区/暂存区变更记录

- `app_host.h`：`PNP_SPEED 300→400`，`PNP_ACC 25→40`。
- `app_host.c`：`footprint_to_class_id()` 增加 `R0*→cres(2)`；`component_cell()` 独立映射 `C0*→0/R0*→1/LED*→3`；`Host_FinishCsvImport()` 直接调用 `download_done()`。
- `app_esp_task.c`：CSV `ok` 仅在发送失败时才进入重试待发状态。
- 编译验证：Keil UV4 `0 Error(s), 1 Warning(s)`，warning 为 `driver_tmc2209.c` 空循环体，与本次改动无关。

### 58.6 文档同步

- `AGENTS.md`：P1 批量上限统一为 32；补充类别号帧格式约束；P1/P4 关键常量与补偿速度同步。
- `HISTORY.md`：追加本节；§54.3 中 `P1_MAX_TARGETS=10` 为历史值，当前以本节/AGENTS.md 的 32 为准。

## 五十九、2026-08-12 会话 — GUI v1.7 日志/进度联调准备

### 59.1 背景

- 屏幕 GUI 的 Log 功能不打印日志，且贴片进度是否可持续推进（如 `1/28 → 2/28`）需要确认。
- 本仓库没有 G0B1 GUI 源码，GUI 固件由另一位负责；本次只能完成主控侧协议实现与诊断准备。
- 接口文档更新为 v1.7：SPI2 时钟上限收紧到 ≤8MHz，建议约 5MHz；实测 10.625MHz 下 GUI 中断收发会偶发丢字节。GUI 从机 SPI1 中断使能修复为从机内部修复，主控无需修改。

### 59.2 主控修改

| 文件 | 改动 |
|------|------|
| `Task/app_gui_spi.h/c` | 新增 `GUI_LogMsg_t`、`gui_log_queue`、`g_gui_handshake_done`、`GUI_SPI_LogProcess()`；`GUI_SPI_NotifyLog()` 改为入队，Host_Task 周期发送 |
| `Core/Src/app_freertos.c` | 创建 `gui_log_queue`（32×GUI_LogMsg_t） |
| `Task/app_host.c` | 主循环调用 `GUI_SPI_LogProcess()`；关键 PnP 节点同步 `LOG:`；`place_step()`、P1 异常跳过、PCONTINUE 恢复时补发 `SMT_PROGRESS` |

- GUI LOG 同步消息：`PNP_START`、`MARK_SCAN`、`COMP_SCAN`、`MARK_DONE`、`NOZZLE_ALIGN`、`PICK_FAIL`、`ALIGN_FAIL`、`PLACE_START`、`PLACE_DONE`。
- 进度更新路径：下载完成 `0/N`；P3 校正完成 `(i+1)/N`；每个元件贴装完成后 `(i+1)/N`；全部完成 `N/N`。
- 握手状态仅用于链路诊断，不再作为 LOG 发送门控；主控始终尝试发送 `LOG:` 与 `SMT_PROGRESS:`。

### 59.3 当前速率与代码状态

- `Core/Src/spi.c` 与 `pnp_1.ioc` 当前为 `SPI_BAUDRATEPRESCALER_32`，170MHz/32 ≈ 5.3125MHz，符合 v1.7 建议。
- `app_gui_spi.h/c` 中残留的 `v1.6`/10.625MHz 注释已同步为 v1.7/5.3125MHz。

### 59.4 验证

- [x] ARMCLANG `-fsyntax-only` 通过：`app_gui_spi.c`、`app_freertos.c`、`app_host.c`。
- [ ] GUI Log 页面显示与 `SMT_PROGRESS` 推进待 G0B1 真机联调确认。

### 59.5 待 GUI 端确认

- GUI SPI1 从机是否解析 `LOG:<message>` 并写入 Log 页面。
- GUI 是否解析 `SMT_PROGRESS:<current>,<total>` 并刷新进度条/文本。
- GUI 固件是否已刷入 v1.7 对应版本（SPI1 中断使能修复）。
- 若 MOSI 有完整 128 字节 `LOG:` 帧但页面不显示，问题在 GUI 固件接收/页面刷新，不在主控。

### 59.6 文档同步

- `AGENTS.md`：§4.5 更新为 v1.7；硬件表 SPI2 速率同步；补充日志队列、进度更新与 GUI 端待确认项。
- `HISTORY.md`：追加本章节；§56 保留为 v1.6 历史记录。

## 六十、2026-08-12 会话 — PNPSTOP/PCONTINUE 断点恢复 + P3 空吸嘴重吸取

### 60.1 背景

- 新增 `PNPSTOP` / `PCONTINUE` 断点恢复。用户实测 `PNPSTOP` 能停止电机，但发送 `PCONTINUE` 后没有反应，不能按暂停状态继续。
- 排查方向：命令解析、UART 接收/清空、恢复上下文、坐标恢复、状态机恢复入口。

### 60.2 根因

- `Host_Task` 旧逻辑处理完当前 UART 批次后无条件调用 `UART_ClearData()`。`PNPSTOP` 处理耗时较长，期间到达的 `PCONTINUE` 其 ISR `data_ready` 会被一起清掉，固件根本没进入恢复流程。
- `g_resume_ctx` 原只覆盖 `HOST_FIND_COMP` ~ `HOST_PLACE`，P2 Mark 对位、P4 基线/校验阶段触发 `PNPSTOP` 时 `step_id=RESUME_STEP_NONE`，`PCONTINUE` 被当作上下文不匹配忽略。
- 恢复时保存坐标与当前逻辑坐标通常相等，但旧实现仍调用绝对位置移动；零位移命令可能因 CAN 到位响应不确定而超时，表现为恢复卡住/无响应。
- 真机日志 `ctx_step=0 task=0/2` 进一步定位：`pnp_halt()` 在 31H 编码器同步失败时旧实现直接返回，`g_resume_ctx` 完全未保存，导致 `PCONTINUE` 必然报 `task/ctx mismatch`。

### 60.3 修改

| 文件 | 改动 |
|------|------|
| `Task/app_uart_parser.c/h` | 新增 `HCMD_PNPSTOP`、`HCMD_PCONTINUE`；`PCONITINUE` 作为兼容别名 |
| `Drivers/ZeMCU-G4/driver_motor.c/h` | 新增 `motorEmergencyHold()`：X1/X2/Y 发 `0xF7` 急停 + `0xF3` 保持使能，ISR 可调用 |
| `Drivers/ZeMCU-G4/driver_uart.c/h` | 新增 `UART_ClearAppData()`：只清已拷贝的旧应用缓冲，不清 ISR `data_ready` |
| `Task/app_motion.c/h` | 新增 `g_system_halted`、`coord_sync_from_encoders()`、`motor_move_absolute()`；`motion_wait_done()` 急停返回 `-3` 且不失效坐标；零位移恢复直接成功；编码器同步超时 100→250ms；TMC R 轴与 `pick_component()` 增加急停检查 |
| `Task/app_host.c/h` | 新增 `ResumeContext_t`、`HOST_REPICK`、`pnp_halt()`、`pnp_continue()`；UART 回调扫描 `PNPSTOP` 并立即保持电机；补齐 P2/P4/P1/PICK/P3/MOVE_TO_PCB/PLACE 恢复；P3 `err3_8` 改为同点重吸流程；局部 phase 改为文件级便于恢复复位 |

- `g_resume_ctx` 新增 `coord_synced`：PNPSTOP 编码器同步失败也保存逻辑坐标，非 `home_done` 路径 PCONTINUE 先重试编码器同步；重试仍失败时按逻辑坐标继续并告警。
- 新增 `motion_flush_after_halt()`：PCONTINUE 恢复运动前排空 CAN 残留帧、清零 `g_axes_done_bits/g_axes_error` 并重新开启同步，避免旧 0x02 假到位导致“日志显示运动完成但实际未运动”。
- PNPSTOP 全模块关断：气泵、补光灯、电磁阀、加热台、DRV8803 两片芯片输出、TMC R 轴使能均关闭，XY 电机保持使能锁定位置；PCONTINUE 恢复时重新使能 DRV8803、舵机供电与 TMC，并按步骤继续。
- 新增 `UART_TX_Reset()`：PCONTINUE 入口先复位 UART1 TX DMA/`tx_pending` 并继续发送环形缓冲，解决急停后串口日志不再打印的问题。
- PNPSTOP 增加回原点步骤：保存断点后临时解除冻结，回 `(0,0)`，再恢复 halted 并关断模块；`g_resume_ctx.home_done=true`，PCONTINUE 从原点恢复到保存坐标，不再误判坐标偏差。

### 60.4 P3 err3_8 当前行为

- 第一次 `err3_8`：返回保存的取料点 `g_last_pick_x/y_steps`，重新 `pick_component()`，再移动到下相机并重启 P3。
- 第二次 `err3_8`：清空 P1 批量队列，强制重新打开上位相机 P1 识别，禁止依赖上次记忆结果直接贴装。
- 第三次 `err3_8`：进入 `HOST_ERROR`。
- 每次重吸后都必须重新经过下相机验证。

### 60.5 验证

- [x] ARM GCC 14.3.1 `-fsyntax-only` 通过：`app_uart_parser.c`、`driver_motor.c`、`driver_uart.c`、`app_motion.c`、`app_host.c`。
- [x] `git -c core.whitespace=cr-at-eol diff --check` 通过。
- [ ] Keil UV4 完整编译待用户环境验证。
- [ ] PNPSTOP → PCONTINUE 真机断点恢复待联调确认。

### 60.6 文档同步

- `AGENTS.md`：§4.1 命令列表新增 `PNPSTOP`/`PCONTINUE`；新增 §4.1.1 断点恢复；§4.2 P3 更新 `err3_8` 重吸取策略；§9.8 补充 UART 回调与 `UART_ClearAppData` 当前说明。
- `HISTORY.md`：追加本章节；§26 保留为历史行为记录，当前 P3 空吸嘴策略以本章节和 AGENTS.md 为准。

### 60.7 回原点后 PCONTINUE 过冲/堵转修复（2026-08-12 续）

**现象：** PNPSTOP 回原点后发送 PCONTINUE，恢复运动日志为 `[POLL] timeout: bits=0x03 need=0x07 waited=2000ms`，随后仍返回 `PCONTINUE_OK` 并继续；吸嘴没有恢复到断点位置，下一步移动过冲并堵转。

**根因：** `pnp_continue()` 使用 `motor_move_absolute()` 恢复保存坐标，内部走 MKS `0xF5` 绝对坐标，且 `move_to()` 固定等待 `ACK_TIMEOUT_MS=2000`。回原点后需要恢复约 9.5 万步，`PNP_SPEED_FINE=100RPM` 无法在 2s 内到位；旧实现收到 `ret=-1` 后仍“容错继续”，物理位置未恢复就进入 REPICK/后续步骤。

**修复：**

- `home_done=true` 时不再重读编码器，校验逻辑原点在容差内后，用 `safe_move_to()` 按 `0xF4` 相对坐标从 `(0,0)` 恢复到保存坐标；相对运动使用动态到位超时，与 PnP 自动流程一致。
- 恢复运动失败不再容错继续，返回 `PCONTINUE_FAIL` 并进入 `HOST_ERROR`。
- `g_resume_ctx.home_done` 仅在回原点 `safe_move_to(0,0)` 成功时置位，回原点失败时 PCONTINUE 走坐标校验/错误路径。

**涉及文件：** `Task/app_host.c`。

**验证：** ARM GCC 14.3.1 `-fsyntax-only` 通过 `Task/app_host.c`；真机断点恢复需用户确认。

### 60.8 PCONTINUE 在 PCB 上重吸修复（2026-08-12 续）

**现象：** PNPSTOP/PCONTINUE 恢复后，固件记录的“散料区取料点”实际是 PCB 贴装坐标；继续运行会在 PCB 上执行吸取，而不是回到散料区。

**根因：** PNPSTOP 打断 `p1_start_pick_from_queue()` 中从当前位置移动至散料区取料点的 `safe_move_to()` 时，`safe_move_to()` 返回 `-3`，旧代码没有检查返回值，仍把 `Coord_Get()` 的当前坐标（常为 PCB 附近）写入 `g_last_pick_*`，并推进 `g_state=HOST_PICK`。`pnp_halt()` 随后保存 `RESUME_STEP_PICK`，PCONTINUE 便从保存坐标回到 PCB 附近的“重吸取点”。`p1_align_finish()` 存在同类问题。

**修复：**

- `p1_start_pick_from_queue()` 检查 `safe_move_to()` 返回值；被 PNPSTOP 打断时保持 `HOST_FIND_COMP` 并返回，不再写 `g_last_pick_*`、不再推进到 `HOST_PICK`。
- 取料点改为直接保存 P1 队列坐标 `q->x_steps/q->y_steps`，不再依赖移动完成后的 `Coord_Get()` 快照。
- `p1_align_finish()` 先计算取料点再移动，移动被打断时同样保持 `HOST_FIND_COMP`；成功后才保存取料点并进入 `HOST_PICK`。
- P1 迭代修正移动也检查返回值，避免失败后继续强制完成识别。

**涉及文件：** `Task/app_host.c`。

**验证：** ARM GCC 14.3.1 `-fsyntax-only` 通过 `Task/app_host.c`；真机 PNPSTOP 打断 P1 去散料区移动后再 PCONTINUE 需用户确认。
