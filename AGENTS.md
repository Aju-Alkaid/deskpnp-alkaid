# PnP 贴片机嵌入式固件 — 项目说明书

## 一、项目概览

桌面级贴片机，主控 STM32G474VETx（170MHz Cortex-M4F），基于 STM32CubeMX 生成 HAL 库工程，FreeRTOS 多任务调度。

功能：接收上位机坐标文件 → 双目视觉定位元件 → CAN 总线控制三轴运动 → 吸嘴拾取/放置 → 加热台控温。

**团队分工：** 本仓库为嵌入式固件（C 语言），视觉/硬件/GUI（TouchGFX）由其他人负责。

## 二、硬件平台

| 资源 | 详情 |
|------|------|
| MCU | STM32G474VETx, HSE 16MHz → PLL 170MHz |
| 调试接口 | SWD (NRST=PG10) |
| 串口1 (USART1) | PE0(TX) / PE1(RX), 115200, DMA, 连接上位机 |
| 串口2 (USART2) | PD5(TX) / PD6(RX), 115200, DMA, 连接 MaixCam 摄像头 |
| 串口3 (USART3) | PB9(TX) / PB11(RX), 115200, DMA, 连接 TMC2209(R轴) |
| LPUART1 | PC1(TX) / PC0(RX), 115200, 半双工, 预留 |
| CAN (FDCAN1) | PA12(TX) / PA11(RX), 1Mbps, 连接 3 台 MKS SERVO42D 总线伺服电机 (ID=0x01 X1, ID=0x02 X2, ID=0x03 Y) |
| SPI2 | PB13(SCK) / PB15(MOSI), CS=PD10, DC/RS=PD9, RST=PD8, 连接 LCD(ST7306) |
| SPI3 | PC10(SCK) / PC11(MISO) / PC12(MOSI), CS=PA15, 连接 W25Q64 Flash |
| SPI4 | PE2(SCK) / PE5(MISO) / PE6(MOSI), CS=PE3, RST=PC13, 连接 ESP32 通信模块 |
| TIM2 | CH1(PA0) 12V_C1 PWM / CH3(PB10) Z轴舵机 PWM (50Hz) / 32位时间戳基准 |
| TIM5 | CH1(PB2) 24V_C1 PWM / CH3(PE8) 12V_C2 PWM (50Hz) |
| TIM6 | HAL 系统时基 |
| CRC | 硬件 CRC 校验 |
| GPIO 按键 | KEY1(PC6) / KEY2(PC7) / CW(PA8) / CCW(PC8) / PUSH(PC9), 低电平有效 |
| DRV8803×2 | U12(12V 驱动): PE9(EN)/PE10(RST)/PE15(FAULT) ; 输出端口见 §10.4 |
|  | U13(24V 驱动): PA4(EN)/PB0(RST)/PA6(IN5)/PA7(IN6)/PC4(IN7)/PC5(IN8)/PA5(FAULT) |
| TMC2209 | UART3 通信, PD15(TMC1_EN) / PD14(TMC2_EN 预留) |
| 加热台 | CAN ID 0x10(命令) / 0x11(状态), 独立控制 |
| 温度传感器 | PF9 / PA3, DS18B20 |
| 舵机(Z轴) | PB10, TIM2_CH3, MG995 (50Hz PWM) |
| 吸嘴气泵 | PE11 (12VO1, DRV8803 U12 OUT1 开关) |
| 电磁阀 | PA6 (24VO1, DRV8803 U13 OUT5 低端开关, PA6=LOW时导通) |
| BOOT0 | PB8, 启动选择 |
| LCD_LED | PD8, LCD 背光 |
## 三、目录结构

```
pnp_1/
├── Core/                        # CubeMX 生成（修改后重新生成会覆盖！）
│   ├── Inc/                     # main.h, usart.h, gpio.h, tim.h, spi.h, fdcan.h...
│   └── Src/                     # main.c, usart.c, stm32g4xx_it.c, app_freertos.c...
├── Drivers/
│   ├── STM32G4xx_HAL_Driver/    # HAL 库（禁止修改）
│   ├── CMSIS/                   # CMSIS 核心（禁止修改）
│   └── ZeMCU-G4/                # ★ 自定义驱动层 ★
│       ├── driver_uart.c/h      # UART DMA+空闲中断 4通道驱动（UART_CH1~4）
│       ├── driver_can.c/h       # FDCAN 收发 + 滤波器 + CRC_SUM8 + 中断
│       ├── driver_motor.c/h     # MKS 伺服电机 CAN 控制（0xF5/0xF3/0x82/0x92/0x4A/0x4B 等）
│       ├── driver_tmc2209.c/h   # TMC2209 UART 寄存器读写 (R轴)
│       ├── driver_servo.c/h     # MG995 舵机 PWM 控制（TIM5_CH3 / PE8）
│       ├── driver_drv8803.c/h   # DRV8803 双芯片 8通道驱动（12V+24V）
│       ├── driver_heater.c/h    # 加热台 CAN 通信 (CAN ID 0x10/0x11)
│       ├── driver_timer.c/h     # 定时器工具
│       ├── driver_spiflash_w25q64.c/h  # SPI Flash (W25Q64)
│       ├── tmc_protocol.c/h     # TMC2209 协议层
│       ├── pid.c/h              # 通用 PID 控制器（位置/速度模式）
│       ├── motor.c/h            # 32步进电机控制 (TMC2209+PID)
│       ├── ringbuf.c/h          # 环形缓冲区 (CAM_RING=1024, HOST_RING=4096)
│       ├── key.c/h              # 5键扫描（20ms 消抖 + 事件型）
│       ├── timestamp.c/h        # TIM2 32位时间戳，overflow_count 全局溢出计数
│       ├── app_motor.h          # 电机应用层头文件（占位）
│       └── driver_CH340.c/h     # 串口文件转存（CH340 USB转串口，未实现）
├── Task/                        # ★ FreeRTOS 应用层任务 ★
│   ├── app_host.c/h             # 上位机通信任务 + CSV解析 + 视觉协调 + 调试模式
│   ├── app_uart_parser.c/h      # 上位机行协议解析器（COMMAND arg\n 格式）
│   ├── app_vision.c/h           # 摄像头 0x7E/0x7F 协议解析（process1/2/3）
│   ├── app_motion.c/h           # 运动控制函数 + CAN_Process_Task + MotionTask_Func
│   ├── app_test.c/h             # 测试任务 (vMotorTestTask) + PrintDebug 函数
│   └── Task_Init.c/h            # 任务创建框架（Tasks_Create，当前未激活）
├── TouchGFX/                    # GUI 图形界面（已移植，FreeRTOS 任务驱动）
├── Middlewares/                  # FreeRTOS + TouchGFX 中间件（系统生成，禁止修改）
├── MDK-ARM/                     # Keil MDK 工程文件
├── build/                       # CMake 构建输出
├── CMakeLists.txt               # CMake 构建配置
├── pnp_1.ioc                    # CubeMX 工程文件
└── STM32G474XX_FLASH.ld         # 链接脚本
```

