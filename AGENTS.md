# PnP 贴片机嵌入式固件 — 项目说明书

> **历史记录请见 [HISTORY.md](E:/Desktop/qiansai/pnp_1/HISTORY.md)。** 本文档为当前状态参考，历史会话记录、调试过程、已修复 Bug 的详细记录均已迁移至 HISTORY.md。

> **文件编辑规则（AI Agent 必读）**
> 
> 工具链：Node.js 可用（`node -e`），无 Python。源文件 UTF-8 + CRLF。
> 
> **禁止用 PowerShell 字符串拼接、行号定位、ArrayList Insert/RemoveAt 做代码编辑。**
> 
> **唯一可靠方式：Base64 + Node.js**
> 
> ```
> $old = "从目标文件原样复制的原文"
> $new = "替换后的文本"
> $f   = "E:/path/to/file.c"
> $ob  = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($old))
> $nb  = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($new))
> node -e "const f=require('fs');let c=f.readFileSync('$f','utf8');" +
>          "let o=Buffer.from('$ob','base64').toString('utf8');" +
>          "let n=Buffer.from('$nb','base64').toString('utf8');" +
>          "c=c.replaceAll(o,n);f.writeFileSync('$f',c,'utf8');"
> ```
> 
> 关键：`$old` 必须从目标文件原样复制，换行符（CRLF/LF）要一致。
> 多次替换在同一个 node -e 内串联 replaceAll。
> 匹配失败 node 输出 NOT FOUND，不破坏文件。
> 
> **手动使用（推荐）：** 编辑 `tools\_replace.ps1` 中的 `$TARGET_FILE` / `$OLD_TEXT` / `$NEW_TEXT`，
> 然后 `powershell -ExecutionPolicy Bypass -File .\tools\_replace.ps1` 运行。
> 多行文本直接粘贴在 `@'...'@` 之间，无需转义。
> 
> **v2 特性：** `-DryRun` 预览模式 | 4 种 CRLF/LF 自动回退匹配 | 失败时输出诊断（搜索文本首行 + 文件偏移）


## 一、项目概览

桌面级贴片机，主控 STM32G474VETx（170MHz Cortex-M4F），基于 STM32CubeMX 生成 HAL 库工程，FreeRTOS 多任务调度。

功能：接收上位机坐标文件 → 双目视觉定位元件 → CAN 总线控制三轴运动 → 吸嘴拾取/放置 → 加热台控温。

**团队分工：** 本仓库为嵌入式固件（C 语言），视觉/硬件/GUI（TouchGFX）由其他人负责。

## 二、硬件平台

| 资源 | 详情 |
|------|------|
| MCU | STM32G474VETx, HSE 25MHz → PLL 170MHz |
| 调试接口 | SWD (NRST=PG10) |
| 串口1 (USART1) | PE0(TX) / PE1(RX), 115200, DMA, 连接上位机 |
| 串口2 (USART2) | PD5(TX) / PD6(RX), 115200, DMA, 连接 MaixCam 摄像头 |
| 串口3 (USART3) | PB9(TX) / PB11(RX), 115200, DMA, 连接 TMC2209(R轴) |
| LPUART1 | PC1(TX) / PC0(RX), 115200, 半双工, 预留 |
| CAN (FDCAN1) | PA12(TX) / PA11(RX), 500kbps, 连接 3 台 MKS SERVO42D 总线伺服电机 (ID=0x01 X1, ID=0x02 X2, ID=0x03 Y) |
| SPI2 | PB13(SCK) / PB15(MOSI), CS=PD10, DC/RS=PD9, RST=PD8, 连接 LCD(ST7306) |
| SPI3 | PC10(SCK) / PC11(MISO) / PC12(MOSI), CS=PA15, 连接 W25Q64 Flash |
| SPI4 | PE2(SCK) / PE5(MISO) / PE6(MOSI), CS=PE3, RST=PC13, 连接 ESP32 通信模块 |
| TIM2 | CH1(PA0) 12V_C1 PWM / CH3(PB10) Z轴舵机 PWM (50Hz) / 32位时间戳基准 |
| TIM5 | CH1(PB2) KTH7823 编码器输入捕获 (170MHz) / CH3(PE8) Z轴舵机 PWM (50Hz) |
| TIM6 | HAL 系统时基 |
| CRC | 硬件 CRC 校验 |
| GPIO 按键 | KEY1(PC6) / KEY2(PC7) / CW(PA8) / CCW(PC8) / PUSH(PC9), 低电平有效 |
| DRV8803×2 | U12(12V 驱动): PE9(EN)/PE10(RST)/PE15(FAULT) ; 输出端口见 §10.4 |
|  | U13(24V 驱动): PA4(EN)/PB0(RST)/PA6(IN5)/PA7(IN6)/PC4(IN7)/PC5(IN8)/PA5(FAULT) |
| TMC2209 | UART3 通信, PD15(TMC1_EN) / PD14(TMC2_EN 预留) |
| KTH7823 磁编码器 | PB2, TIM5_CH1 输入捕获 (PSC=0, 170MHz), 910Hz PWM 14-bit 绝对位置 |
| 加热台 | CAN ID 0x04(命令) / 0x05(状态), FDCAN1 500kbps, 共享电机 CAN 总线 |
| 温度传感器 | PF9 / PA3, DS18B20 |
| 舵机(Z轴) | PB10, TIM2_CH3, MG995 (50Hz PWM, 角度越大吸嘴越低) |
| 吸嘴气泵 | PE11 (12VO1, DRV8803 U12 OUT1 开关) |
| 下相机补光灯 | PE12 (12VO2, DRV8803 U12 OUT2 开关) |
| 电磁阀 | PA6 (24VO1, DRV8803 U13 OUT5, PA6=HIGH时导通 — 标准DRV8803: IN=HIGH→OUT=LOW) |
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
│       ├── driver_kth7823.c/h   # KTH7823 磁编码器 PWM 输入捕获 + 角度解算 (R轴闭环反馈)
│       ├── driver_servo.c/h     # MG995 舵机 PWM 控制（TIM2_CH3 / PB10）
│       ├── driver_drv8803.c/h   # DRV8803 双芯片 8通道驱动（12V+24V）
│       ├── driver_heater.c/h    # 加热台 CAN 通信 (CAN ID 0x04/0x05, 自有 CRC 协议, 见 §4.4)
│       ├── driver_timer.c/h     # 定时器工具
│       ├── driver_spiflash_w25q64.c/h  # SPI Flash (W25Q64)
│       ├── driver_esp32.c/h     # ESP32 SPI4 通信驱动
│       ├── tmc_protocol.c/h     # TMC2209 协议层
│       ├── pid.c/h              # 通用 PID 控制器（位置/速度模式）
│       ├── motor.c/h            # 32步进电机控制 (TMC2209+PID)
│       ├── ringbuf.c/h          # 环形缓冲区 (CAM_RING=1024, HOST_RING=4096)
│       ├── key.c/h              # 5键扫描（20ms 消抖 + 事件型）
│       ├── timestamp.c/h        # TIM2 32位时间戳，overflow_count 全局溢出计数
│       ├── lcd.c/h              # ST7306 LCD 驱动
│       ├── lcd_config.h         # LCD 配置
│       └── driver_CH340.c/h     # 串口文件转存（CH340 USB转串口，未实现）
├── Task/                        # ★ FreeRTOS 应用层任务 ★
│   ├── app_host.c/h             # 上位机通信任务 + CSV解析 + 视觉协调 + 调试模式
│   ├── app_uart_parser.c/h      # 上位机行协议解析器（COMMAND arg\n 格式）
│   ├── app_vision.c/h           # MaixCAM v2 帧协议 (0x7E LEN PAYLOAD 0x7F) + P1/P2/P3 状态机
│   ├── app_motion.c/h           # 运动控制函数 + CAN_Process_Task + MotionTask_Func
│   ├── app_motor.c/h            # 电机应用层（占位）
│   ├── app_test.c/h             # 测试任务 (vMotorTestTask) + PrintDebug 函数
│   ├── app_config.h             # 全局配置常量（标定默认值、速度常量）
│   ├── app_esp_protocol.c/h     # ESP32 SPI 协议层（组包/解包/校验）
│   ├── app_esp_task.c/h         # ESP32 周期数据推送任务
│   ├── app_logger.c/h           # SPI Flash 运行日志（W25Q64 0x7FE000，7种事件）
│   ├── app_touchgfx_bridge.c/h  # TouchGFX ↔ 主控桥接层
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
  - `MOVE_UP/DOWN/LEFT/RIGHT <步长mm>` — 离散移动（0.3/0.5/1/5/10）
  - `MOVE_*_START <速度mm/s>` — 连续移动开始（1~50），第二次点击自动发 `MOVE_STOP`
  - `MOVE_STOP` — 停止连续移动
  - `MOVE_TO <x> <y>` — 运动至绝对坐标 (mm)
  - `SET_ORIGIN` — 当前位置设为零点。仅 HOST_HOME 或 HOST_DEBUG 状态下生效，PnP 途中调用被忽略（防止意外毁掉座标系）。HOST_HOME 状态触发后自动发 DEBUG_MODE 进入 HOST_DEBUG
  - `SET_SERVO <角度>` — Z 轴舵机 (0~180°, 角度越大吸嘴越低)
  - `SET_R_AXIS <角度>` — R 轴旋转 (0~360°)
  - `PUMP_ON` — 开启气泵
  - `PUMP_OFF` — 关闭气泵 + 电磁阀吹气 1s 后关阀
  - HEAT_ON / HEAT_OFF — 加热台启动/停止
  - AUTO_HEAT ON / OFF — 贴装完成后自动启动/关闭回流焊
  - EXIT_DEBUG_MODE — 退出调试模式
  - HOME — 回零（移动至原点）
  - VALVE_ON — 开电磁阀
  - VALVE_OFF — 关电磁阀
  - RESUME — 从 WAIT_REFILL / ERROR 恢复
  - ABORT — 中止当前 PnP 流程，回 HOST_DEBUG
  - **标定命令：** SET_SCATTER_AREA / SET_SCATTER_SIZE <mm> / SET_PCB_AREA_MIN / SET_PCB_AREA_MAX / SET_BOTTOM_CAM / SET_Z_SAFE / SET_Z_PICK / SET_Z_PLACE / SET_R_ZERO / SAVE_CALIB / RESTORE_CALIB
