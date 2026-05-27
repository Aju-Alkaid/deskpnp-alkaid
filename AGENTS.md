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
| 串口2 (USART2) | PD5(TX) / PD6(RX), 921600, DMA, 连接 MaixCam 摄像头 |
| 串口3 (USART3) | PB9(TX) / PB11(RX), 115200, DMA, 连接 TMC2209(R轴) |
| LPUART1 | PC1(TX) / PC0(RX), 115200, 半双工, 预留 |
| CAN (FDCAN1) | PA12(TX) / PA11(RX), 1Mbps, 连接 3 台 MKS SERVO42D 总线伺服电机 (ID=0x01 X1, ID=0x02 X2, ID=0x03 Y) |
| SPI2 | PB13(SCK) / PB15(MOSI), CS=PD10, DC/RS=PD9, RST=PD8, 连接 LCD(ST7306) |
| SPI3 | PC10(SCK) / PC11(MISO) / PC12(MOSI), CS=PA15, 连接 W25Q64 Flash |
| SPI4 | PE2(SCK) / PE5(MISO) / PE6(MOSI), CS=PE3, RST=PC13, 连接 ESP32 通信模块 |
| TIM2 | CH1(PA0) PWM / CH3(PB10)：12V_C1 PWM 控制 / 32位时间戳基准 |
| TIM5 | CH1(PB2)：24V_C1 PWM / CH3(PE8)：12V_C2 及 MG995 舵机 PWM (50Hz) |
| TIM6 | HAL 系统时基 |
| CRC | 硬件 CRC 校验 |
| GPIO 按键 | KEY1(PC6) / KEY2(PC7) / CW(PA8) / CCW(PC8) / PUSH(PC9), 低电平有效 |
| DRV8803×2 | U12(12V 驱动): PE9(EN)/PE10(RST)/PE14(IN1)/PE13(IN2)/PE12(IN3)/PE11(IN4)/PE15(FAULT) |
|  | U13(24V 驱动): PA4(EN)/PB0(RST)/PA6(IN5)/PA7(IN6)/PC4(IN7)/PC5(IN8)/PA5(FAULT) |
| TMC2209 | UART3 通信, PD15(TMC1_EN) / PD14(TMC2_EN 预留) |
| 加热台 | CAN ID 0x10(命令) / 0x11(状态), 独立控制 |
| 温度传感器 | PF9 / PA3, DS18B20 |
| 舵机(Z轴) | PE8, TIM5_CH3, MG995 |
| 吸嘴气泵 | PE12, GPIO 输出(高有效) |
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
- **物理层：** 921600, 8N1, DMA+空闲中断
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
| `Host_Task` | 1024 | Normal | 上位机通信 + CSV解析 + 视觉协调 |
| `CAN_Process_Task` | 512 | Normal | 从 motor_event_queue 取 CAN 报文，设事件组标志 |
| `vMotorTestTask` | 1024 | Normal | 电机测试任务（当前活跃的生产任务） |
| `TouchGFX_Task` | 8192 | Normal | GUI 图形界面渲染 + VSYNC + 按键处理 |
| `Key_Task` | 256 | Normal | 硬件按键扫描（10ms）+ 消抖 → keyEventQueue |
| `PnP_Motion_Task` | 1024 | Normal | 正式运动任务（已注释，未激活） |

**任务间通信：**
- `motor_event_queue` (32深度) — CAN 中断 → CAN_Process_Task / vMotorTestTask
- `motion_cmd_queue` (20深度) — Host_Task → MotionTask_Func
- `host_pkt_queue` — 视觉回调/串口解析 → Host_Task
- `evtAxesDone` 事件组 — CAN_Process_Task 通知到位
- `keyEventQueue` (16深) — Key_Task → KeyController → TouchGFX 按键事件
- `dataTransferQueue` (16深) — 主系统 Task → Model::processQueue() → UI 数据同步
- `vsync_queue` (1深) — TIM7 ISR → TouchGFX 渲染循环
- `frame_buffer_sem` — TouchGFX 帧缓冲互斥锁
- `semX1Done/semX2Done/semYDone` 信号量 — 三轴独立到位信号