## 四、通信协议

### 4.1 上位机 ? G4 (USART1, PE0/PE1)
- **物理层：** 115200, 8N1, DMA+空闲中断
- **协议格式：** 行文本协议，`COMMAND arg\n`
- **命令列表：**
  - `MOVE_UP/MOVE_DOWN/MOVE_LEFT/MOVE_RIGHT [步长mm]` — 调试单步移动
  - `MOVE_UP_START/MOVE_DOWN_START/MOVE_LEFT_START/MOVE_RIGHT_START [速度]` — 调试连续移动(开始)
  - `MOVE_STOP` — 停止连续移动
  - `SET_ORIGIN` — 设置当前点为原点
  - `EXIT_DEBUG_MODE` — 退出调试模式
- **文件下载流程：**
  1. G4 发送 `DOWNLOAD_READY\n` 给上位机
  2. 上位机逐行发送 CSV 数据（每行以 `\n` 结尾）
  3. 首行作为表头解析（识别 X/Y/Rotation/SMD 列）
  4. 300ms 超时无新行 → 下载完成，自动进入 Mark 点对齐流程
  5. CSV 格式：`ID,Name,X(mm),Y(mm),Rotation(deg),SMD`
- **无校验：** 纯文本协议，依赖 UART 硬件可靠性

### 4.2 G4 ? MaixCam 摄像头 (USART2, PD5/PD6)
- **物理层：** 115200, 8N1, DMA+空闲中断
- **协议格式：** `0x7E ... 0x7F` 帧定界，帧内为 UTF-8 字符串字段（按 0x7F 分隔）
- **帧结构：** `7E begin 7F field1 7F field2 7F ... 7F end 7F`
- **命令（G4→摄像头）：**
  - `process1` — 散料区检测元件
  - `process2` — Mark 点检测
  - `process3` — 元件偏移检测
- **返回格式（摄像头→G4）：**
  - process1 成功：`begin, x_offset, y_offset, comp_info, end`
  - process2 成功：`begin, mark1_x, mark1_y, mark2_x, mark2_y, end`
  - process3 成功：`begin, x_offset, y_offset, end`
  - 失败：包含 `err1`/`err2`/`err3` 字段

### 4.3 G4 ? MKS SERVO42D 电机 (CAN, FDCAN1)
- **物理层：** CAN 2.0A, 1Mbps, 标准帧(11位ID)
- **校验：** SUM8 CRC（ID+数据字节累加取低8位）
- **数据长度限制：** ≤7 字节有效数据 + 1 字节 CRC = 最多8字节
- **电机 ID：** X1=0x01, X2=0x02, Y=0x03, 广播=0x00
- **主要功能码：**
  - `0xF5` 坐标绝对运动（速度=2B+加速度=1B+坐标=3B，共7字节+CRC）
  - `0xF3` 使能/去使能
  - `0x82` 设置工作模式（SR_vFOC=0x05）
  - `0x92` 设为零点
  - `0x4A` 同步标志开关
  - `0x4B` 同步执行触发
  - `0x95` 设置到位阈值
- **状态码（电机→G4）：**
  - `0x01` 运行中
  - `0x02` 运行完成（到位）
  - `0x03` 限位停止/堵转

## 五、任务架构

| 任务 | 栈大小 | 优先级 | 功能 |
|------|--------|--------|------|
| `Host_Task` | 1024 | Normal | ★ 主任务：上位机通信 + 调试命令 + CSV解析 + 视觉协调 + PnP流程 |
| `CAN_Process_Task` | 512 | Normal | 从 motor_event_queue 取 CAN 报文，设事件组标志 |
| `vMotorTestTask` | 1024 | Normal | MKS 电机测试任务（已注释） |
| `TouchGFX_Task` | 8192 | Normal | GUI 图形界面渲染 + VSYNC + 按键处理 |
| `Key_Task` | 256 | Normal | 硬件按键扫描（10ms）+ 消抖 → keyEventQueue |
| `ESP_Task` | 512 | Normal | ESP32 通信 + WiFi 状态管理 |
| `PnP_Motion_Task` | 1024 | Normal | 备用运动任务（已注释，未激活） |
| `StartHostMotionTestTask` | 4096 | Normal | 调试用运动任务（已注释，功能合并到 Host_Task） |
| `StartPickPlaceTestTask` | 2048 | Normal | Pick&Place 测试（已注释，功能合并到 Host_Task） |
| `StartMotorTestTask` | 1024 | Normal | TMC2209 测试（已注释） |