- **文件下载流程：**
  1. G4 发送 `DOWNLOAD_READY\n` 给上位机
  2. 上位机逐行发送 CSV 数据（每行以 `\n` 结尾，UTF-8 编码）
  3. 首行作为表头解析（检测 `"Designator"` 或 `Designator`，兼容引号/无引号两种格式）
  4. 500ms 超时无新行 → 下载完成，自动进入 Mark 点对齐流程
  5. CSV 格式：15 列，`\t` 分隔，字段以双引号包裹。列：Designator / Device / Footprint / Mid X / Mid Y / Ref X / Ref Y / Pad X / Pad Y / Pins / Layer / Rotation / SMD / Comment / Name
  6. 坐标值含 `mm` 后缀（如 `"8mm"`），固件自动去后缀解析为浮点数
  7. 详细规范见上位机文档《单片机端 CSV 数据处理规范》
- **无校验：** 纯文本协议，依赖 UART 硬件可靠性


### 4.2 G4 ↔ MaixCam 摄像头 (USART2, PD5/PD6)

- **物理层：** 115200, 8N1, DMA+空闲中断
- **帧协议（v2，2026-06-17 更新）：**
  ```
  ┌────────┬────────┬──────────────────┬────────┐
  │  0x7E  │ length │  UTF-8 Payload   │  0x7F  │
  │ 1 Byte │ 1 Byte │   0~255 Bytes    │ 1 Byte │
  └────────┴────────┴──────────────────┴────────┘
  ```
  - 帧头 `0x7E`、帧尾 `0x7F` 各 1 字节
  - `length`：payload 字节数（0~255）
  - `payload`：UTF-8 字符串
  - 接收状态机：`WAIT_HEAD(0x7E) → WAIT_LEN → WAIT_DATA(len字节) → WAIT_TAIL(0x7F)`

- **通信模型：** 主控 → MaixCAM，请求-响应，单任务串行

- **P0 启动握手：**
  ```
  Host → Cam: p0          // 握手请求
  Cam  → Host: rdy         // 握手成功（120s 超时发 err0，主控须重发 p0）
  ```

- **P1 — 元件识别（上摄像头，YOLO 224×224，4 类：ccapt/cledy/cledo/crest，2026-07-11 升级为单次检测）：**

  | 步骤 | 方向 | 帧 | 说明 |
  |------|------|-----|------|
  | 1 | Host→Cam | `p1` | 启动 P1 |
  | 2 | Cam→Host | `rdy` | **询问类别** |
  | 3 | Host→Cam | `cls` | 类别指令 |
  | 4 | Host→Cam | `N:{class_id}` | 0=ccapt, 1=cledy, 2=cledo, 3=crest |
  | — | — | — | 映射规则：`LED*`→cledo(2), `C0*`/`R0*`→ccapt(0), 其他→ccapt(fallback) |
  | 5 | Host→Cam | `end` | 结束类别询问 |
  | 6 | Cam 开始 Phase0 搜索 | | |

  - **Phase0（搜索）：** Cam→Host: `stp`（连续3帧锁定确认）→ Host→Cam: `go`（停机确认）
  - **Phase1（单次检测，无 Phase2）：** Cam 采集 20 帧位置+角度，MAD 去野值 + 圆形均值滤波 → 一次性发送：
    ```
    Cam→Host: "pos"
    Cam→Host: "N:{dx * 47.077}"     // X 偏差，**已是电机步数**
    Cam→Host: "N:{dy * 47.077}"     // Y 偏差，**已是电机步数**
    Cam→Host: "N:{final_ao}"        // final_ao = -angle * 100（百分之一度）
    Cam→Host: "N:{class_id}"        // 0=ccapt 1=cledy 2=cledo 3=crest
    Cam→Host: "end"
    ```
    共 **4 字段**（dx, dy, angle, class_id）。角度×100，紧跟 pos 一次性返回，无需 Phase2。
  - **关键参数：** `p1_initial_gain = 47.077`（像素→电机步数），`INIT_FRAMES = 20`，`HUNT_LOCK_FRAMES = 3`
  - **错误：** `err1_1`（初始化失败）、`err1_3`（Phase0 超时）、`err1_4`（相机读取失败>50次）、`err1_5`（未检测到目标）。`err1_5/err1_1/err1_4` 每元件最多重试 3 次

- **P2 — Mark 多点寻标对位（上摄像头 448×448，Canny 边缘检测，2026-07-11 更新增益）：**

  | 步骤 | 方向 | 帧 | 说明 |
  |------|------|-----|------|
  | 1 | Host→Cam | `p2` | 启动 P2 |
  | 2 | Cam→Host | `rdy` | 就绪 |
  | 3 | Host→Cam | `go` | 开始搜索（逐个 Mark 循环） |
  | 4 | Cam→Host | `stp` | Mark 锁定（8帧稳定确认） |
  | 5 | Host→Cam | `go` | 确认停机 → 精确测位 |
  | 6 | Cam→Host | `pos` `N:{dx*48.5}` `N:{dy*48.5}` `end` | 偏移 **已是电机步数** |
  | 7 | Host→Cam | `go` | 移动完成 → 迭代归零 |
  | 8 | Cam→Host | `ok` | 对准完成（10帧平均，|offset|≤4.0px） |
  | — | — | — | 未对准 → 回到步骤 6（新 pos），最多 7 轮迭代 |
  | — | — | — | 非末 Mark 的 ok → 自动切 P2_WAIT_GO，等 Host 发 go 搜索下一个 |
  | — | — | — | 末 Mark 的 ok → VISION_DONE |
  | — | Host→Cam | `end` | 随时终止 |
  - **关键参数：** `p2_dxdy_gain = 48.5`（像素→电机步数），`p2_target_count = 3`，`p2_align_max_iter = 7`，`p2_align_th = 4.0px`
  - **错误：** `err2_1`（空闲超时）、`err2_3`（pos_detect 未检测到 Mark）、`err2_4`（对准迭代超 7 次）
  - **P2 退出必须发送 `end` 帧：** P2 完成或异常退出后，必须通过 `Vision_SendEnd()` 发送 `end` 帧通知摄像头终止 P2 会话。Host_Task 的 `mark_align_step` 和 `app_test.c` 的 `cam_p2_full_test_run` 在所有 P2 退出路径均已添加此调用。

- **P3 — 下相机单次检测对位（下摄像头 640×480，YOLO model_286344，2026-07-11 升级为单次检测）：**

  | 步骤 | 方向 | 帧 | 说明 |
  |------|------|-----|------|
  | 1 | Host→Cam | `p3` | 启动 P3 |
  | — | Cam | Phase0：吸嘴检查（YOLO 连续 6 帧检测吸嘴圆） | |
  | — | Cam→Host | `err3_8` | 检测到吸嘴圆 → 吸嘴空取（无元件） |
  | 2 | Cam→Host | `pos` + 数据包 + `end` | Phase1 单次检测，**已是电机步数**： |
  | — | — | — | `N:{dx * 13.5554}` — X 偏差 |
  | — | — | — | `N:{dy * 13.5554}` — Y 偏差 |
  | — | — | — | `N:{final_ao}` — final_ao = angle * 100（百分之一度） |
  | — | — | — | 共 **3 字段**（dx, dy, angle）。角度一次返回，无 Phase2 迭代。 |
  - **关键参数：** `p3_dx_gain = p3_dy_gain = 13.5554`（像素→电机步数），`avg_frames = 10`，`p3_nozzle_check_frames = 6`
  - **错误码：** `err3_1`（下相机初始化失败）、`err3_5`（YOLO 检测帧数不足）、`err3_8`（**吸嘴空取**—不可降级贴装，主控回退重新吸取）
  - **模型：** P3 使用 `model_286344.mud`（cap/res/led），P1/P2 使用 `model_284490.mud`（cCapt/cLedy/cLedo/cRest），程序自动切换。

- **pos 格式汇总：**

  | 进程 | 帧序列 | 字段数 | 单位 | 增益 |
  |------|--------|--------|------|------|
  | P1 | `pos` N:dx N:dy N:ao N:cls `end` | 4 | **电机步数** | 47.077 |
  | P2 | `pos` N:dx N:dy `end` | 2 | **电机步数** | 48.5 |
  | P3 | `pos` N:dx N:dy N:ao `end` | 3 | **电机步数** | 13.5554 |

  > **重要变更（2026-07-11）：** CAM 端已将像素值预乘增益，输出的 N: 值**直接是电机步数**。
  > G4 固件端**不再做任何缩放**，仅进行轴映射（cam X→Y电机取反，cam Y→X1+X2）。
  > P1/P3 改为**单次检测**，Phase1 直接返回全部数据（含角度），不再有 Phase2 迭代对齐。
  > 角度符号：P1 `-angle*100`（取反），P3 `angle*100`（不取反）。
  > 详细协议参见上位机文档 `E:/聊天记录/通讯接口文档 (1).md`。
- **固件侧实现：**- **固件侧实现：** 见 `Task/app_vision.c/h`。帧解析 + 状态机在 `feed_byte`（ISR 安全）中运行；UART 帧发送（`send_frame`）仅在任务上下文调用，不在 ISR 中阻塞。

### 4.2.1 物理轴约定（设计坐标 → 电机坐标映射）

> **核心矛盾：** 因硬件接线，CAN ID 0x01/0x02 电机在代码中叫「X 电机」但实际驱动 Y 轴（前后），CAN ID 0x03 叫「Y 电机」但实际驱动 X 轴（左右）。下文中 `machine_x` 存储 Y 轴位置，`machine_y` 存储 X 轴位置——变量命名追随电机编号而非物理方向。