## 六、关键数据流

```
上位机 --[USART1 CSV]--> Host_UartRecvCallback → host_pkt_queue → Host_Task
                                                            │
                                              解析CSV → 元件数组 g_components[]
                                                            │
                                             ┌── Vision_SendCmd(process2) → 摄像头
                                             │   摄像头返回 Mark 点 → 计算对齐偏移
                                             │
                                        for each component:
                                             │   Vision_SendCmd(process1) → 找元件
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
5. **正式运动任务未激活：** `PnP_Motion_Task` 线程在 `app_freertos.c` 中被注释。当前运动指令由 `vMotorTestTask` 执行，后者是硬编码的测试序列。正式生产需要激活 `PnP_Motion_Task`。
6. **MOTION_CMD_PICK/PLACE 缺少 XY 移动到吸嘴/贴装位置：** `pick_component()` 和 `place_component()` 直接操作 Z 轴舵机，但调用前需要上层先发送 `MOTION_CMD_MOVE_TO` 到达目标位置。
7. **连续移动（MOVE_*_START）仅打印日志：** 调试模式的连续移动命令处理逻辑未实现实际的持续运动控制。
8. **R 轴控制：** `r_axis_rotate` 使用虚拟地址 `R_AXIS_ADDR=0x04` 通过 `positionMode3Run` 发送，但该地址没有实际电机，实际 R 轴由 TMC2209 通过 UART3 控制，两套系统未对接。
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

### 10.1 常用 GPIO 引脚速查
| 功能 | 引脚 | 备注 |
|------|------|------|
| USART1_TX/RX | PE0/PE1 | 上位机通信 |
| USART2_TX/RX | PD5/PD6 | MaixCam 摄像头 |
| USART3_TX/RX | PB9/PB11 | TMC2209(R轴) |
| CAN_TX/RX | PA12/PA11 | 三轴伺服电机 |
| 吸嘴气泵 | PE12 | 高有效 |
| 舵机 PWM (Z轴) | PE8 | TIM5_CH3 |
| 12V_C1 PWM | PB10 | TIM2_CH3 |
| 12V_C2 PWM | PE8 | TIM5_CH3 (与舵机共用) |
| 24V_C1 PWM | PB2 | TIM5_CH1 |
| 24V_C2 PWM | PB1 | |
| SPI2_SCK/MOSI | PB13/PB15 | LCD |
| SPI2_CS/DC/RST | PD10/PD9/PD8 | LCD 控制 |
| SPI3_SCK/MISO/MOSI | PC10/PC11/PC12 | Flash |
| SPI3_CS | PA15 | Flash 片选 |
| SPI4_SCK/MISO/MOSI | PE2/PE5/PE6 | ESP32 |
| SPI4_CS | PE3 | ESP32 片选 |
| ESP32_RESET | PC13 | ESP32 硬复位 |
| TMC1_EN | PD15 | R轴使能 |
| TMC2_EN | PD14 | 预留 |
| KEY1/KEY2 | PC6/PC7 | 低有效 |
| CW/CCW/PUSH | PA8/PC8/PC9 | 低有效 |
| BOOT0 | PB8 | 启动选择 |
| 温度传感器 | PF9 / PA3 | DS18B20 |
| LCD_LED | PD8 | LCD 背光 |
| DRV1_IN4_PIN | PE11 | 控制真空泵使能失能 |

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

### 11.8 move_xy_relative 优化要点

（`Task/app_test.c` 第 548-616 行）

1. **按需发指令**：仅向 dx!=0 或 dy!=0 的轴发送 positionMode3Run，避免向静止轴发冗余命令
2. **按需等待**：`done_mask` 只包含需要运动的轴完成标志，不等静止轴
3. **osDelay(2) 防崩溃**：连续 CAN 发送间插入 2ms 延时，释放 PrintDebug→vsnprintf（现为 dbg_vformat）栈帧
4. **CMSIS 错误码过滤**：`if ((int32_t)flags < 0)` 先判断是否为错误码再检查事件标志
5. **中断响应**：轮询期间处理 UART 新命令，支持运动中取消

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