**任务间通信：**
- `motor_event_queue` (32深度) — CAN 中断 → CAN_Process_Task / vMotorTestTask
- `motion_cmd_queue` (20深度) — Host_Task → MotionTask_Func
- `host_pkt_queue` (64深) — 已弃用，Host_Task 改用 UART_PeekData 直接读取
- `evtAxesDone` 事件组 — CAN_Process_Task 通知到位
- `keyEventQueue` (16深) — Key_Task → KeyController → TouchGFX 按键事件
- `dataTransferQueue` (16深) — 主系统 Task → Model::processQueue() → UI 数据同步
- `vsync_queue` (1深) — TIM7 ISR → TouchGFX 渲染循环
- `frame_buffer_sem` — TouchGFX 帧缓冲互斥锁
- `semX1Done/semX2Done/semYDone` 信号量 — 三轴独立到位信号

## 六、关键数据流

```
上位机 --[USART1]--> DMA+空闲中断 → data_ready=true
                          │
            Host_Task 主循环 (~100Hz):
              UART_Driver_Process()  → DMA→app缓冲
              UART_PeekData()        → 读app缓冲（任务上下文）
              LineParser_Feed()      → 行解析（任务上下文，FPU安全）
                ├─ 运动/调试命令 → handle_debug_cmd()
                ├─ RAW_LINE(DEBUG/INIT) → 切换 HOST_DOWNLOADING
                └─ RAW_LINE(DOWNLOADING) → 解析CSV → g_components[]
                                             │   Vision_SendCmd(process3) → 偏移修正
                                             │   motion_cmd_queue → MotionTask_Func
                                             │       │
                                             │   CAN → 电机运动到位
                                             │   Z轴舵机 拾取/放置
                                             │
                                             └── 完成 → host_send("DOWNLOAD_COMPLETE")
```

## 七、关键数据结构

| 结构体 | 所在文件 | 用途 | 关键字段 |
|--------|----------|------|----------|
| `Component_t` | app_host.h | 贴装元件信息 | id, target_x/y/angle, feeder_id, placed |
| `HostParsed_t` | app_uart_parser.h | 上位机行解析结果 | cmd, param(float), raw[512] |
| `CamData_t` | app_vision.h | 摄像头返回数据 | result, x/y_offset, mark1/2_x/y |
| `MotionCmd_t` | app_motion.h | 运动指令 | cmd_type, target_x/y/r, speed, acc |
| `CAN_Rx_Packet_t` | driver_can.h | CAN 数据包 | ID, FuncCode, Status, Data[8], Timestamp |
| `RingBuf_t` | ringbuf.h | 环形缓冲区 | buffer, size, head(写)/tail(读) |
| `LineParser_t` | app_uart_parser.h | 行解析器状态机 | buf[512], idx, complete |
| `UART_Channel_t` | driver_uart.c(内部) | UART 通道控制块 | huart/hdmarx, 双缓冲, data_ready/is_rx_active/overflow_count |
| `MotorState_t` | driver_motor.h | 电机状态枚举 | IDLE/SENDING/WAITING/COMPLETE/ERROR |

## 八、编码规范与约束

1. **文件编码：** 所有 `.c/.h` 文件使用 **GBK** 编码。读写时必须指定 `[Text.Encoding]::GetEncoding(''GBK'')`
2. **CubeMX 用户代码区：** 自定义代码只能写在 `USER CODE BEGIN/END` 标记之间，否则重新生成时会被覆盖
3. **禁止批量删除：** 禁止 `del /s`, `rd /s`, `Remove-Item -Recurse` 等批量删除命令
4. **中文注释：** 项目标准使用中文注释
5. **修改审批：** AI 提出问题/建议 → 用户审核 → 批准后修改
6. **中断安全：** ISR 内禁止阻塞调用（如 PrintDebug 中的 `HAL_UART_Transmit`），禁止 `osDelay`
7. **栈溢出防护：** 每个任务栈大小已在 app_freertos.c 定义，增加 printf 类函数需增大栈（至少 512）

## 九、已知问题与注意事项

### 9.1 严重问题（需关注）
1. 暂无

### 9.2 警告问题（建议修复）
2. **CAN ISR 中调用 PrintDebug（已修复，见 §9.6-2）：** `HAL_FDCAN_RxFifo0Callback` 中 PrintDebug 调用已用 `#ifdef DEBUG_CAN_ISR` 包裹。
3. **`CAN_Transmit_Data` 中调试打印（已修复，见 §9.6-2）：** 每次 CAN 发送的 TX 日志同样用 `#ifdef DEBUG_CAN_ISR` 包裹。
4. **`motor_send_move_cmd` 函数体冗余：** 该函数的 buffer 填充逻辑与 `positionMode3Run` 重复，实际调用也是转发到后者。建议移除冗余逻辑或直接废弃此函数。