| 电机 | CAN ID | 变量名惯例 | 实际物理轴 | 正方向 |
|------|--------|-----------|-----------|--------|
| X1 | 0x01 | X 电机 | Y 轴（前后） | 上正下负 |
| X2 | 0x02 | X 电机 | Y 轴（前后） | 上正下负 |
| Y  | 0x03 | Y 电机 | X 轴（左右） | 左负右正 |

**映射规则（所有涉及设计坐标 → 电机坐标的地方必须遵守）：**

```
设计坐标 target_x  → 物理 X 轴（左右） → Y 电机   → machine_y（存的是 X 位置）
设计坐标 target_y  → 物理 Y 轴（上下） → X1+X2 电机 → machine_x（存的是 Y 位置）
相机 X 偏移 (dx) → Y 电机   (物理 X)  【取反: dy = -(dx * scale)】
相机 Y 偏移 (dy) → X1+X2 电机 (物理 Y)  【不取反: dx = +(dy * scale)】
```

**各环节符号速查：**

| 环节 | dx/X1+X2 公式 | dy/Y电机 公式 |
|------|-------------|-------------|
| Mark 跳转 | `+(tdy * 512)` | `-(tdx * 512)` |
| P2 偏移修正 | `-(r->dy)` | `-(r->dx)` |
| P1 偏移修正 | `-(r->dy)` | `-(r->dx)` |
| P3 偏移修正 | `+(r->dy)` 不取反 | `-(r->dx)` |

> **2026-07-11 更新：** CAM 端已预乘增益（P1=47.077, P2=48.5, P3=13.5554），G4 端直接使用 r->dx/r->dy 值，不再乘 scale。上表已移除 `* scale`。

> P3 的 dx 不取反是因为下相机图像左右镜像（相机朝上拍摄），X 轴自然反转。

### 4.3 G4 ? MKS SERVO42D 电机 (CAN, FDCAN1)

- **物理层：** CAN 2.0A, 500kbps, 标准帧(11位ID)
- **校验：** SUM8 CRC（ID+数据字节累加取低8位）
- **数据长度限制：** ≤7 字节有效数据 + 1 字节 CRC = 最多8字节
- **电机 ID：** X1=0x01, X2=0x02, Y=0x03, 广播=0x00
- **主要功能码：**
  - `0xFD` 位置模式1 — 绝对运动（positionMode1Run）
  - `0xF4` 位置模式2 — 相对运动（速度=2B+加速度=1B+相对坐标=3B，共7字节+CRC）
  - `0xF5` 位置模式3 — 绝对运动（帧格式同 0xF4，坐标为绝对位置）
  - `0xF6` 速度模式 — 连续运行（方向/速度=2B+加速度=1B，共4字节+CRC）
  - `0xF3` 使能/去使能
  - `0xF7` 立即停止（不受同步模式影响）
  - `0x82` 设置工作模式（SR_vFOC=0x05）
  - `0x92` 设为零点
  - `0x4A` 开启同步标志（须逐电机发送）
  - `0x4B` 同步执行触发（**必须发广播地址 0x00**，逐电机发会导致双 X 轴分步堵转）
  - `0x95` 设置到位阈值（默认 200 步，已改为 50 步（0.5mm 精度））
  - `0x83` 设置工作电流 — `Motor_Init()` 中已设三轴为 1400mA（写入 RAM，断电恢复默认）
- **状态码（电机→G4）：**
  - `0x01` 已接收/运行中（非同步模式立即开始运动）
  - `0x02` 运行完成（到位）
  - `0x03` 堵转保护触发（电机自动松轴，须发 0x3D 或按 Enter 复位）
  - `0x05` 指令已缓存，等待同步触发（同步模式下）


### 4.4 G4 ↔ 加热台从机 (CAN, FDCAN1)

- **物理层：** CAN 2.0A, 500kbps, 标准帧(11位ID)，与电机共享 FDCAN1 总线
- **CAN ID：** 命令帧 `0x04`（主控→加热台），状态帧 `0x05`（加热台→主控）
- **命令帧格式（主控→加热台）：**

| 字节 | 含义 |
|------|------|
| 0 | 命令码 |
| 1..N | 参数（如有） |
| N+1 | CRC = (字节0..N 累加和) & 0xFF（仅 SET_TEMP/SET_PID 附带；START/STOP/QUERY 无 CRC） |

- **状态帧格式（加热台→主控，6 字节）：**

| 字节 | 含义 | 类型 | 说明 |
|------|------|------|------|
| 0 | state | uint8 | 状态码 |
| 1 | cur_temp_H | uint8 | 当前温度高字节 |
| 2 | cur_temp_L | uint8 | 当前温度低字节 |
| 3 | tar_temp_H | uint8 | 目标温度高字节 |
| 4 | tar_temp_L | uint8 | 目标温度低字节 |
| 5 | error | uint8 | 错误码（0=正常） |

温度值为 **有符号 16 位大端**，单位 **0.1°C**。例：`0x08 0x98` = 220.0°C。

- **命令列表：**

| 命令码 | 名称 | 帧格式 | 说明 |
|--------|------|--------|------|
| `0x01` | START | `[0x01]` | 启动回流焊温度曲线，从机开始周期性（1500ms）上报温度 |
| `0x02` | STOP | `[0x02]` | 立即关闭加热，继续上报直至温度 < 60°C |
| `0x03` | SET_TEMP | `[0x03][temp_H][temp_L][crc]` | 手动 PID 控温模式，temp 为 int16 大端，0.1°C |
| `0x04` | SET_PID | `[0x04][Kp_H][Kp_L][Ki_H][Ki_L][Kd_H][Kd_L][crc]` | 设定 PID 参数，Kp/Ki/Kd 均为 int16 大端，单位 0.001 |
| `0x05` | QUERY | `[0x05]` | 查询状态，从机立即回复一次状态帧 |

- **状态码：**

| 值 | 名称 | 说明 |
|----|------|------|
| 0x00 | IDLE | 空闲，加热关闭 |
| 0x01 | HEATING | 加热中（曲线/手动） |
| 0x02 | HOLDING | 恒温保持 |
| 0x03 | COOLING | 降温中，仍在回报 |
| 0x04 | COMPLETE | 曲线完成，加热关闭 |
| 0x05 | ERROR | 故障（查看 error 字节） |

- **错误码：**

| 值 | 说明 |
|----|------|
| 0x00 | 无错误 |
| 0x01 | 热电偶断开 |
| 0x02 | 超温 |
| 0x03 | 通信超时 |

- **回流焊温度曲线：**

| 阶段 | 目标温度 | 时长 | 说明 |
|------|----------|------|------|
| 预热 | 150°C | 100s | ~1.5°C/s 升温 |
| 浸泡 | 150°C | 40s | 恒温 |
| 浸泡 | 180°C | 60s | 缓慢升温，激活助焊剂 |
| 升温 | 230°C | 55s | ~1.0°C/s 升至峰值 |
| 回流 | 230°C | 35s | 峰值保持，焊料熔化 |
| 冷却 | 60°C | 80s | ~2.1°C/s 降温 |

总周期约 370s，峰值 230°C。

- **实现要点：**
  - 加热台 CRC 不含 CAN ID，与电机协议（SUM8 含 CAN ID）不同，因此加热台使用独立的 `Heater_Transmit()` 直接调用 `HAL_FDCAN_AddMessageToTxFifoQ`，不经过 `CAN_Transmit_Data`
  - CAN RX 中断回调（`driver_can.c`）按 CAN ID 路由：ID=0x05 → `heater_rx_queue`，其余 → `motor_event_queue`
  - 详见 `driver_heater.c/h`
- **初始化顺序约束：** `Heater_Init()` 必须在 `CAN_Init()` 之前调用。`CAN_Init()` 中 `HAL_FDCAN_Start()` 使总线激活后 CAN ISR 即可触发，若 `heater_rx_queue` 尚未创建（NULL），加热台状态帧将被静默丢弃。
- **CAN 滤波器隐患：** `FilterID2 = 0x1FFC0000` 超出 HAL 11-bit 范围（max 0x7FF），当前因 CubeMX 设 `StdFiltersNbr = 0` 恰好全通。重新生成 CubeMX 时若 `StdFiltersNbr` ≥ 1，滤波器将仅通过 ID 低 8 位为 0 的帧，所有 CAN 通信中断。详见 HISTORY.md §33.2。

## 五、任务架构

| 任务 | 栈大小 | 优先级 | 功能 |
|------|--------|--------|------|
| `Host_Task` | 1024 | Normal | ★ 主任务：上位机通信 + 调试命令 + CSV解析 + 视觉协调 + PnP流程 |
| `CAN_Process_Task` | 512 | Normal | 从 motor_event_queue 取 CAN 报文，更新 g_axes_done_bits / g_axes_error 全局标志 |
| `vMotorTestTask` | 1024 | Normal | MKS 电机测试任务（已注释） |
| `TouchGFX_Task` | 8192 | Normal | GUI 图形界面渲染 + VSYNC + 按键处理 |
| `Key_Task` | 256 | Normal | 硬件按键扫描（10ms）+ 消抖 → keyEventQueue |
| `ESP_Task` | 512 | Normal | ESP32 通信 + WiFi 状态管理 |
| `StartESPTestTask` | 1024 | Normal | ESP32 通信链路 8 项测试（SPI4），启用时需禁用 `ESP_Task` |
| `PnP_Motion_Task` | 1024 | Normal | 备用运动任务（已注释，未激活） |
| `StartHostMotionTestTask` | 4096 | Normal | 调试用运动任务（已注释，功能合并到 Host_Task） |
| `StartPickPlaceTestTask` | 2048 | Normal | Pick&Place 测试（已注释，功能合并到 Host_Task） |
| `StartMotorTestTask` | 1024 | Normal | TMC2209 测试（已注释） |
| `StartCamTestTask` | 2048 | Normal | 摄像头+电机联动测试（P0/P1/P2/P3 协议验证 + 角度追踪 + R轴旋转） |