### 9.3 功能性问题（待完善）
5. **正式运动任务（已解决，见 §9.8）：** `PnP_Motion_Task` 已由 `Host_Task` 取代，`Host_Task` 统一处理调试命令和 PnP 流程。
6. **MOTION_CMD_PICK/PLACE 缺少 XY 移动到吸嘴/贴装位置：** `pick_component()` 和 `place_component()` 直接操作 Z 轴舵机，但调用前需要上层先发送 `MOTION_CMD_MOVE_TO` 到达目标位置。
7. **连续移动（已解决，见 §9.8）：** `Host_Task` 的 `handle_debug_cmd` 已实现完整的 JOG 控制（同步模式+positionMode3Run+motorSyncTrigger）。
8. **R 轴控制：** `r_axis_rotate` 通过 `TMC_SetSpeed`（VACTUAL 寄存器）直接驱动 TMC2209（UART3），已对接。R 轴使用「使能→旋转→关闭」模式，`TMC_Init()` 初始化后驱动默认关闭，`r_axis_rotate` 内部自动使能/关闭。详见 §9.9 和 §16.7。
9. **LPUART1 未配置 DMA 接收：** `hdmarx = NULL`，仅用作 TMC2209 半双工阻塞通信。如果该通道用于其他用途需重新配置。
### 9.4 代码质量
10. 暂无
11. **`driver_motor.c runFail/runOK` 死循环：** 两个函数都是 `while(1){}` 空循环，无实际错误处理逻辑。
12. **未使用的全局变量：** `CAN1_0x1fe_Tx_Data` 等 7 个 8 字节数组（共 56 字节）、`CAN_RxDone`、`CAN_ID`、`realTimeLocation` 等，部分来自早期代码残留。`can_rx_queue` 已删除。
13. **`app_test.h` 与 `app_motion.h` 重复声明（已修复，见 §9.6-5）：** 重复的 `semX1Done`、`evtAxesDone` 等 extern 声明已从 `app_test.h` 移除。
### 9.5 编译与构建
14. **Keil MDK 工程：** 主要使用 MDK-ARM 目录下的 Keil 工程编译。CMakeLists.txt 也可用于构建。
15. **`overflow_count` 唯一声明在 `timestamp.c`：** `timestamp.h` 有 `extern volatile`，`main.c` 通过包含 `timestamp.h` 使用，不得在 main.c 中重复定义。

### 9.6 已完成的架构改进（2026-05）
1. **已创建 `host_pkt_queue`：** 16 深度 `HostMsg_t` 队列，UART 空闲中断回调 → 队列 → Host_Task。修复了原先队列未创建导致 NULL 写入的运行时 Bug。
2. **已添加 `g_debug_mutex` + `DEBUG_CAN_ISR` 条件编译：** 互斥锁保护任务上下文 `PrintDebug` 的静态 `s_debug_buf`，解决多任务并发日志交错。ISR 中 PrintDebug 由 `DEBUG_CAN_ISR` 宏控制（默认关闭），彻底消除 ISR 阻塞 UART 问题。
3. **`StartHostMotionTestTask` 已改为事件驱动：** 原主循环 `vTaskDelay(10ms)` 轮询改为 `osThreadFlagsWait` 阻塞等待。UART 空闲中断通过 `osThreadFlagsSet(hostMotionTaskHandle, ...)` 唤醒任务，延迟从 ≤10ms 降至 <1ms。
4. **`Key_Task` 已改用 `osDelayUntil`：** 原 `osDelay(10)` 改为 `osDelayUntil`，消除任务执行时间导致的周期漂移，保证精确 10ms 扫描间隔。
5. **已删除未使用的 FreeRTOS 对象：** `semX1Done`、`semX2Done`、`semYDone`、`semEmergency`（信号量）和 `can_rx_queue`（队列）已从源码中移除。到位通知统一使用 `evtAxesDone` 事件组。


## 十、快速参考

### 9.7 已完成的改进（2026-06）

**1. TIM2 频率调整：** CubeMX 中 TIM2 ARR 从 1000 改为 19999，使 CH3(PB10) 产生 50Hz PWM 用于 Z 轴舵机。CH1(PA0, 12V_C1) 频率同步降至 50Hz，但由于 pulse 值远超 ARR 实际只做开关控制，不受影响。

**2. CubeMX PE8 AF Bug（已在 driver_servo.c 中修复）：** CubeMX 生成的 TIM5_CH3(PE8) AF 是 AF1，但 STM32G474 正确值是 AF2。Servo_Init() 中检测 TIM5 时自动修复。

**3. TIM5 HAL State 共享限制（已在 driver_servo.c 中绕过）：** TIM5_CH1 先启动导致 State=BUSY，阻塞 CH3 的 HAL_TIM_PWM_Start。通过临时恢复 State 绕过。

### 9.9 TMC2209 使能/关闭设计（2026-06-11~12）

**设计原则：** TMC2209 驱动仅在 R 轴需要旋转时使能，其余时间关闭（ENN=HIGH），防止电机持续通电发热。

**关键改动：**

| 文件 | 改动 |
|------|------|
| `driver_tmc2209.h` | 新增 `TMC_ENABLE_DELAY_MS 50` 宏，统一上电稳定延时 |
| `driver_tmc2209.c:365` | `TMC_Init()` 末尾加 `TMC_SetEnable(false)`，初始化后自动关闭 |
| `app_motion.c:r_axis_rotate` | 入口使能+延时→设 VACTUAL→运行→停止→关闭 |
| `app_test.c:StartMotorTestTask` | 循环内每次使能→运行 2s→停止→关闭→反转重复 |
| `app_host.c` | 启动时 `TMC_SetEnable(false)` 确保驱动关闭 |
| `app_test.c:StartCamTestTask` | 启动时 `TMC_SetEnable(false)`（本任务不使用 R 轴） |

**CubeMX 默认值陷阱：** PD15(TMC1_EN) 在 CubeMX 中配置为 GPIO_Output，默认初始电平 LOW。ENN 低有效，因此从 boot 起 TMC2209 即为使能状态。如果没有任何任务调用 `TMC_SetEnable(false)`（例如 Host_Task 被注释），TMC2209 将持续通电。**任何不使用 R 轴的任务必须显式调用 `TMC_SetEnable(false)`。**

**调用模式：** 所有 TMC2209 使用点统一遵循：
```
TMC_SetEnable(true) → vTaskDelay(TMC_ENABLE_DELAY_MS) → TMC_SetSpeed(...) → 运行 → TMC_SetSpeed(0) → vTaskDelay(停稳) → TMC_SetEnable(false)
```

**4. TMC2209 VACTUAL 启动扭矩不足：** 直接跳全速时静摩擦卡住电机，需用速度斜坡（5000→80000 µstep/s，每级 +8000，40ms/级）。

**5. DRV8803 24V 端口为低端开关：** Port_24VO1(PA6) 等 24V 端口负载串在 24V 电源和 OUT 之间，PA6=LOW 时 OUT 拉 GND 负载导通，逻辑与 12V 端口相反。