**任务间通信：**
- `motor_event_queue` (32深度) — CAN 中断 → CAN_Process_Task / vMotorTestTask
- `motion_cmd_queue` (20深度) — Host_Task → MotionTask_Func
- `host_pkt_queue` (64深) — 已弃用，Host_Task 改用 UART_PeekData 直接读取
- `evtAxesDone` 事件组 — 已弃用（保留对象但不再使用），改为 `g_axes_done_bits`/`g_axes_error` volatile 全局变量
- `keyEventQueue` (16深) — Key_Task → KeyController → TouchGFX 按键事件
- `dataTransferQueue` (16深) — 主系统 Task → Model::processQueue() → UI 数据同步
- `vsync_queue` (1深) — TouchGFX 内部 (OSWrappers.cpp)，TIM7 ISR → 渲染循环
- `frame_buffer_sem` — TouchGFX 内部 (OSWrappers.cpp)，帧缓冲互斥锁
- `semX1Done/semX2Done/semYDone` 信号量 — 已废弃，统一使用 evtAxesDone 事件组（见 §9.6-5）
- `heater_rx_queue` (10深度) — CAN ISR → Heater_ProcessStatus()，加热台状态帧专用队列
- `g_coord_mutex` (互斥锁) — Coord_Get/Update/Invalidate 内部，保护 MachineCoord_t 读写

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
                └─ RAW_LINE(DOWNLOADING) → csv_get_field() 按\t分割15列
                                             │   剥离引号 + parse_mm() 去mm后缀
                                             │   SMD白名单过滤 → g_marks[] (MARK)
                                             │                  → g_components[] (Yes)
                                             │
                                             └── 500ms超时 → download_done()
                                                   │
                                                   ├─ g_mark_count>0 → P2建系
                                                   │    mark_align_step() → Vision_Start(VCMD_P2)
                                                   │    → g_mark_avg_dx/dy (Mark平均偏移)
                                                   │
                                                   └─ g_comp_count>0 → 逐个贴装循环
                                                        HOST_FIND_COMP          → P1找元件(含类别询问)
                                                        → HOST_PICK              → 吸取元件(Z轴舵机+气泵)
                                                        → HOST_MOVE_TO_BOTTOM_CAM → 移动到下相机站(复位P3偏移)
                                                        → HOST_OFFSET_CHECK      → P3下相机偏移检测+累积
                                                        → HOST_MOVE_TO_PCB       → 旋转补偿+PCB原点偏移+P3偏移→移动到贴装位
                                                        → HOST_PLACE             → 贴装元件(Z轴舵机+电磁阀吹气)
                                                        → 下一元件或 HOST_DONE
                                                        ┌ AUTO_HEAT 开启时 → HOST_REFLOW (回流焊) → HOST_DEBUG
                                                        ├ 连续3次P1失败  → HOST_WAIT_REFILL (等RESUME)
                                                        └ 电机异常/P2失败 → HOST_ERROR (30s超时自动回DEBUG)

              Heater_ProcessStatus()   → 每轮处理加热台CAN状态帧```

## 七、关键数据结构

| 结构体 | 所在文件 | 用途 | 关键字段 |
|--------|----------|------|----------|
| `Component_t` | app_host.h | 贴装元件/Mark点信息 | id, target_x/y/angle, footprint[32], layer, is_mark, feeder_id, placed |
| `HostParsed_t` | app_uart_parser.h | 上位机行解析结果 | cmd, param(float), raw[512] |
| `VisionResult_t` | app_vision.h | 视觉结果数据（v2 协议） | dx, dy, angle_x100, angle_valid, class_id, class_name[8], mark_index, mark_count |
| `MotionCmd_t` | app_motion.h | 运动指令 | cmd_type, target_x/y/r, speed, acc |
| `MachineCoord_t` | app_motion.h | 机器座标系（线程安全单例） | x, y, r, z, homed, valid |
| `CAN_Rx_Packet_t` | driver_can.h | CAN 数据包 | ID, FuncCode, Status, Data[8], Timestamp |
| `RingBuf_t` | ringbuf.h | 环形缓冲区 | buffer, size, head(写)/tail(读) |
| `LineParser_t` | app_uart_parser.h | 行解析器状态机 | buf[512], idx, complete |
| `UART_Channel_t` | driver_uart.c(内部) | UART 通道控制块 | huart/hdmarx, 双缓冲, data_ready/is_rx_active/overflow_count |
| `MotorState_t` | driver_motor.h | 电机状态枚举 | IDLE/SENDING/WAITING/COMPLETE/ERROR |
| `HeaterStatus_t` | `driver_heater.h` | 加热台状态快照 | state, cur_temp(0.1°C), tar_temp, error, timestamp |

## 八、编码规范与约束

1. **文件编码：** 所有 `.c/.h` 文件使用 **UTF-8** 编码。读写时必须指定 `[Text.Encoding]::GetEncoding(''UTF-8'')`
2. **CubeMX 用户代码区：** 自定义代码只能写在 `USER CODE BEGIN/END` 标记之间，否则重新生成时会被覆盖
3. **禁止批量删除：** 禁止 `del /s`, `rd /s`, `Remove-Item -Recurse` 等批量删除命令
4. **中文注释：** 项目标准使用中文注释
5. **修改审批：** AI 提出问题/建议 → 用户审核 → 批准后修改
6. **中断安全：** ISR 内禁止阻塞调用（如 PrintDebug 中的 `HAL_UART_Transmit`），禁止 `osDelay`
7. **栈溢出防护：** 每个任务栈大小已在 app_freertos.c 定义，增加 printf 类函数需增大栈（至少 512）

## 九、已知问题与注意事项

### 9.2 警告问题（建议修复）
2. **CAN ISR 中调用 PrintDebug（已修复，见 §9.6-2）：** `HAL_FDCAN_RxFifo0Callback` 中 PrintDebug 调用已用 `#ifdef DEBUG_CAN_ISR` 包裹。
3. **`CAN_Transmit_Data` 中调试打印（已修复，见 §9.6-2）：** 每次 CAN 发送的 TX 日志同样用 `#ifdef DEBUG_CAN_ISR` 包裹。
4. **`motor_send_move_cmd` 函数体冗余：** 该函数的 buffer 填充逻辑与 `positionMode3Run` 重复，实际调用也是转发到后者。建议移除冗余逻辑或直接废弃此函数。

### 9.3 功能性问题（部分已解决）
5. **离散移动命令去重 BUG（已修复）：** `handle_debug_cmd` 的去重逻辑原先对所有命令生效，导致同方向同步长的离散移动（MOVE_UP/DOWN/LEFT/RIGHT）只能执行一次。已收窄为仅对 JOG START 命令去重。
5. **正式运动任务（已解决，见 §9.8）：** `PnP_Motion_Task` 已由 `Host_Task` 取代，`Host_Task` 统一处理调试命令和 PnP 流程。
6. **MOTION_CMD_PICK/PLACE 缺少 XY 移动到吸嘴/贴装位置：** `pick_component()` 和 `place_component()` 直接操作 Z 轴舵机，但调用前需要上层先发送 `MOTION_CMD_MOVE_TO` 到达目标位置。
7. **连续移动（已解决，见 §9.8）：** `Host_Task` 的 `handle_debug_cmd` 已实现完整的 JOG 控制（同步模式+positionMode3Run+motorSyncTrigger）。
8. **R 轴控制：** `r_axis_rotate` 通过 `TMC_SetSpeed`（VACTUAL 寄存器）直接驱动 TMC2209（UART3），已对接。R 轴使用「使能→旋转→关闭」模式，`TMC_Init()` 初始化后驱动默认关闭，`r_axis_rotate` 内部自动使能/关闭。P1/P3 阶段已加入两步闭环角度矫正（详见 §9.16）。详见 HISTORY.md §16.7。
9. **LPUART1 未配置 DMA 接收：** `hdmarx = NULL`，仅用作 TMC2209 半双工阻塞通信。如果该通道用于其他用途需重新配置。
10. **CAN 滤波器配置隐患：** `FilterID2 = 0x1FFC0000` 超出 HAL 11-bit 范围，当前因 `StdFiltersNbr = 0` 恰好全通。CubeMX 重新生成时若 `StdFiltersNbr` ≥ 1，滤波器将仅通过 ID 低 8 位为 0 的帧，所有 CAN 通信中断。修复：将 `FilterID2` 改为 `0x000`，确保 `StdFiltersNbr` ≥ 1。
### 9.4 代码质量
10. **`driver_motor.c runFail/runOK` 死循环：** 两个函数都是 `while(1){}` 空循环，无实际错误处理逻辑。
11. **未使用的全局变量：** `CAN1_0x1fe_Tx_Data` 等 7 个 8 字节数组（共 56 字节）、`CAN_RxDone`、`realTimeLocation` 等，部分来自早期代码残留。注意：`CAN_ID` 在 `canCRC_ATM()` 中有实际使用（CRC 计算），`can_rx_queue` 已删除。
12. **`app_test.h` 与 `app_motion.h` 重复声明（已修复，见 §9.6-5）：** 重复的 `semX1Done`、`evtAxesDone` 等 extern 声明已从 `app_test.h` 移除。
### 9.5 编译与构建
14. **Keil MDK 工程：** 主要使用 MDK-ARM 目录下的 Keil 工程编译。CMakeLists.txt 也可用于构建。
15. **`overflow_count` 唯一声明在 `timestamp.c`：** `timestamp.h` 有 `extern volatile`，`main.c` 通过包含 `timestamp.h` 使用，不得在 main.c 中重复定义。

### 9.6 已完成的架构改进（2026-05）
1. **已创建 `host_pkt_queue`：** 64 深度 `HostMsg_t` 队列，UART 空闲中断回调 → 队列 → Host_Task。修复了原先队列未创建导致 NULL 写入的运行时 Bug。
2. **已添加 `g_debug_mutex` + `DEBUG_CAN_ISR` 条件编译：** 互斥锁保护任务上下文 `PrintDebug` 的静态 `s_debug_buf`，解决多任务并发日志交错。ISR 中 PrintDebug 由 `DEBUG_CAN_ISR` 宏控制（默认关闭），彻底消除 ISR 阻塞 UART 问题。
3. **`StartHostMotionTestTask` 已改为事件驱动：** 原主循环 `vTaskDelay(10ms)` 轮询改为 `osThreadFlagsWait` 阻塞等待。UART 空闲中断通过 `osThreadFlagsSet(hostMotionTaskHandle, ...)` 唤醒任务，延迟从 ≤10ms 降至 <1ms。
4. **`Key_Task` 已改用 `osDelayUntil`：** 原 `osDelay(10)` 改为 `osDelayUntil`，消除任务执行时间导致的周期漂移，保证精确 10ms 扫描间隔。
5. **已删除未使用的 FreeRTOS 对象：** `semX1Done`、`semX2Done`、`semYDone`、`semEmergency`（信号量）和 `can_rx_queue`（队列）已从源码中移除。到位通知统一使用 `evtAxesDone` 事件组。


## 十、快速参考

### 9.7 已完成的改进（2026-06）

> 此三项改进的详细记录已移至 §18（舵机驱动改进记录），此处仅保留索引：
3. TIM5 HAL State 绕过 → HISTORY.md §18.2
2. CubeMX PE8 AF Bug 修复 → HISTORY.md §18.1
1. TIM2 频率调整 → HISTORY.md §18.3


### 9.8 已完成的架构改进（2026-06-10）

**1. Host_Task 启动不再发送 DOWNLOAD_READY：** 上位机协议规定 `DEBUG_MODE` 解锁调试按钮，`DOWNLOAD_READY` 进入文件下载模式。原代码启动时同时发送两者，导致上位机被 `DOWNLOAD_READY` 带入下载模式，调试按钮被重新锁定。修复：启动只发 `DEBUG_MODE\n`，`DOWNLOAD_READY` 仅在下载完成或退出调试时发送。

**2. 命令解析从 ISR 移至任务上下文（UART_PeekData 架构）：** 原架构 `HAL_UARTEx_RxEventCallback(ISR)` → `Host_UartRecvCallback(ISR)` → `LineParser_Feed`(含 `strtof` 浮点) → `host_pkt_queue` → `Host_Task`。Cortex-M4F 在 ISR 中使用 FPU 浮点运算可能导致静默失败，且 `g_parser` 被 ISR 和任务同时使用存在竞态。修复：`Host_UartRecvCallback` 改为空函数（仅保留 `(void)data; (void)len;`），`Host_Task` 主循环改用 `UART_PeekData` + `LineParser_Feed`（全部在任务上下文执行），`host_pkt_queue` 弃用。

**3. move_xy_relative 进入等待循环前加 UART_ClearData：** `UART_PeekData` 只读不清 `data_ready` 标志。外层主循环读完后标志仍置位，导致 `move_xy_relative` 内部等待循环中的 `UART_PeekData` 检测到残留数据，立即返回 -3（误判为中断命令）。修复：等待循环前调用 `UART_ClearData(UART_CH1)` 刷新标志。此 bug 导致 `MOVE_TO`/`SET_ORIGIN` 等阻塞式运动无效。

**4. HAL_TIM_PWM_Start CCER 不生效（改用 CMSIS API）：** 对 TIM2 调用 `HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_3)` 返回 HAL_OK 但 CCER 寄存器维持 0x00，CH3 输出未使能。根因未明确定位（疑似 HAL 库版本或编译优化问题）。修复：使用 CMSIS 级 `TIM_CCxChannelCmd(htim->Instance, TIM_CHANNEL_3, TIM_CCx_ENABLE)` + `__HAL_TIM_ENABLE(htim)`，并正确维护 `htim->State`（设为 BUSY → 操作 → 恢复）。此方案同时兼容 TIM5 的多通道 HAL State 锁问题。`driver_servo.c` 中相关注释已更新。

**5. TMC2209 ENN 引脚为低有效，Host_Task 用 TMC_SetEnable(false) 禁用：** PD15(TMC1_EN) 连接 TMC2209 Pin17 ENN（Enable Not）。LOW=驱动开启，HIGH=驱动关闭。CubeMX 初始化为 LOW（配合 TMC_Init 使用）。`Host_Task` 启动时调用 `TMC_SetEnable(false)` 关闭驱动，防止 R 轴电机在未初始化时发热。

**6. Servo_Init + DRV8803 初始化加入 Host_Task：** 原先缺少 `Servo_Init(&htim2)` 和 `DRV8803_Init()`，导致 `SET_SERVO` 命令无效（`servo_handles[2].initialized=false`），且 Z 轴舵机因 DRV8803 输出状态不确定而发热。修复：Host_Task 启动流程包含 `DRV8803_Init → EnableChip(1) → Servo_Init(&htim2) → SetOutput(12VO4) → Servo_SetAngle(Z_SERVO_CH, 120°)`。

**7. 常量语义化拆分：** 原 `JOG_SPEED`/`JOG_ACC` 被离散移动、MOVE_TO 和 JOG 共用，命名误导。拆分为三层：`DEBUG_SPEED`/`DEBUG_ACC`（MOVE_UP/DOWN/LEFT/RIGHT、MOVE_TO）、`JOG_SPEED`/`JOG_ACC`（MOVE_*_START 连续 JOG）、`PNP_SPEED`/`PNP_ACC`（Mark/找元件/贴装流程），进一步细分为 `PNP_SPEED_FAST`(300, 长距离)、`PNP_SPEED_FINE`(100, 视觉微调)、`PNP_ACC_FINE`(10, 微调加速度)。舵机通道号定义为 `#define Z_SERVO_CH 2`，消除魔法数字。

**8. parse_header 越界修复：** `while (p <= end)` 在空行时 `p == end` 多读一字节。改为 `p < end`。

**9. Vision_Init 重复调用清理：** 原先 `app_freertos.c` 和 `Host_Task` 各调用一次，导致 `[VISION] Init done` 打印两次。已从 `app_freertos.c` 移除。

**10. 主循环冗余 osDelay 清理：** 原主循环 switch 后有额外 `osDelay(5)`，导致 HOST_DEBUG 态每循环 15ms 而非预期的 10ms。已移除。

**11. HOST_DONE/HOST_ERROR 不再自动发 DOWNLOAD_READY：** 任务完成或出错恢复后不再发送 `DOWNLOAD_READY`，避免触发上位机进入文件下载模式锁定调试按钮。

**12. 运动命令不受 g_state 限制：** `handle_debug_cmd` 调用条件从 `if (g_state == HOST_DEBUG)` 改为 `if (cmd != RAW_LINE/NONE/UNKNOWN)`，确保即使状态意外切换（如被 CSV 数据误触 HOST_DOWNLOADING），运动命令仍能正常处理。

**13. Host_UartRecvCallback 重复注释头清理：** 移除旧的 `/* === Host_UartRecvCallback — UART ISR 中调用 === */` 注释块，只保留"已弃用队列模式"版本。

### 9.9 TMC2209 使能/关闭设计（2026-06-11~12）

> 此设计的完整文档已移至 HISTORY.md §16.7，此处仅保留与 TMC2209 使能/关闭主设计无关的边缘说明。

**补充 2 — DRV8803 24V 端口为低端开关：** Port_24VO1(PA6) 等为标准 DRV8803 低端驱动（IN=HIGH→OUT=LOW→负载导通，与 12V 端口逻辑一致）。详见 §10.4。
**补充 1 — VACTUAL 启动扭矩不足：** 直接跳全速时静摩擦卡住电机，需用速度斜坡（5000→80000 μstep/s，每级 +8000，40ms/级）。详见 HISTORY.md §16.3。

### 9.10 UART DMA 架构改进（2026-06-12）

**背景：** 上位机输出日志重复、命令被多次执行。经排查发现三重根因：

#### 根因 1：ISR 内立即重启 DMA 与任务侧竞态

原 `HAL_UARTEx_RxEventCallback` ISR 在置 `data_ready` 后立即调用 `UART_StartReceive_DMA` 重启 DMA。但此时 `UART_Driver_Process` 尚未拷贝数据，DMA 缓冲区被新传输覆盖；同时 `HAL_UART_DMAStop` 可能触发虚假空闲中断，改写 `data_len`。

**修复：** ISR 行为按通道可配置。引入 `UART_Channel_t.isr_restart` 字段：

| 通道 | `isr_restart` | 策略 |
|------|--------------|------|
| UART_CH1 (上位机) | `false` | ISR 不重启 DMA，由 `UART_Driver_Process` 统一重启 |
| UART_CH2 (摄像头) | `true` | ISR 立即重启，保证帧数据不丢 |
| UART_CH3 (TMC2209) | `true` | ISR 立即重启 |
| UART_CH4 (LPUART1) | `true` | ISR 立即重启（预留） |

初始化在 `UART_Driver_Init()` 中显式赋值，长期维护只需改一行。

#### 根因 2：data_ready/data_len 被 ISR 和任务共享

原架构 `UART_Driver_Process` 拷贝数据后不清 `data_ready`，依赖外部 `UART_ClearData` 来清。PrintDebug 输出的 TX→RX 硬件环回触发 ISR 在 `UART_PeekData` 和 `UART_ClearData` 之间修改 `data_len`，导致 app buffer 内容和长度字段脱钩。

**修复：** 引入独立字段 `rx_app_len`（任务侧，ISR 不触及）。

```
ISR 侧                任务侧（UART_Driver_Process 临界区内原子操作）
data_ready ─┐         ┌─ rx_app_len（任务只读）
data_len   ─┤  解耦   ├─ rx_app_buf
            ┘         └─ 拷贝 + 清零 data_ready/data_len
```