### 9.8 已完成的架构改进（2026-06-10）

**1. Host_Task 启动不再发送 DOWNLOAD_READY：** 上位机协议规定 `DEBUG_MODE` 解锁调试按钮，`DOWNLOAD_READY` 进入文件下载模式。原代码启动时同时发送两者，导致上位机被 `DOWNLOAD_READY` 带入下载模式，调试按钮被重新锁定。修复：启动只发 `DEBUG_MODE\n`，`DOWNLOAD_READY` 仅在下载完成或退出调试时发送。

**2. 命令解析从 ISR 移至任务上下文（UART_PeekData 架构）：** 原架构 `HAL_UARTEx_RxEventCallback(ISR)` → `Host_UartRecvCallback(ISR)` → `LineParser_Feed`(含 `strtof` 浮点) → `host_pkt_queue` → `Host_Task`。Cortex-M4F 在 ISR 中使用 FPU 浮点运算可能导致静默失败，且 `g_parser` 被 ISR 和任务同时使用存在竞态。修复：`Host_UartRecvCallback` 改为空函数（仅保留 `(void)data; (void)len;`），`Host_Task` 主循环改用 `UART_PeekData` + `LineParser_Feed`（全部在任务上下文执行），`host_pkt_queue` 弃用。

**3. move_xy_relative 进入等待循环前加 UART_ClearData：** `UART_PeekData` 只读不清 `data_ready` 标志。外层主循环读完后标志仍置位，导致 `move_xy_relative` 内部等待循环中的 `UART_PeekData` 检测到残留数据，立即返回 -3（误判为中断命令）。修复：等待循环前调用 `UART_ClearData(UART_CH1)` 刷新标志。此 bug 导致 `MOVE_TO`/`SET_ORIGIN` 等阻塞式运动无效。

**4. HAL_TIM_PWM_Start CCER 不生效（改用 CMSIS API）：** 对 TIM2 调用 `HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_3)` 返回 HAL_OK 但 CCER 寄存器维持 0x00，CH3 输出未使能。根因未明确定位（疑似 HAL 库版本或编译优化问题）。修复：使用 CMSIS 级 `TIM_CCxChannelCmd(htim->Instance, TIM_CHANNEL_3, TIM_CCx_ENABLE)` + `__HAL_TIM_ENABLE(htim)`，并正确维护 `htim->State`（设为 BUSY → 操作 → 恢复）。此方案同时兼容 TIM5 的多通道 HAL State 锁问题。`driver_servo.c` 中相关注释已更新。

**5. TMC2209 ENN 引脚为低有效，Host_Task 用 TMC_SetEnable(false) 禁用：** PD15(TMC1_EN) 连接 TMC2209 Pin17 ENN（Enable Not）。LOW=驱动开启，HIGH=驱动关闭。CubeMX 初始化为 LOW（配合 TMC_Init 使用）。`Host_Task` 启动时调用 `TMC_SetEnable(false)` 关闭驱动，防止 R 轴电机在未初始化时发热。

**6. Servo_Init + DRV8803 初始化加入 Host_Task：** 原先缺少 `Servo_Init(&htim2)` 和 `DRV8803_Init()`，导致 `SET_SERVO` 命令无效（`servo_handles[2].initialized=false`），且 Z 轴舵机因 DRV8803 输出状态不确定而发热。修复：Host_Task 启动流程包含 `DRV8803_Init → EnableChip(1) → Servo_Init(&htim2) → SetOutput(12VO4) → Servo_SetAngle(Z_SERVO_CH, 120°)`。

**7. 常量语义化拆分：** 原 `JOG_SPEED`/`JOG_ACC` 被离散移动、MOVE_TO 和 JOG 共用，命名误导。拆分为三层：`DEBUG_SPEED`/`DEBUG_ACC`（MOVE_UP/DOWN/LEFT/RIGHT、MOVE_TO）、`JOG_SPEED`/`JOG_ACC`（MOVE_*_START 连续 JOG）、`PNP_SPEED`/`PNP_ACC`（Mark/找元件/贴装流程）。舵机通道号定义为 `#define Z_SERVO_CH 2`，消除魔法数字。

**8. parse_header 越界修复：** `while (p <= end)` 在空行时 `p == end` 多读一字节。改为 `p < end`。

**9. Vision_Init 重复调用清理：** 原先 `app_freertos.c` 和 `Host_Task` 各调用一次，导致 `[VISION] Init done` 打印两次。已从 `app_freertos.c` 移除。

**10. 主循环冗余 osDelay 清理：** 原主循环 switch 后有额外 `osDelay(5)`，导致 HOST_DEBUG 态每循环 15ms 而非预期的 10ms。已移除。

**11. HOST_DONE/HOST_ERROR 不再自动发 DOWNLOAD_READY：** 任务完成或出错恢复后不再发送 `DOWNLOAD_READY`，避免触发上位机进入文件下载模式锁定调试按钮。

**12. 运动命令不受 g_state 限制：** `handle_debug_cmd` 调用条件从 `if (g_state == HOST_DEBUG)` 改为 `if (cmd != RAW_LINE/NONE/UNKNOWN)`，确保即使状态意外切换（如被 CSV 数据误触 HOST_DOWNLOADING），运动命令仍能正常处理。

**13. Host_UartRecvCallback 重复注释头清理：** 移除旧的 `/* === Host_UartRecvCallback — UART ISR 中调用 === */` 注释块，只保留"已弃用队列模式"版本。