`UART_PeekData` / `UART_HasData` / `UART_GetRxCount` 全部改用 `rx_app_len`。
`UART_ClearData` 同时清零 `rx_app_len`、`data_ready`、`data_len`，兼容 `move_xy_relative` 的中断检测。

#### 根因 3：CAN TX 调试打印刷屏

`driver_can.c:139` 的 TX 调试打印被 `#ifdef DEBUG_CAN_ISR` 包裹，但 Keil 工程可能定义了该宏。每次 CAN 发送产生 3 行日志，一个 JOG 指令触发 ~9 次 CAN TX = 27 行日志。

**修复：** `#ifdef DEBUG_CAN_ISR` → `#if 0`，永久禁用。

#### 涉及文件

| 文件 | 改动 |
|------|------|
| `driver_uart.c` | `rx_app_len` 字段 + `isr_restart` 字段 + `UART_Driver_Process` 原子消费 + `UART_ClearData` 三清 |
| `driver_can.c` | CAN TX 调试 `#if 0` |


### 9.11 命令处理架构改进（2026-06-12）

#### 状态去重

`handle_debug_cmd` 入口加入状态去重：连续两次 cmd+param 完全相同的命令直接丢弃。

```
MOVE_DOWN_START 10 → 执行 (g_last=MOVE_DOWN_START, param=10)
MOVE_DOWN_START 10 → 丢弃 (cmd==g_last && param==g_last_param)
MOVE_STOP          → 执行 (cmd!=g_last，自动复位)
```

相比时间窗口防抖，不依赖 tick 精度，中间有其他命令介入自动复位。

#### RAW_LINE 回显过滤

主循环中 RAW_LINE 处理增加四重过滤，防止 PrintDebug 输出和握手消息的 TX→RX 环回误触发下载模式：

1. 命令执行期间（`g_during_cmd=true`）的 RAW_LINE → 直接 `continue`
2. 以 `[` 开头的行（PrintDebug 回显）
3. 精确匹配 `DEBUG_MODE`（10 字节）
4. 精确匹配 `DOWNLOAD_READY`（14 字节）
5. 精确匹配 `EXIT_DEBUG_MODE`（15 字节）

#### 离散移动合并

`HCMD_MOVE_UP/DOWN/LEFT/RIGHT` 四个 case 合并为 fall-through + 静态查表驱动，同时加入 `move_xy_relative` 返回值检测（中断时日志标记 INTERRUPTED）。

#### 涉及文件

| 文件 | 改动 |
|------|------|
| `app_host.c` | 状态去重 + `g_during_cmd` 守卫 + 四重过滤 + 离散移动合并 |


### 9.12 泵阀控制语义化（2026-06-12）

**背景：** 电磁阀实际硬件行为为 PA6=HIGH→导通（标准 DRV8803），与早期文档记录相反。`DRV8803_Init` 中 12V/24V 端口统一初始化为 LOW（关断）。

**语义化接口：** 在 `driver_drv8803.h` 中新增内联函数：

```c
static inline void Pump_On(void)   { DRV8803_SetOutput(&Port_12VO1, true);  }
static inline void Pump_Off(void)  { DRV8803_SetOutput(&Port_12VO1, false); }
static inline void Valve_On(void)  { DRV8803_SetOutput(&Port_24VO1, true);  }  // PA6=HIGH→导通
static inline void Valve_Off(void) { DRV8803_SetOutput(&Port_24VO1, false); }  // PA6=LOW→关断
```

**PUMP_OFF 时序：** 关泵 → Valve_On() → osDelay(1000ms) → Valve_Off()。其余时间阀常关。

**涉及文件：**
| 文件 | 改动 |
|------|------|
| `driver_drv8803.h` | 新增 Pump_On/Off、Valve_On/Off 内联函数 |
| `driver_drv8803.c` | `DRV8803_Init` 12V/24V 统一初态 LOW |
| `app_host.c` | `handle_debug_cmd` PUMP_ON/OFF 改用语义化接口 |
| `app_test.c` | 所有 BSRR 直写 PA6 改为 `DRV8803_SetOutput(VALVE_PORT, ...)` |


### 9.13 CAN 中断激活 + 轴映射 + 精准速度修复（2026-06-12）

#### 根因 1：CAN RX 中断未正确激活

can_filter_mask_config() 中 HAL_FDCAN_ActivateNotification 在 HAL_FDCAN_Start **之前**调用，而 CAN_Init 中 **之后**的那次调用被注释掉。结果是 CAN RX 中断可能未正确使能，电机到位反馈（0xF5 + Status=0x02）无法被接收。

**修复：**

| 文件 | 改动 |
|------|------|
| `driver_can.c` | can_filter_mask_config 移除 HAL_FDCAN_ActivateNotification，仅保留滤波器配置 |
| driver_can.c | CAN_Init 中取消注释 HAL_FDCAN_Start 之后的 HAL_FDCAN_ActivateNotification |
| app_test.c | 移除 StartCamTestTask、vMotorTestTask 中冗余的手动 HAL_FDCAN_ActivateNotification（CAN_Init 内部已处理） |

正确初始化顺序：can_filter_mask_config（配置滤波器，CCE=1）→ HAL_FDCAN_Start（state → BUSY）→ HAL_FDCAN_ActivateNotification（使能中断，state=BUSY）.

#### 根因 2：main.c 中无效的 Motor_Init()

main.c:131 在 RTOS 调度器启动前和 CAN 启动前调用 Motor_Init(). 此时 hfdcan1.State = READY（非 BUSY），HAL_FDCAN_AddMessageToTxFifoQ 返回 HAL_ERROR（NOT_STARTED），所有 CAN 帧静默失败.

**修复：** 移除该调用，替换为注释。各任务在自身初始化阶段调用 CAN_Init + Motor_Init.

#### 轴对应关系

| 物理轴 | 程序变量 | 驱动轴 | 电机 |
|--------|---------|--------|------|
| X | dy / cur_y | Y 轴 | 单电机 (ID=0x03) |
| Y | dx / cur_x | X1+X2 双轴 | 双电机同步 (ID=0x01, 0x02) |

此映射关系在 cam_test_run 的 P1/P3 偏移计算中已体现（轴映射相同，符号因上下相机而异，详见 §9.14）：

```c
// P1（上相机，镜头朝下）：两轴均取反
dx_s = -(r->dy * CAM_PX_TO_STEPS);  // 摄像头 Y → X1+X2（物理 Y）
dy_s = -(r->dx * CAM_PX_TO_STEPS);  // 摄像头 X → Y 电机（物理 X）

// P3（下相机，镜头朝上）：r->dx 取反（dy_s 为负），r->dy 不取反（dx_s 为正）
dx_s =  (r->dy * CAM_PX_TO_STEPS);  // 摄像头 Y → X1+X2（物理 Y）
dy_s = -(r->dx * CAM_PX_TO_STEPS);  // 摄像头 X → Y 电机（物理 X）
```

#### 视觉闭环精准速度

CAM_MOVE_SPEED 从 300 RPM（25 mm/s）降至 **100 RPM（≈8.3 mm/s）**，配合视觉迭代对齐使用，减少过冲。

#### 涉及文件

| 文件 | 改动 |
|------|------|
| driver_can.c | CAN 中断激活移到 HAL_FDCAN_Start 之后 |
| main.c | 移除无效 Motor_Init() |
| app_test.c | 移除冗余 HAL_FDCAN_ActivateNotification ×2 + 轴映射交换 + CAM_MOVE_SPEED=100 |



### 9.14 move_xy_relative 事件组架构 + 轴映射修正（2026-06-13）

**背景：** StartCamTestTask 中电机到位但 move_xy_relative 始终超时（ret=-1）。经三重根因排查后彻底修复。

#### 根因 1：CAN_Process_Task 与 move_xy_relative 队列竞争

`CAN_Process_Task` 使用 `osWaitForever` 阻塞读取 `motor_event_queue`，`move_xy_relative` 原先直接轮询同一队列（0 超时）。前者优先级相同但总先抢到到位帧，后者永远看不到 0xF5+0x02 完成包。

**修复：** `move_xy_relative` 改为通过 `osEventFlagsGet(evtAxesDone)` 非破坏性读事件组标志，不再直接访问队列。`CAN_Process_Task` 负责消费队列并设标志，`move_xy_relative` 只读标志，分工明确无竞争。

#### 根因 2：osFlagsWaitAny auto-clear 丢失标志

`osEventFlagsWait(evtAxesDone, ..., osFlagsWaitAny, ...)` 会在返回时 auto-clear **所有**已设置的匹配标志。三轴到位顺序不定：若 Y 先到位设 EVENT_Y_DONE，然后 X1 到位，Wait 返回时同时清除两者。Y 只发一次到位帧，标志被偷后再也不会回来，accum 凑不齐 `EVENT_ALL_AXES`。

**修复：** 放弃 `osEventFlagsWait`，改用 `osEventFlagsGet` — 非破坏性读取，标志不会被清除。轮询 + 真实时钟超时。

#### 根因 3：位置移动方向反转 + 轴映射未交换

摄像头偏移方向与电机运动方向相反（摄像头 dx=35，电机 +dx 移动后 dx=36，偏移反而变大），且程序轴（X1+X2=物理Y、Y电机=物理X）未做交换。

**最终映射公式（app_test.c cam_test_run）：**

P1（上相机，镜头朝下）和 P3（下相机，镜头朝上）使用**相同的轴映射**，但 **dx_s 符号不同**：

```c
// P1（上相机）：两轴均取反
dx_s = -(int32_t)(r->dy * CAM_PX_TO_STEPS);
dy_s = -(int32_t)(r->dx * CAM_PX_TO_STEPS);

// P3（下相机）：r->dx 取反（dy_s 为负），r->dy 不取反（dx_s 为正）（2026-06-13 经多次实测验证）
dx_s =  (int32_t)(r->dy * CAM_PX_TO_STEPS);
dy_s = -(int32_t)(r->dx * CAM_PX_TO_STEPS);
```

**物理原因：** 上相机随龙门移动、镜头朝下；下相机固定、镜头朝上。上下视角的 Y 轴镜像关系相反，导致 P3 的 Y 轴补偿方向与 P1 相反（不取反）。

**P2（Mark 建系）路径同步修正：**
```c
dx_s = -(int32_t)(r->dy * CAM_MM10000_TO_STEPS);
dy_s = -(int32_t)(r->dx * CAM_MM10000_TO_STEPS);
```

**要点：** （1）轴映射 — cam.dx→Y电机(dy_s)，cam.dy→X1+X2(dx_s)，P1/P2/P3 三者一致；（2）P1/P2 两轴取反（dx_s/dy_s 均为负），P3 仅 r->dx 取反（dy_s 为负）、r->dy 不取反（dx_s 为正）。

#### move_xy_relative 当前架构

```
osEventFlagsClear → 发 CAN 命令 → while(真实时钟未超时) {
    flags = osEventFlagsGet(evtAxesDone);  // 非破坏性读
    if (flags & ERROR) → return -2;
    if ((flags & done_mask) == done_mask) → return 0;
    UART_Driver_Process + 中断命令检测;
    osDelay(100);
}
```

**超时机制：** 使用 `osKernelGetTickCount()` 真实时钟（10s），而非计数器累加。计数器在标志已设时会在微秒级烧完迭代预算。

**诊断日志：** `[MOVE]` 和 `[CAN_PROC]` 日志受 `#ifdef DEBUG_MOVE` / `#ifdef DEBUG_CAN_PROC` 控制（app_test.c 头部定义）。注释掉 define 即可静默。

#### 涉及文件

| 文件 | 改动 |
|------|------|
| app_test.c | move_xy_relative：队列轮询→事件组 osEventFlagsGet + 真实时钟 + 文档注释 |
| app_test.c | cam_test_run P1/P2：两轴取反；P3：r->dx 取反（dy_s 为负），r->dy 不取反（dx_s 为正），因下相机镜头朝上 |
| app_host.c | find_comp_step / mark_align_step / offset_check_step 同步修正为相同轴映射（§9.16） |
| app_test.c | 新增 `#define DEBUG_MOVE` / `#define DEBUG_CAN_PROC` 诊断开关 |
| app_motion.c | CAN_Process_Task：到位帧日志加 `#ifdef DEBUG_CAN_PROC` 保护 |

### 9.15 加热台协议重构（2026-06-17）

**背景：** 加热台从机通信协议规范化（参考 `AGENTS_HEATER.md`），原 `driver_heater.c/h` 使用临时 CAN ID（0x10/0x11）和错误的状态/错误码定义，且未按协议规范处理 CRC。

**改动内容：**

| 改动项 | 旧值 | 新值 |
|--------|------|------|
| CAN ID 命令/状态 | 0x10 / 0x11 | 0x04 / 0x05 |
| 状态码 | 7 个（STANDBY~ERROR） | 6 个（IDLE/HEATING/HOLDING/COOLING/COMPLETE/ERROR） |
| 错误码 | SENSOR/OVERTEMP/TIMEOUT | THERMOCOUPLE/OVERTEMP/COMM_TIMEOUT |
| 命令 CRC | 无（依赖 CAN_Transmit_Data 自动 CRC） | START/STOP/QUERY 无 CRC；SET_TEMP/SET_PID 附加 CRC（纯数据累加，不含 CAN ID） |
| 发送函数 | 通过 CAN_Transmit_Data（电机 CRC 规则） | 独立 Heater_Transmit() 直接调用 HAL_FDCAN_AddMessageToTxFifoQ |
| 接收过滤 | `pkt.FuncCode == HEATER_STATUS_ID`（逻辑错误） | `pkt.ID == HEATER_STATUS_ID`（纯 CAN ID 过滤） |
| 新增命令 | — | `Heater_SendQuery()` (0x05) |

**CAN RX 路由（driver_can.c）：** `HAL_FDCAN_RxFifo0Callback` 中新增加热台帧分发：
```c
if (pkt.ID == HEATER_STATUS_ID && heater_rx_queue != NULL) {
    osMessageQueuePut(heater_rx_queue, &pkt, 0, 0);
}
```
同时补充了此前遗漏的 `pkt.DataLength = header.DataLength` 赋值。

**温度打印修复：** `Heater_ProcessStatus` 中新增 `print_temp()` 辅助函数，先取绝对值再格式化，避免 C 语言负数除/取模歧义（`-5/10=0` 导致 `-0.5°C` 错误显示为 `0.5`）。

**波特率确认：** 加热台与电机共享 FDCAN1（1 Mbps），加热台从机侧已同步调整为 1 Mbps。

**涉及文件：**

| 文件 | 改动 |
|------|------|
| `driver_heater.h` | CAN ID/状态码/错误码/命令码宏全面重写，新增 Heater_SendQuery 声明 |
| `driver_heater.c` | 新增 Heater_Transmit 原生发送 + 全部命令函数重写 + ProcessStatus 修复 + print_temp 辅助函数 + 死代码移除 |
| `driver_can.c` | 加热台 ID=0x05 路由 + DataLength 赋值 |
| `AGENTS.md` | §4.4 新增加热台协议文档 + 硬件表/目录结构/任务通信/数据结构表更新 |


### 9.16 R 轴两步闭环角度矫正 + Host_Task 坐标映射修正（2026-06-19）

**背景：** 原先 Host_Task 和 StartCamTestTask 中 R 轴角度矫正逻辑存在两个问题：（1）P1 检测到的元件角度未被用于矫正，仅打印日志；（2）P3 残余角度也未做二次精修。此外 Host_Task 的三个视觉 step 函数使用直接坐标映射（dx→X, dy→Y），与 cam_test_run 中已验证的摄像头→电机轴映射（cam Y→X1+X2, cam X→Y 电机）不一致。

#### R 轴两步闭环矫正流程

```
P1 检测角度 → 吸取 → [矫正1: r_axis_rotate(p1_angle)] → 移至下相机
  → P3 验证 → [矫正2: 若 |residual| > 阈值 → r_axis_rotate(residual)]
  → 贴装/释放
```

**设计要点：**
- 矫正 1（吸取后）：摄像头检测到元件偏角后先吸取，待真空稳定后再旋转补偿。吸嘴带着元件旋转而非空吸嘴旋转，避免空吸嘴旋转后元件相对吸嘴滑动。Host_Task 的矫正 1 位于 pick_step 吸取成功后、进入 P3 之前；StartCamTestTask 同理。
- 矫正 2（P3 后）：下相机验证矫正结果。若仍有残余偏角（机械公差、吸取偏移导致），执行二次精修。
- 两次矫正使用相同的阈值 `R_CORRECTION_THRESHOLD_DEG`（0.1°）和转速 `R_SPEED_RPM`（60 RPM）。

#### host_correct_r_from_vision 辅助函数

Host_Task 中提取了公共辅助函数（[app_host.c](E:/Desktop/qiansai/pnp_1/Task/app_host.c:782)），消除 find_comp_step 和 offset_check_step 中的重复代码：

```c
static bool host_correct_r_from_vision(const VisionResult_t *r, const char *stage) {
    if (!r || !r->angle_valid) return false;          // 空指针 + 有效性守卫
    float ang = (float)r->angle_x100 / 100.0f;
    if (fabsf(ang) <= R_CORRECTION_THRESHOLD_DEG) return false;  // 低于阈值跳过
    PrintDebug("[HOST] %s: R correction %.2f deg\r\n", stage, (double)ang);
    r_axis_rotate(ang, R_SPEED_RPM);
    return true;
}
```

调用点：
- pick_step pick_component() 成功后 → host_correct_r_from_vision(Vision_GetResult(), "P1") → HOST_MOVE_TO_BOTTOM_CAM (P1 矫正已从 VISION_DONE 移至吸取后)
- `offset_check_step` VISION_DONE → `host_correct_r_from_vision(r, "P3")` → HOST_MOVE_TO_PCB

#### Host_Task 坐标映射修正

三处视觉 step 函数的坐标映射均已修正为与 cam_test_run 一致的摄像头→电机轴映射（§9.14）：

| 函数 | 进程 | 修正前 | 修正后 |
|------|------|--------|--------|
| `find_comp_step` | P1 | `dx_s = r->dx * coeff` | `dx_s = -(r->dy * coeff)` — cam Y→X1+X2, 取反 |
| | | `dy_s = r->dy * coeff` | `dy_s = -(r->dx * coeff)` — cam X→Y 电机, 取反 |
| `mark_align_step` | P2 | `dx_s = r->dx / 10000 * steps` | `dx_s = -(r->dy / 10000 * steps)` |
| | | `dy_s = r->dy / 10000 * steps` | `dy_s = -(r->dx / 10000 * steps)` |
| `offset_check_step` | P3 | `dx_s = r->dx * 0.1f` | `dx_s = (r->dy * 0.1f)` — cam Y→X1+X2, 不取反 |
| | | `dy_s = r->dy * 0.1f` | `dy_s = -(r->dx * 0.1f)` — cam X→Y 电机, 取反 |

#### 共享常量治理

| 常量 | 定义位置 | 说明 |
|------|----------|------|
| `R_CORRECTION_THRESHOLD_DEG` (0.1f) | `app_config.h` | 新增。R 轴矫正最小角度阈值，host + test 共用 |
| `R_SPEED_RPM` (60.0f) | `app_config.h` | 从 `app_host.h` 迁移。R 轴矫正/贴装转速，host + test 共用 |

#### StartCamTestTask 测试流程更新

初始化流程对齐 Host_Task（DRV8803→舵机上电→Valve_Off→TMC→Calib_Load→舵机安全角→CAN→Motor→Vision→P0握手）。
测试流程激活完整闭环：P2 Mark 建系 → P1 检测 → pick_component 吸取 → R 矫正1 → 移至下相机 → P3 验证 → R 矫正2 → 释放。执行顺序为 P2→P1→P3（先建系再找元件），与 Host_Task 一致。