### 10.1 常用 GPIO 引脚速查
| 功能 | 引脚 | 备注 |
|------|------|------|
| USART1_TX/RX | PE0/PE1 | 上位机通信 |
| USART2_TX/RX | PD5/PD6 | MaixCam 摄像头 |
| USART3_TX/RX | PB9/PB11 | TMC2209(R轴) |
| CAN_TX/RX | PA12/PA11 | 三轴伺服电机 |
| 吸嘴气泵 | PE12 | 高有效 |
| 舵机 PWM (Z轴) | PB10 | TIM2_CH3 (50Hz) |
| 12V_C1 PWM | PA0 | TIM2_CH1 |
| 12V_C2 PWM | PE8 | TIM5_CH3 (50Hz) |
| 24V_C1 PWM | PB2 | TIM5_CH1 |
| 24V_C2 PWM | PB1 | |
| SPI2_SCK/MOSI | PB13/PB15 | LCD |
| SPI2_CS/DC/RST | PD10/PD9/PD8 | LCD 控制 |
| SPI3_SCK/MISO/MOSI | PC10/PC11/PC12 | Flash |
| SPI3_CS | PA15 | Flash 片选 |
| SPI4_SCK/MISO/MOSI | PE2/PE5/PE6 | ESP32 |
| SPI4_CS | PE3 | ESP32 片选 |
| ESP32_RESET | PC13 | ESP32 硬复位 |
| TMC1_EN (ENN) | PD15 | R轴使能（低有效：LOW=开启，HIGH=关闭） |
| TMC2_EN | PD14 | 预留 |
| KEY1/KEY2 | PC6/PC7 | 低有效 |
| CW/CCW/PUSH | PA8/PC8/PC9 | 低有效 |
| BOOT0 | PB8 | 启动选择 |
| 温度传感器 | PF9 / PA3 | DS18B20 |
| LCD_LED | PD8 | LCD 背光 |
| 12VO1(开关) | PE11 | 真空泵 / 12V输出1 |
| 12VO2(开关) | PE12 | 12V输出2（预留） |
| 12VO3(开关/PWM) | PE13 / PE8 | 12V输出3 + PWM(TIM5_CH3) |
| 12VO4(开关/PWM) | PE14 / PB10 | 12V输出4 + PWM(TIM2_CH3) |
| 24VO1(开关) | PA6 | 24V输出1 |
| 24VO2(开关) | PA7 | 24V输出2 |
| 24VO3(开关/PWM) | PC4 / PB1 | 24V输出3 + PWM(TIM3_CH4) |
| 24VO4(开关/PWM) | PC5 / PB2 | 24V输出4 + PWM(TIM5_CH1) |

### 10.2 电机 CAN 指令速查
| 功能码 | 功能 | 数据长度 |
|--------|------|----------|
| 0xF5 | 坐标绝对运动 | 7 字节 |
| 0xF3 | 使能/去使能 | 2 字节 |
| 0x82 | 设置工作模式 | 2 字节 |
| 0x92 | 设为零点 | 1 字节 |
| 0x4A | 同步标志开关 | 2 字节 (广播) |
| 0x4B | 同步执行触发 | 1 字节 (广播) |
| 0x95 | 到位阈值设置 | 4 字节 |
| 0x83 | 设置工作电流 | 3 字节 |
| 0x84 | 设置工作细分 | 2 字节 |
| 0x32 | 读取实时转速 | 2 字节 |
| 0x3F | 恢复出厂参数 | 2 字节 |
| 0x3D | 解除堵转保护 | 1 字节 |
| 0x85 | EN 引脚配置 | 2 字节 |

### 10.3 C 文件编码说明
- `Drivers/ZeMCU-G4/` 与 `Task/` 目录使用 **UTF-8（带 BOM）编码**
- `Core/` 目录（CubeMX 生成）仍为 **GBK（CP936）编码**
- CubeMX 生成的 CubeMX User Code 起始/结束标记：`/* USER CODE BEGIN ... */` / `/* USER CODE END ... */`
- CubeMX 重新生成代码时，标记外内容会被覆盖
### 10.4 DRV8803 端口对照与使用示例

**端口对应关系：**

| 逻辑端口 | 开关引脚 | PWM 引脚 | PWM TIM 通道 | 用途 |
|----------|---------|---------|-------------|------|
| `Port_12VO1` | PE11 | — | — | 真空泵 |
| `Port_12VO2` | PE12 | — | — | 预留 |
| `Port_12VO3` | PE13 | PE8 | TIM5_CH3 | 12V PWM 输出 |
| `Port_12VO4` | PE14 | PB10 | TIM2_CH3 | 12V PWM 输出 |
| `Port_24VO1` | PA6 | — | — | 24V 输出 |
| `Port_24VO2` | PA7 | — | — | 24V 输出 |
| `Port_24VO3` | PC4 | PB1 | TIM3_CH4 | 24V PWM 输出 |
| `Port_24VO4` | PC5 | PB2 | TIM5_CH1 | 24V PWM 输出 |

> **芯片级引脚**（固定，不属于输出端口）：
> U12(12V): PE9(EN) / PE10(RST) / PE15(FAULT)
> U13(24V): PA4(EN) / PB0(RST) / PA5(FAULT)

**数据结构：**

```c
typedef struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
} Pin_t;

typedef struct {
    uint8_t num_pins;   // 1=仅开关, 2=开关+PWM
    Pin_t   pins[2];    // pins[0]=开关引脚, pins[1]=PWM引脚
} PowerPort_t;
```

**API 函数：**

| 函数 | 说明 |
|------|------|
| `DRV8803_Init()` | 初始化所有输出端口为低电平，禁用两个芯片 |
| `DRV8803_SetOutput(port, on)` | 控制某端口开关（仅操作 pins[0]） |
| `DRV8803_EnableChip(id, enable)` | 芯片级使能（1=12V, 2=24V） |
| `DRV8803_IsChipFault(id)` | 查询芯片故障状态 |
| `DRV8803_TriggerChipReset(id)` | 硬件复位指定芯片 |
| `DRV8803_HandleFault_RTOS(id)` | FreeRTOS 任务中的故障恢复流程 |

**使用示例：**

```c
// 初始化
DRV8803_Init();
DRV8803_EnableChip(1, true);    // 使能 U12 (12V)

// 开关控制
DRV8803_SetOutput(&Port_12VO1, true);   // 开启真空泵
DRV8803_SetOutput(&Port_12VO1, false);  // 关闭

// PWM 控制（通过便捷宏访问引脚）
HAL_GPIO_WritePin(PWM_12VO3_PIN.port, PWM_12VO3_PIN.pin, GPIO_PIN_SET);

// 故障处理
if (DRV8803_IsChipFault(1)) {
    DRV8803_HandleFault_RTOS(1);
    DRV8803_EnableChip(1, true);
    DRV8803_SetOutput(&Port_12VO1, true);
}
```


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

### 12.1 移植概述

| 项目 | 移植前状态 | 移植后状态 |
|------|-----------|-----------|
| TouchGFX | 裸机 main() while(1) 轮询 | 独立 TouchGFX_Task（栈 8192B） |
| VSYNC 信号 | 未实现 | TIM7 硬件定时器 30Hz 模拟 VSYNC |
| LCD 驱动 | HAL_Delay 依赖 Systick | BusyDelay 忙等替代，移除 HAL_Delay |
| 按键驱动 | 裸机轮询 | FreeRTOS Key_Task（10ms 周期） + 消息队列 |
| 主系统↔GUI 通信 | 全局变量轮询 | FreeRTOS 消息队列（Data_Transfer） |
| 系统时基 | Systick | TIM6（HAL 系统时基） |

### 12.2 新增文件清单

| 文件 | 说明 |
|------|------|
| `TouchGFX/target/KeyController.cpp` | 按键控制器，消费 keyEventQueue，松开触发 |
| `TouchGFX/target/KeyController.hpp` | KeyController 头文件，继承 ButtonController |
| `TouchGFX/gui/src/model/Data_Transfer.c` | 主系统↔GUI 消息队列模块 |
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

> ⚠️ CW/CCW 引脚与 AGENTS.md §二 不一致（规格: CW=PA8, CCW=PC8），反映编码器物理安装方向

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

### 12.8 仍存在的问题（⚠️ 待处理）

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

> 本章节供 Agent 和开发人员参考，描述 FreeRTOS ↔ TouchGFX 双向通信框架。

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

1. **单一入口**：所有 FreeRTOS↔GUI 通信必须经过 `Data_Transfer.h` 的 API，禁止直接操作全局变量
2. **路由解耦**：GUI 端只调用 `Model::sendCommand()`，不关心命令如何到达目标
3. **命名约定**：`DT_xxx` = 通知，`DT_CMD_xxx` = 命令，`onNotifyXxx` = Presenter 回调
4. **非阻塞**：所有队列操作使用 `osMessageQueuePut/Get` 零超时，不阻塞渲染循环
5. **向后兼容**：全局变量 `if_now_SMT`/`total_SMT`/`now_SMT`/`Temp`/`if_DOWNLOAD_READY` 保留可用，但新代码应通过 `DT_Notify*` 系列函数更新
## 十四、任务报告 — TouchGFX FreeRTOS 移植 + 分发中枢（2026-05-20~21）（后续任务报告可在此基础上进行延申）

### 14.1 任务概述

将 STM32G474 贴片机项目中的 TouchGFX GUI、ST7306 LCD 驱动、5键按键驱动从裸机迁移到 FreeRTOS，
并搭建统一的分发中枢（Dispatcher）实现 FreeRTOS ↔ TouchGFX 双向通信。

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


## ???ESP32 ???? ? IoT ?? (v2, 2026-05-28)

### 15.1 ????

?? SPI4 ??? ESP32-C3 ????????????????????????? Web ????
???? `esp-temp/ESP32-C3??????_v2.0.md`?

**?????**

| ? | ?? | ?? |
|----|------|------|
| ??? | `Drivers/ZeMCU-G4/driver_esp32.c/h` | SPI4 128B ????? + CS(PE3) ?? + C3RESET(PC13) ??? |
| ??? | `Task/app_esp_protocol.c/h` | ??(??/??/??)???(??)????????/????? |
| ??? | `Task/app_esp_task.c/h` | ESP_Task 500ms ?? + ???????? + ?????? + ???? |

### 15.2 ????

| ?? | ? | ??? | ??/?? |
|------|-----|--------|----------|
| `ESP_Task` | 512 | Normal | ???? + 500ms ?? |

**??????**
- `esp_cmd_queue` (8 ??) ? ???? ? ESP_Task??? WiFi ??/????

### 15.3 ???

```
??????                         ESP_Task                      ESP32-C3
(????)                            ?                              ?
now_SMT/total_SMT ??????? ?? "32/50" ??? 0x10 0x01 ??SPI4???  ?? ? WebSocket
if_now_SMT/Heater ??????? ?? "SMTing" ??? 0x10 0x02 ??SPI4???  ??
HeaterStatus.state ?????? ?? "1"/"0"  ??? 0x10 0x03 ??SPI4???
HeaterStatus.cur_temp ??? ?? "85.3"   ??? 0x10 0x04 ??SPI4???

ESP_SendCommand(WIFI_ON) ??? esp_cmd_queue ??? 0x20 0x01 ??SPI4???  ?? WiFi
                                                        ???SPI4??  0xF2 "1"
                              g_esp_wifi_connected = 1
```

### 15.4 ????? (???????)

| ?? | ???? | ?? |
|------|----------|------|
| `g_esp_wifi_enabled` | `app_esp_task.c` | WiFi ???? (0=?, 1=?) |
| `g_esp_wifi_connected` | `app_esp_task.c` | WiFi ?????? (ESP ??) |
| `g_esp_fault_code` | `app_esp_task.c` | ??? (0x00=???) |
| `g_esp_last_rx_tick` | `app_esp_task.c` | ??????? tick |