#### 涉及文件

| 文件 | 改动 |
|------|------|
| `app_config.h` | 新增 `R_CORRECTION_THRESHOLD_DEG`；移入 `R_SPEED_RPM` |
| `app_host.h` | 移除 `R_SPEED_RPM`（已迁至 app_config.h） |
| `app_host.c` | 新增 `host_correct_r_from_vision` 辅助函数；find_comp_step/offset_check_step VISION_DONE 调用辅助函数；find_comp_step/mark_align_step/offset_check_step VISION_GOT_POS 坐标映射修正 |
| `app_test.c` | StartCamTestTask 初始化流程对齐；P1/P2/P3 测试流程激活；R 轴两步矫正逻辑；硬编码 0.1f/60.0f 替换为命名常量；cam_test_run 移动调用改为 safe_move_to |


### 9.17 上位机↔电机坐标系约定 + PrintDebug 显示统一（2026-06-22）

**背景：** 点动模式下 MOVE_UP 打印 `(153,0)` 而非预期的 `(0,153)`，同时 MOVE_TO (5,5) 实际走到 (-5,5)。排查发现上位机坐标系（X=水平、Y=垂直）与电机坐标系（X电机=垂直、Y电机=水平）之间存在 90° 旋转 + 水平轴符号翻转，部分代码路径未正确处理。

#### 坐标系约定

```
上位机坐标系（host）：        电机坐标系（motor）：
     Y+ (上)                      X电机+ (上)
      ↑                            ↑
      |                            |
      +--→ X+ (右)                +--→ Y电机+ (左)

映射关系：
  host_x = -motor_y      （Y电机正转 = 机器向左 = 上位机 X 负方向）
  host_y = +motor_x      （X电机正转 = 机器向上 = 上位机 Y 正方向）

逆映射：
  motor_x = +host_y
  motor_y = -host_x
```

#### 离散移动方向验证

`handle_debug_cmd` 离散移动查表存储的是**电机轴偏移量**（sx=X电机符号, sy=Y电机符号）：

| 方向 | 表值 | 电机动作 | 上位机等效 | 正确？ |
|------|------|---------|-----------|--------|
| MOVE_UP | {+1, 0} | X电机+ | host Y+ | ✓ |
| MOVE_DOWN | {-1, 0} | X电机- | host Y- | ✓ |
| MOVE_LEFT | {0, +1} | Y电机+ | host X- | ✓ |
| MOVE_RIGHT | {0, -1} | Y电机- | host X+ | ✓ |

JOG 连续移动（MOVE_*_START）直接控制对应电机，方向一致。

#### MOVE_TO Bug 修复

[app_host.c](E:/Desktop/qiansai/pnp_1/Task/app_host.c:537) MOVE_TO 原先的映射：

```c
// 修复前：
tx = cmd->param2 * STEPS;   // host Y → X电机 (正确)
ty = cmd->param  * STEPS;   // host X → Y电机 (缺取反！)
// MOVE_TO 5 5 → 实际走到 (-5, 5)
```

```c
// 修复后：
tx = cmd->param2 * STEPS;   // host Y → X电机 (同号)
ty = -cmd->param * STEPS;   // host X → Y电机 (取反)
// MOVE_TO 5 5 → 正确走到 (5, 5)
```

#### PrintDebug 显示统一

将所有显示 `Coord_Get().x / .y` 的 PrintDebug 统一转换为上位机坐标系：`(-motor_y, motor_x)`。

涉及位置：

| 行号 | 命令/函数 | 改动 |
|------|----------|------|
| ~481,484 | 离散移动 (MOVE_UP/DOWN/LEFT/RIGHT) | `(x,y)` → `(-y,x)` |
| ~541 | MOVE_TO | `(x,y)` → `(-y,x)` + ty 取反 |
| ~382 | SET_SCATTER_AREA | `(x,y)` → `(-y,x)` |
| ~391 | SET_HEATER_PLATFORM_MIN | `(x,y)` → `(-y,x)` |
| ~396 | SET_HEATER_PLATFORM_MAX | `(x,y)` → `(-y,x)` |
| ~401 | SET_BOTTOM_CAM | `(x,y)` → `(-y,x)` |

#### 标定数据存储说明

标定 SET 命令（SET_SCATTER_AREA、SET_BOTTOM_CAM 等）**存入 Flash 的是电机坐标**（`Coord_Get().x/y` 原始值）。后续 PnP 流程从 Flash 读出后直接传给 `safe_move_to`，后者也接受电机坐标。全过程无坐标系转换，存取闭环正确。PrintDebug 仅显示层转换，不影响存储值。

#### 摄像头→电机映射（不受影响）

P1/P2/P3 视觉 step 函数使用独立的摄像头→电机映射（§9.14, §9.16），与本次修改的 `handle_debug_cmd` 和 MOVE_TO 路径物理隔离，互不干扰。

#### 涉及文件

| 文件 | 改动 |
|------|------|
| `app_host.c` | `handle_debug_cmd`：离散移动 PrintDebug 坐标转换 + MOVE_TO ty 取反 + MOVE_TO PrintDebug 坐标转换 |
| `app_host.c` | `handle_calib_cmd`：四个标定 SET 命令 PrintDebug 坐标转换 |
| `AGENTS.md` | §9.17 新增，HISTORY.md §27.8 坐标映射条目修正 |


### 9.18 R 轴 KTH7823 闭环控制（2026-07-11~12）

**背景：** 原先 R 轴使用 TMC2209 VACTUAL 速度模式开环控制（定时盲估到位），无位置反馈。
新增 KTH7823 14-bit 磁编码器实现闭环 PID 位置控制。

**硬件：** KTH7823 PWM 输出 (910Hz, 14-bit) → PB2 (TIM5_CH1 输入捕获, PSC=0, 170MHz)。
PB2 原先为 DRV8803 Port_24VO4 的 PWM 引脚，已让出（Port_24VO4 改为仅开关模式）。

**TIM5 共用：** CH1=输入捕获 (KTH7823) + CH3=PWM 输出 (舵机)。
PSC 从 169 降至 0 → 舵机 ARR 从 19999 变为 3,399,999，舵机驱动 pulse 字段已用 uint32_t 适配。

**软件架构：**

| 层 | 文件 | 职责 |
|----|------|------|
| 驱动层 | driver_kth7823.c/h | PWM 双边沿输入捕获 ISR + 角度公式解算 (910Hz 更新率) |
| 控制层 | pp_motion.c _axis_rotate() | 闭环 PID (位置式, static 复用) + 解绕 (unwrap) 角度处理 |
| 配置层 | pp_config.h | R_CLOSED_KP/KI/KD/MAX_SPEED/THRESHOLD/TIMEOUT/KICK_MS/LOOP_MS |

**闭环流程：** _axis_rotate(target) → TMC2209 使能 → 初始方向开环起步 10ms →
PID 循环 (5ms 周期): 读编码器角度 → 解绕到 target±180° → PID 计算速度 → TMC_SetSpeed →
|error|<0.2° 到位停机 → TMC2209 关断。

**r_axis_set_zero：** 调用 KTH7823_WaitData(100ms) 等待首个有效编码器读数，记录为 g_r_encoder_zero_offset。后续所有角度 = 编码器原始值 - offset → 软件可任意指定零点。

**关键设计决策：**
- PID 实例为函数内 static，仅首次 PID_Init，后续 PID_Reset + PID_SetParams 复用，避免 FreeRTOS mutex 堆泄漏
- 输入滤波器 ICFilter=8 (~47ns @170MHz)，在 KTH7823_Init 中直接写 TIM5->CCMR1 覆写 CubeMX 默认值 0
- GetAngle() 先快照 ngle_deg 再清 data_ready，避免 ISR 并发导致的数据不一致
- 首次编码器读数用于 PID 历史装入 (MeasurementPrev)，消除首拍 D-term 尖峰
- 3s 超时保护 → 返回 -2，防止编码器断线时死循环阻塞 PnP 流程

**新增文件：** Drivers/ZeMCU-G4/driver_kth7823.c/h

**修改文件：**

| 文件 | 改动 |
|------|------|
| Core/Src/tim.c | TIM5 PSC=0, Period=3,399,999, CH1 IC + CH3 PWM |
| Core/Src/stm32g4xx_it.c | TIM5_IRQHandler (CubeMX 生成) |
| Drivers/ZeMCU-G4/driver_drv8803.c | Port_24VO4 移除 PB2, num_pins=1 |
| Task/app_config.h | 新增 8 个 R_CLOSED_* PID 参数常量 |
| Task/app_motion.c | _axis_rotate() 闭环 PID; _axis_set_zero() 编码器零点 |
| Task/app_host.c | KTH7823_Init() 调用 + 错误检查; host_correct_r_from_vision 阈值统一 |

**已知限制：**
- PID 参数 (KP=2.5, KI=0.05, KD=0.3) 为理论值，需实机整定
- _axis_rotate 返回值未在调用方检查（与原开环行为一致，PnP 状态机暂无 R 轴超时恢复路径）
- R_CORRECTION_THRESHOLD_DEG (0.1°) 仍定义但已无引用，被 R_CLOSED_THRESHOLD (0.2°) 统一替代


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
| KTH7823 编码器 | PB2 | TIM5_CH1 (输入捕获) |
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
| `Port_24VO4` | PC5 | — | — | 24V 开关输出 (PB2 已让给 KTH7823) |

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
**故障排查：**

| 现象 | 检查项 |
|------|--------|
| 12V 端口不工作 | PE9(EN1) 是否为 LOW |
| 24V 端口不工作 | PA4(EN2) 是否为 LOW、PB0(RST2) 是否为 LOW |
| 电磁阀不动 | PA6 是否有 HIGH→LOW 跳变、U13 是否使能、24V 供电是否正常 |
| 舵机不转 | PB10 是否有 50Hz PWM、PE14(开关) 是否 HIGH |