### 15.5 ?????

| ??? | ??? | ?? | ???? |
|--------|--------|------|----------|
| `0x10` | `0x01` | ???? | ESP_Task ???? |
| `0x10` | `0x02` | ???? | ESP_Task ???? |
| `0x10` | `0x03` | ????? | ESP_Task ???? |
| `0x10` | `0x04` | ????? | ESP_Task ???? (??>0.5?C) |
| `0x20` | `0x01` | ?? WiFi | `ESP_SendCommand(ESP_CMD_WIFI_ON)` |
| `0x20` | `0x02` | ?? WiFi | `ESP_SendCommand(ESP_CMD_WIFI_OFF)` |
| `0x30` | `0x01` | ???? | `ESP_SendCommand(ESP_CMD_QUERY_FAULT)` |
| `0x30` | `0x02` | ?? WiFi | `ESP_SendCommand(ESP_CMD_QUERY_WIFI)` |

### 15.6 ??? Bug

- **HostMotion ???????** `app_freertos.c` ? `osThreadNew(StartHostMotionTestTask, ...)` ??????????????
- **SPI4_CS/C3RESET ??????** `ESP_GPIO_Init()` ? CS ? RST ??

### 15.7 ??????

| ?? | ?? |
|------|------|
| `Drivers/ZeMCU-G4/driver_esp32.c` | 67 |
| `Drivers/ZeMCU-G4/driver_esp32.h` | 41 |
| `Task/app_esp_protocol.c` | 139 |
| `Task/app_esp_protocol.h` | 143 |
| `Task/app_esp_task.c` | 282 |
| `Task/app_esp_task.h` | 55 |
| `ESP32????_STM32?????_v2.md` | ???? |

### 15.8 ????

1. Keil ???? (????????)
2. ESP32 ??? (ESP ???????????)
3. ????? WiFi ????
4. TouchGFX GUI ?? WiFi ?????

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
5. TaskDelay(200) → 等 LDO/振荡器稳定
6. 写 GCONF/CHOPCONF/PWMCONF/IHOLD_IRUN/TPOWERDOWN

#### 问题 7：TX 引脚驱动模式

**结论：** 经过测试，TX 推挽模式（GPIO_MODE_AF_PP）配合 1kΩ 串联电阻是正确的。
开漏模式（GPIO_MODE_AF_OD）上升沿过慢导致信号质量问题。此 TMC2209 模组地址为 0x00，
波特率 115200，工作正常。

### 16.3 电机扭矩调优

| 参数 | 最终值 | 说明 |
|------|--------|------|
| 运行电流 | 1000mA (TMC2209_MOTOR_RUN_CURRENT) | 原 800mA 扭矩不足 |
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
| R正转 | 斜坡 5000→80000 µstep/s | — | — | — |
| R反转 | 斜坡 -5000→-80000 µstep/s | — | — | — |

### 17.2 关键实现细节

**R 轴速度斜坡：** TMC2209 VACTUAL 模式无极变速/加速斜坡。直接跳全速时静摩擦力会卡住电机，需从 5000 µstep/s 起步，每级 +8000，40ms/级，逐步提升至 80000。

**电磁阀控制：** 24V O1(PA6) 是 DRV8803 U13 的低端开关。PA6=LOW 时 OUT5 拉 GND，阀两端 24V 压差导通。PA6=HIGH 时 OUT5 拉 24V，阀两端同电位关断。代码用 `GPIOA->BSRR` 直写寄存器，先 HIGH 再 LOW 确保 DRV8803 锁存状态变化。

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

### 19.1 当前实际接线

| 逻辑端口 | 开关引脚 | PWM 引脚 | 实际用途 |
|----------|---------|---------|----------|
| Port_12VO1 | PE11 | — | 吸嘴气泵 |
| Port_12VO2 | PE12 | — | 预留 |
| Port_12VO3 | PE13 | PE8(TIM5_CH3) | 预留(PWM) |
| Port_12VO4 | PE14 | PB10(TIM2_CH3) | Z轴舵机供电+PWM |
| Port_24VO1 | PA6 | — | 电磁阀(低端开关) |
| Port_24VO2 | PA7 | — | 预留 |
| Port_24VO3 | PC4 | PB1(TIM3_CH4) | 预留(PWM) |
| Port_24VO4 | PC5 | PB2(TIM5_CH1) | 24V_C1(PWM) |

### 19.2 API 使用要点

- `DRV8803_Init()` 后必须分别调用 `DRV8803_EnableChip(1, true)` 和 `DRV8803_EnableChip(2, true)` 使能 U12/U13
- `DRV8803_SetOutput()` 仅操作开关引脚(pins[0])，HIGH=导通(HIGH-side ON)
- **24V 端口（Port_24VO1 等）是低端开关**：`DRV8803_SetOutput` 的 on/off 逻辑与输出状态相反。on=true 时 OUT 拉 24V（负载关），on=false 时 OUT 拉 GND（负载开）
- PWM 引脚(pins[1])由上层通过 HAL 定时器 API 控制，不走 DRV8803_SetOutput
- `DRV8803_Init()` 会将所有端口引脚拉低，使能芯片后 24V 端口默认处于"导通"状态（OUT=GND），需在初始化完成后主动拉高关断

### 19.3 故障排查

| 现象 | 检查项 |
|------|--------|
| 12V 端口不工作 | PE9(EN1) 是否为 LOW |
| 24V 端口不工作 | PA4(EN2) 是否为 LOW、PB0(RST2) 是否为 LOW |
| 电磁阀不动 | PA6 是否有 HIGH→LOW 跳变、U13 是否使能、24V 供电是否正常 |
| 舵机不转 | PB10 是否有 50Hz PWM、PE14(开关) 是否 HIGH |
