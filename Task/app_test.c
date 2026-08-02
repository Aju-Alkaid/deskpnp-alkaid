#include "driver_uart.h"  
#include "FreeRTOS.h"     
#include "task.h"         
#include <string.h>       // 用于 strlen
#include <stdio.h>        // 用于 sprintf (可选)
#include "driver_tmc2209.h"  
#include <stdarg.h>
#include "driver_drv8803.h"
#include "driver_servo.h"
#include "app_test.h"
#include "driver_can.h"
#include "app_motion.h"
#include <math.h>   // 解决 fabsf 未声明
#include "app_uart_parser.h"
#include "app_vision.h"
#include "driver_spiflash_w25q64.h"
#include "app_host.h"
#include "app_esp_task.h"
#include "app_esp_protocol.h"
#include "driver_esp32.h"

// 简单的步进脉冲生成函数 (需根据你的GPIO定义修改)
#define STEP_GPIO_PORT GPIOD
#define STEP_PIN GPIO_PIN_12
#define DIR_GPIO_PORT GPIOD
#define DIR_PIN GPIO_PIN_13

/* 测试任务参数 */
#define DRV8803_TEST_PERIOD_MS  500   // 通道循环切换间隔
/* 舵机测试任务参数 */
#define SERVO_TEST_CH            2            // PE8 → TIM5_CH3 → 通道索引 2
#define SERVO_SWING_STEP_DEG     0.5f         // 每步角度增量
#define SERVO_SWING_PERIOD_MS    10           // 每步延时(ms)，决定运动速度


/* ---- 诊断日志开关（调试完成后注释掉即可关闭） ---- */
#define DEBUG_MOVE         // move_xy_relative 到位/超时日志
#define DEBUG_CAN_PROC     // CAN_Process_Task 到位帧日志

/* ---- 上位机运动测试 常量 ---- */


/* 外部变量：CAN接收队列（已在 driver_can.c 中定义） */
extern osMessageQueueId_t motor_event_queue;
extern osMutexId_t g_debug_mutex;
extern osThreadId_t hostMotionTaskHandle;
extern UART_HandleTypeDef huart1; 
extern UART_HandleTypeDef huart3;
extern TIM_HandleTypeDef htim5;
extern TIM_HandleTypeDef htim2;
static char s_debug_buf[128]; 


/**
 * @brief 轻量格式化核心，替代 vsnprintf，栈占用约 80 字节
 * @note  支持: %d %ld %u %.1f %.2f %02X %s %.*s 及纯文本（含 UTF-8 中文）
 */
static int dbg_vformat(char* buf, int sz, const char* fmt, va_list args) {
    static const int pow10[] = {1, 10, 100, 1000, 10000, 100000, 1000000};
    int pos = 0;
    char ch;

    while ((ch = *fmt++) && pos < sz - 1) {
        if (ch != (char)0x25) { buf[pos++] = ch; continue; }  /* 0x25='%' */

        ch = *fmt++;
        int zero = 0, prec = -1;

        if (ch == '0') { zero = 1; ch = *fmt++; }
        if (ch >= '0' && ch <= '9') { ch = *fmt++; }  /* 跳过宽度位 (如 %02X 的 2) */
        if (ch == '.') {
            ch = *fmt++;
            if (ch == '*') { prec = va_arg(args, int); ch = *fmt++; }
            else { prec = 0; while (ch >= '0' && ch <= '9') { prec = prec * 10 + (ch - '0'); ch = *fmt++; } }
        }
        if (ch == 'l') { ch = *fmt++; }

        if (ch == 'd' || ch == 'u') {
            int v = va_arg(args, int);
            if (v < 0 && ch == 'd') { buf[pos++] = '-'; v = -v; }
            char tmp[12]; int t = 0;
            do { tmp[t++] = (char)('0' + (v % 10)); v /= 10; } while (v);
            while (t--) buf[pos++] = tmp[t];
        }
        else if (ch == 's') {
            const char* s = va_arg(args, const char*);
            if (s) {
                int n = (prec >= 0) ? prec : (sz - pos - 1);
                while (n-- && *s && pos < sz - 1) buf[pos++] = *s++;
            }
        }
        else if (ch == 'X') {
            unsigned int v = va_arg(args, unsigned int) & 0xFF;
            if (zero) { buf[pos++] = "0123456789ABCDEF"[(v >> 4) & 0xF]; }
            buf[pos++] = "0123456789ABCDEF"[v & 0xF];
        }
        else if (ch == 'f') {
            double fv = va_arg(args, double);
            int p = (prec < 0) ? 1 : prec;
            if (p > 6) p = 6;
            if (fv < 0.0) { buf[pos++] = '-'; fv = -fv; }
            int ip = (int)fv;
            int fp = (int)((fv - (double)ip) * (double)pow10[p] + 0.5);
            if (fp >= pow10[p]) { ip++; fp -= pow10[p]; }
            char tmp[12]; int t = 0;
            do { tmp[t++] = (char)('0' + (ip % 10)); ip /= 10; } while (ip);
            while (t--) buf[pos++] = tmp[t];
            buf[pos++] = '.';
            for (int i = p - 1; i >= 0; i--) buf[pos++] = (char)('0' + ((fp / pow10[i]) % 10));
        }
        else if (ch == '\0') { break; }
        else { buf[pos++] = '%'; buf[pos++] = ch; }
    }
    buf[pos] = '\0';
    return pos;
}

/* ================================================================
 *  MSCNT 原始值采样诊断 — 定速旋转, 每 20ms 读一次 MSCNT, 运行 5s
 *  用法: 串口发送 MSCNT_TEST<CR>
 * ================================================================ */
void MSCNT_Test(void) {
    PrintDebug("[MSCNT-TEST] start: enable TMC2209, speed=5000 Hz\r\n");

    TMC_SetEnable(true);
    vTaskDelay(pdMS_TO_TICKS(TMC_ENABLE_DELAY_MS));

    /* ?? 5000 Hz (?5.9 RPM): ? 20ms ? 100 ?, MSCNT ?? 0~1023 ????? */
    TMC_SetSpeed(5000);
    vTaskDelay(pdMS_TO_TICKS(10));

    uint16_t raw, prev;
    if (TMC_GetMSCNT(&raw) != TMC_ERR_NONE) {
        PrintDebug("[MSCNT-TEST] ERR: first MSCNT read failed\r\n");
        TMC_SetSpeed(0);
        TMC_SetEnable(false);
        return;
    }
    prev = raw;

    uint32_t t0 = HAL_GetTick();
    int count = 0;

    while (HAL_GetTick() - t0 < 5000) {
        vTaskDelay(pdMS_TO_TICKS(20));
        if (TMC_GetMSCNT(&raw) != TMC_ERR_NONE) {
            PrintDebug("[MSCNT-TEST] ERR: read failed at sample %d\r\n", count);
            break;
        }
        int16_t diff = (int16_t)(raw - prev);
        PrintDebug("[MSCNT-TEST] #%d raw=%u prev=%u diff=%d dt=%lu\r\n",
                   count, (unsigned)raw, (unsigned)prev, (int)diff,
                   (unsigned long)(HAL_GetTick() - t0));
        prev = raw;
        count++;
    }

    TMC_SetSpeed(0);
    vTaskDelay(pdMS_TO_TICKS(50));
    TMC_SetEnable(false);
    PrintDebug("[MSCNT-TEST] done: %d samples in 5s\r\n", count);
}





void PrintDebug(const char* fmt, ...) {
    osMutexAcquire(g_debug_mutex, osWaitForever);
    va_list args;
    va_start(args, fmt);
    int len = dbg_vformat(s_debug_buf, sizeof(s_debug_buf), fmt, args);
    va_end(args);
    if (len > 0) {
        UART_Write_DMA(UART_CH1, (uint8_t*)s_debug_buf, len);
    }
    osMutexRelease(g_debug_mutex);
}
void StartMotorTestTask(void *argument);


/**
 * @brief UART 测试任务
 * @param argument 任务参数
 * @note 该任务负责测试 CH340 的收发功能
 */
void StartUartTestTask(void *argument)
{
    UART_SendString(UART_CH1, "UART Echo Test Started (New Driver)\r\n");
		HAL_UART_Transmit(&huart1, (uint8_t*)"Test\r\n", 6, 100);
    for (;;)
    {
        // 驱动处理（搬运 DMA 数据到应用缓冲区）
        UART_Driver_Process();

        // 检查是否有新数据
        const uint8_t *rx_data = NULL;
        uint16_t rx_len = 0;
        if (UART_PeekData(UART_CH1, &rx_data, &rx_len))
        {
            // 构建回显帧
            char echo_buf[300];
            int offset = snprintf(echo_buf, sizeof(echo_buf), "Recv(%d): ", rx_len);
            if (offset > 0 && offset < (int)sizeof(echo_buf))
            {
                size_t copy = (rx_len < (sizeof(echo_buf) - offset - 2)) ? rx_len : (sizeof(echo_buf) - offset - 2);
                memcpy(echo_buf + offset, rx_data, copy);
                offset += copy;
                echo_buf[offset++] = '\r';
                echo_buf[offset++] = '\n';
                echo_buf[offset] = '\0';

                // 发送回显
                UART_SendData(UART_CH1, (uint8_t*)echo_buf, offset);
            }

            // 数据已处理，清除标志
            UART_ClearData(UART_CH1);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
/**
 * @brief TMC2209 测试任务
 */
void StartMotorTestTask(void *argument) {
    vTaskDelay(pdMS_TO_TICKS(500));
    PrintDebug("--- TMC2209 Motor Test ---\r\n");

    if (!TMC_Init()) {
        PrintDebug("[FATAL] TMC_Init failed!\r\n");
        vTaskSuspend(NULL);
    }

    // TMC2209 内部脉冲发生器模式：VACTUAL ≠ 0 即可，无需 RAMPMODE
    int32_t speed = 40000;  // 微步/秒

    for (;;) {
        TMC_SetEnable(true);
        vTaskDelay(pdMS_TO_TICKS(TMC_ENABLE_DELAY_MS));
        TMC_SetSpeed(speed);
        PrintDebug("Speed set to %ld\r\n", speed);
        vTaskDelay(pdMS_TO_TICKS(2000));
        TMC_SetSpeed(0);
        vTaskDelay(pdMS_TO_TICKS(50));
        TMC_SetEnable(false);

        // 反转方向
        speed = -speed;
    }
}

/**
 * @brief 简单 PWM 模拟函数（使用任务延时产生方波）
 * @param port : GPIO 端口
 * @param pin  : 引脚号
 * @param period_ms : 周期（ms）
 * @param duty    : 占空比（0~100）
 * @param cycles  : 重复周期数
 * @note  该函数会阻塞当前任务，仅用于测试演示
 */
static void SimplePWM(GPIO_TypeDef* port, uint16_t pin,
                      uint32_t period_ms, uint8_t duty, uint8_t cycles)
{
    uint32_t high_time = (period_ms * duty) / 100;
    uint32_t low_time  = period_ms - high_time;

    for (uint8_t i = 0; i < cycles; i++) {
        HAL_GPIO_WritePin(port, pin, GPIO_PIN_SET);
        vTaskDelay(pdMS_TO_TICKS(high_time));
        HAL_GPIO_WritePin(port, pin, GPIO_PIN_RESET);
        vTaskDelay(pdMS_TO_TICKS(low_time));
    }
}

/**
 * @brief DRV8803 测试任务
 *   - 初始化芯片配置
 *   - 依次使能两个芯片，循环点亮每个通道
 *   - 控制对应的 PWM 线产生呼吸效果
 *   - 故障监测与处理
 */
void StartDrv8803TestTask(void *argument)
{
     PrintDebug("--- 真空泵测试 (12VO1/PE11) ---\r\n");

    DRV8803_Init();

    DRV8803_EnableChip(1, true);
    DRV8803_EnableChip(2, true);   /* U13 24V 芯片使能（电磁阀） */
    /* U13 上电后给 IN5 一个跳变确保输出状态 */
    Valve_Off();                            /* 阀关断 PA6 LOW */
    vTaskDelay(pdMS_TO_TICKS(2));
    Valve_On();                             /* 阀导通 PA6 HIGH */
    

    Pump_On();
    PrintDebug("12VO1 (PE11) 真空泵已开启.\r\n");

    const TickType_t faultCheckPeriod = pdMS_TO_TICKS(100);
    for (;;)
    {
        if (DRV8803_IsChipFault(1))
        {
            PrintDebug("[FAULT] U12 fault! Attempt recovery...\r\n");
            DRV8803_HandleFault_RTOS(1);
            DRV8803_EnableChip(1, true);
            Pump_On();
            PrintDebug("U12 re-enabled, 12VO1 vacuum pump restored.\r\n");
        }
        vTaskDelay(faultCheckPeriod);
    }
}

/**
 * @brief  舵机匀速摆动测试任务
 * @note   舵机从 0° 平滑转到 180°，再返回 0°，循环往复。
 *         使用非阻塞 vTaskDelay，不影响其他 FreeRTOS 任务。
 */
void StartServoTestTask(void *argument)
{

    PrintDebug("Servo Test Task Started (CH%d, 0-180 deg sweep).\r\n", SERVO_TEST_CH);

    // 确保舵机模块已初始化（若已在 main 中调用可省略，重复调用无害）
    Servo_Init(&htim5);

    float angle = 0.0f;
    int8_t direction = 1; // 1:正向(0→180°)，-1:反向(180→0°)

    for (;;)
    {
        // 设置当前角度
        Servo_SetAngle(SERVO_TEST_CH, angle);

        // 调试输出（每 100ms 打印一次，避免串口阻塞影响运动平滑性）
        static uint32_t print_counter = 0;
        if (++print_counter >= (100 / SERVO_SWING_PERIOD_MS)) {
            PrintDebug("Servo Angle: %.1f°\r\n", angle);
            print_counter = 0;
        }

        // 计算下一步角度
        angle += direction * SERVO_SWING_STEP_DEG;

        // 边界处理：到达端点时反向并精确归位
        if (angle >= 180.0f) {
            angle = 180.0f;
            direction = -1;
        } else if (angle <= 0.0f) {
            angle = 0.0f;
            direction = 1;
        }

        // 非阻塞延时，让出 CPU 给其他任务
        vTaskDelay(pdMS_TO_TICKS(SERVO_SWING_PERIOD_MS));
    }
}

/**
 * @brief 发送单个电机绝对运动指令（坐标绝对模式 F5）
 */
static void motor_move_abs(uint8_t id, int32_t coord, uint16_t speed, uint8_t acc) {
    positionMode3Run(id, speed, acc, coord);
}

/**
 * @brief 发送两个X轴电机绝对运动指令（不立即执行，等同步触发）
 */
static void x_axis_move(int32_t x_coord) {
    motor_move_abs(MOTOR_X1_ID, x_coord, 300, 10);  // X1电机
    motor_move_abs(MOTOR_X2_ID, x_coord, 300, 10);  // X2电机
}

/**
 * @brief 触发同步启动（广播 0x4B）
 */
static void trigger_sync(void) {
    motorSyncTrigger(0);  // 广播地址0
}

/**
 * @brief 等待单个电机运动完成
 * @param timeout_ms 总超时（毫秒）
 * @return 0=全部完成, -1=超时
 */
static uint8_t wait_motor_done(uint8_t motor_id, uint32_t timeout_ms) {
    CAN_Rx_Packet_t pkt;
    uint32_t start = HAL_GetTick();
    while ((HAL_GetTick() - start) < timeout_ms) {
        if (osMessageQueueGet(motor_event_queue, &pkt, NULL, 10) == osOK) {
            if (pkt.FuncCode == 0xF5 && pkt.Status == 0x02 && pkt.ID == motor_id)
                return 0;
        }
    }
    return 1;
}


/**
 * @brief 等待多个电机运动完成
 * @param motors 电机ID数组
 * @param num 电机数量
 * @param timeout_ms 总超时（毫秒）
 * @return 0=全部完成, -1=超时
 */
int wait_motors_done(const uint8_t *motors, int num, uint32_t timeout_ms) {
    // 临时记录哪些电机已到位
    bool done[4] = {false}; // 假设ID最大为3
    int done_count = 0;
    uint32_t start = HAL_GetTick();

    while ((HAL_GetTick() - start) < timeout_ms) {
        CAN_Rx_Packet_t pkt;
        if (osMessageQueueGet(motor_event_queue, &pkt, NULL, 10) == osOK) {
            // 检查是否是F5运动完成的应答
            if (pkt.FuncCode == 0xF5 && pkt.Status == 0x02) {
                for (int i = 0; i < num; i++) {
                    if (pkt.ID == motors[i] && !done[motors[i]]) {
                        done[motors[i]] = true;
                        done_count++;
                        break;
                    }
                }
                if (done_count == num) return 0; // 全部完成
            }
        }
        osDelay(1); // 让出CPU
    }
    return -1;
}

/**
 * @brief 测试任务主体
 */
void vMotorTestTask(void *pvParameters)   {
    /* ---------- 1. 启动 CAN 并激活中断 ---------- */
    CAN_Init(&hfdcan1, NULL);
    osDelay(500);

    /* ---------- 2. 广播初始化 X1/X2/Y 的基础状态 ---------- */
    // EN 设为 Hold
    uint8_t en_hold[2] = {0x85, 0x02};
    CAN_Transmit_Data(&hfdcan1, 0, en_hold, 2); osDelay(50);
    // 解除堵转
    uint8_t unblock[1] = {0x3D};
    CAN_Transmit_Data(&hfdcan1, 0, unblock, 1); osDelay(50);
    // 全部设为总线 FOC 模式
    uint8_t mode[2] = {0x82, 0x05};
    CAN_Transmit_Data(&hfdcan1, 0, mode, 2); osDelay(100);
    // 全部使能
    uint8_t en[2] = {0xF3, 0x01};
    CAN_Transmit_Data(&hfdcan1, 0, en, 2); osDelay(100);
    // 关闭同步标志（方便独立测试）
    uint8_t sync_off[2] = {0x4A, 0x00};
    CAN_Transmit_Data(&hfdcan1, 0, sync_off, 2); osDelay(50);

    /* ---------- 3. 单独设置各轴的特有参数 ---------- */
    // 设置位置到达阈值（ID 1/2/3）
    motorSetArrivalThreshold(1);
    motorSetArrivalThreshold(2);
    motorSetArrivalThreshold(3);
    osDelay(20);

    // 标定当前位置为零点（所有轴）
    motorSetZero(1);
    motorSetZero(2);
    motorSetZero(3);
    osDelay(100);

    PrintDebug("=== XY 3?Axis Test Start ===\r\n");

    /* ---------- 4. 运动测试序列 ---------- */
    const uint16_t speed = 300;
    const uint8_t  acc   = 10;
    // 定义几个点位 (坐标值，一圈=16384)
    // 点1：X=3圈, Y=0
    // 点2：X=3圈, Y=3圈
    // 点3：X=0,    Y=3圈
    // 点4：X=0,    Y=0
    struct { int32_t x; int32_t y; } points[] = {
        { 16384 * 3, 0 },
        { 16384 * 3, 16384 * 3 },
        { 0,          16384 * 3 },
        { 0,          0 }
    };

    for (int i = 0; i < 4; i++) {
        PrintDebug("Point %d: X=%ld, Y=%ld\r\n", i, points[i].x, points[i].y);
        
        osEventFlagsClear(evtAxesDone, EVENT_ALL_AXES);   // 清空旧标志
        // 向 X1/X2 发送绝对位置指令（它们同时运动，但不严格同步起步，时间差很小）
        positionMode3Run(1, speed, acc, points[i].x);
        positionMode3Run(2, speed, acc, points[i].x);
        // 向 Y 轴发送绝对位置指令
        positionMode3Run(3, speed, acc, points[i].y);

        uint32_t flags = osEventFlagsWait(evtAxesDone, EVENT_ALL_AXES | EVENT_ANY_ERROR,
                                        osFlagsWaitAny, 10000);
        if ((flags & EVENT_ALL_AXES) == EVENT_ALL_AXES) {
            PrintDebug("All axes done.\r\n");
        } else {
            PrintDebug("Timeout!\r\n");
        }

        osDelay(200);   // 站间停顿
    }

    PrintDebug("=== XY 3?Axis Test Passed ===\r\n");
    vTaskSuspend(NULL);
}

/**
 * @brief 上位机通讯测试任务
 * @note  独立于 Host_Task，用于手动测试 UART1 链路和行协议解析。
 *        收到命令后打印解析结果并回显给上位机。
 */
void StartHostCommTestTask(void *argument)
{
    LineParser_t parser;
    HostParsed_t parsed;
    LineParser_Init(&parser);

    UART_SendString(UART_CH1, "[G4] HostComm Test Task Started\r\n");
    PrintDebug("[HOST_TEST] Task started. Waiting for commands...\r\n");

    for (;;)
    {
        /* 1. 驱动处理：将 DMA 数据搬运到应用缓冲区 */
        UART_Driver_Process();

        /* 2. 检查 UART_CH1（上位机）是否有新数据 */
        const uint8_t *rx_data = NULL;
        uint16_t rx_len = 0;
        if (UART_PeekData(UART_CH1, &rx_data, &rx_len))
        {
            /* 3. 逐字节喂入行解析器 */
            for (uint16_t i = 0; i < rx_len; i++)
            {
                if (LineParser_Feed(&parser, rx_data[i], &parsed))
                {
                    /* 收到完整一行，打印解析结果 */
                    switch (parsed.cmd)
                    {
                    case HCMD_MOVE_UP:
                        PrintDebug("[HOST_TEST] CMD: MOVE_UP, step=%.2f mm\r\n", parsed.param);
                        break;
                    case HCMD_MOVE_DOWN:
                        PrintDebug("[HOST_TEST] CMD: MOVE_DOWN, step=%.2f mm\r\n", parsed.param);
                        break;
                    case HCMD_MOVE_LEFT:
                        PrintDebug("[HOST_TEST] CMD: MOVE_LEFT, step=%.2f mm\r\n", parsed.param);
                        break;
                    case HCMD_MOVE_RIGHT:
                        PrintDebug("[HOST_TEST] CMD: MOVE_RIGHT, step=%.2f mm\r\n", parsed.param);
                        break;
                    case HCMD_MOVE_UP_START:
                        PrintDebug("[HOST_TEST] CMD: MOVE_UP_START, speed=%.2f mm/s\r\n", parsed.param);
                        break;
                    case HCMD_MOVE_DOWN_START:
                        PrintDebug("[HOST_TEST] CMD: MOVE_DOWN_START, speed=%.2f mm/s\r\n", parsed.param);
                        break;
                    case HCMD_MOVE_LEFT_START:
                        PrintDebug("[HOST_TEST] CMD: MOVE_LEFT_START, speed=%.2f mm/s\r\n", parsed.param);
                        break;
                    case HCMD_MOVE_RIGHT_START:
                        PrintDebug("[HOST_TEST] CMD: MOVE_RIGHT_START, speed=%.2f mm/s\r\n", parsed.param);
                        break;
                    case HCMD_MOVE_STOP:
                        PrintDebug("[HOST_TEST] CMD: MOVE_STOP\r\n");
                        break;
                    case HCMD_SET_ORIGIN:
                        PrintDebug("[HOST_TEST] CMD: SET_ORIGIN\r\n");
                        break;
                    case HCMD_EXIT_DEBUG:
                        PrintDebug("[HOST_TEST] CMD: EXIT_DEBUG_MODE\r\n");
                        break;
                    case HCMD_RAW_LINE:
                        PrintDebug("[HOST_TEST] CSV: %.*s\r\n",
                                   (int)(parsed.raw_len > 40 ? 40 : parsed.raw_len), parsed.raw);
                        break;
                    default:
                        PrintDebug("[HOST_TEST] UNKNOWN CMD\r\n");
                        break;
                    }
                }
            }
            /* 4. 清除就绪标志，释放缓冲区 */
            UART_ClearData(UART_CH1);
        }

        /* 5. 10ms 轮询周期 */
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* 上位机通讯 + XY 运动控制测试任务 属性 */
//const osThreadAttr_t hostMotionTestTask_attributes = {
//    .name = "HostMotion",
//    .stack_size = 1024,
//    .priority = osPriorityNormal
//};

/**
 * @brief 上位机通讯 + XY 运动控制测试任务
 * @note  启动时发送 DEBUG_MODE\n 触发上位机进入 debug 模式。
 *        接收上位机指令，解析后执行对应电机动作并回显。
 */
void StartHostMotionTestTask(void *argument)
{
    LineParser_t parser;
    HostParsed_t parsed;
    LineParser_Init(&parser);

    bool jog_active = false;

    const uint16_t speed = 300;
    const uint8_t  acc   = 25;

    /* 电机初始化: 配置工作模式+使能+归零 */
		CAN_Init(&hfdcan1, NULL);
    Motor_Init();
    osDelay(200);

    /* 启动握手 */
    UART_SendString(UART_CH1, "DEBUG_MODE\n");
    PrintDebug("[HostMotion] Task started, DEBUG_MODE sent.\r\n");

    for (;;)
    {
        UART_Driver_Process();

        const uint8_t *rx_data = NULL;
        uint16_t rx_len = 0;
        if (UART_PeekData(UART_CH1, &rx_data, &rx_len))
        {
            for (uint16_t i = 0; i < rx_len; i++)
            {
                if (LineParser_Feed(&parser, rx_data[i], &parsed))
                {
                    int32_t steps;
                    int result;

                    switch (parsed.cmd)
                    {
                    case HCMD_MOVE_UP:
                        steps = (int32_t)(parsed.param * STEPS_PER_MM);
                        PrintDebug("[HostMotion] MOVE_UP %.2fmm -> %ld steps\r\n", parsed.param, steps);
                        result = move_xy_relative(steps, 0, speed, acc);
                        PrintDebug("[HostMotion] MOVE_UP done, pos=(%ld,%ld) ret=%d\r\n", Coord_Get().x, Coord_Get().y, result);
                        jog_active = false;
                        break;

                    case HCMD_MOVE_DOWN:
                        steps = (int32_t)(-parsed.param * STEPS_PER_MM);
                        PrintDebug("[HostMotion] MOVE_DOWN %.2fmm -> %ld steps\r\n", parsed.param, steps);
                        result = move_xy_relative(steps, 0, speed, acc);
                        PrintDebug("[HostMotion] MOVE_DOWN done, pos=(%ld,%ld) ret=%d\r\n", Coord_Get().x, Coord_Get().y, result);
                        jog_active = false;
                        break;

                    case HCMD_MOVE_LEFT:
                        steps = (int32_t)(parsed.param * STEPS_PER_MM);
                        PrintDebug("[HostMotion] MOVE_LEFT %.2fmm -> %ld steps\r\n", parsed.param, steps);
                        result = move_xy_relative(0, steps, speed, acc);
                        PrintDebug("[HostMotion] MOVE_LEFT done, pos=(%ld,%ld) ret=%d\r\n", Coord_Get().x, Coord_Get().y, result);
                        jog_active = false;
                        break;

                    case HCMD_MOVE_RIGHT:
                        steps = (int32_t)(-parsed.param * STEPS_PER_MM);
                        PrintDebug("[HostMotion] MOVE_RIGHT %.2fmm -> %ld steps\r\n", parsed.param, steps);
                        result = move_xy_relative(0, steps, speed, acc);
                        PrintDebug("[HostMotion] MOVE_RIGHT done, pos=(%ld,%ld) ret=%d\r\n", Coord_Get().x, Coord_Get().y, result);
                        jog_active = false;
                        break;

                    case HCMD_MOVE_UP_START:
                        PrintDebug("[HostMotion] JOG UP speed=%.2f\r\n", parsed.param);
                        if (jog_active) { disable_sync_stop(); }
                        jog_active = true;
                        positionMode3Run(X1_ADDR, (uint16_t)parsed.param, acc, JOG_MAX_STEPS);
                        positionMode3Run(X2_ADDR, (uint16_t)parsed.param, acc, JOG_MAX_STEPS);
                        motorSyncTrigger(0);
                        break;

                    case HCMD_MOVE_DOWN_START:
                        PrintDebug("[HostMotion] JOG DOWN speed=%.2f\r\n", parsed.param);
                        if (jog_active) { disable_sync_stop(); }
                        jog_active = true;
                        positionMode3Run(X1_ADDR, (uint16_t)parsed.param, acc, -JOG_MAX_STEPS);
                        positionMode3Run(X2_ADDR, (uint16_t)parsed.param, acc, -JOG_MAX_STEPS);
                        motorSyncTrigger(0);
                        break;

                    case HCMD_MOVE_LEFT_START:
                        PrintDebug("[HostMotion] JOG LEFT speed=%.2f\r\n", parsed.param);
                        if (jog_active) { disable_sync_stop(); }
                        jog_active = true;
                        positionMode3Run(Y_ADDR, (uint16_t)parsed.param, acc, JOG_MAX_STEPS);
                        motorSyncTrigger(0);
                        break;

                    case HCMD_MOVE_RIGHT_START:
                        PrintDebug("[HostMotion] JOG RIGHT speed=%.2f\r\n", parsed.param);
                        if (jog_active) { disable_sync_stop(); }
                        jog_active = true;
                        positionMode3Run(Y_ADDR, (uint16_t)parsed.param, acc, -JOG_MAX_STEPS);
                        motorSyncTrigger(0);
                        break;

                    case HCMD_MOVE_STOP:
                        PrintDebug("[HostMotion] STOP\r\n");
                        disable_sync_stop();
                        jog_active = false;
                        break;


                    case HCMD_MOVE_TO:
                    {
                        int32_t target_x = (int32_t)(parsed.param  * STEPS_PER_MM);
                        int32_t target_y = (int32_t)(parsed.param2 * STEPS_PER_MM);
                        PrintDebug("[HostMotion] MOVE_TO (%.2f, %.2f)mm -> (%ld, %ld) steps\r\n",
                                   parsed.param, parsed.param2, target_x, target_y);
                        if (jog_active) { axis_stop(X1_ADDR); axis_stop(X2_ADDR); axis_stop(Y_ADDR); jog_active = false; }
                        int32_t dx = target_x - Coord_Get().x;
                        int32_t dy = target_y - Coord_Get().y;
                        int result = move_xy_relative(dx, dy, speed, acc);
                        PrintDebug("[HostMotion] MOVE_TO done, pos=(%ld,%ld) ret=%d\r\n", Coord_Get().x, Coord_Get().y, result);
                        break;
                    }
                    case HCMD_SET_ORIGIN:
                        PrintDebug("[HostMotion] SET_ORIGIN\r\n");
                        motorSetZero(X1_ADDR);
                        motorSetZero(X2_ADDR);
                        motorSetZero(Y_ADDR);
                        Coord_SetHome();
                        osDelay(100);
                        break;

                    case HCMD_EXIT_DEBUG:
                        PrintDebug("[HostMotion] EXIT_DEBUG_MODE, task suspended.\r\n");
                        vTaskSuspend(NULL);
                        break;

                    case HCMD_RAW_LINE:
                        PrintDebug("[HostMotion] CSV: %.*s\r\n",
                                   (int)(parsed.raw_len > 40 ? 40 : parsed.raw_len), parsed.raw);
                        break;

                    default:
                        PrintDebug("[HostMotion] UNKNOWN CMD\r\n");
                        break;
                    }
                }
            }
            if (s_cmd_interrupted) {
                s_cmd_interrupted = false;
                continue;
            }
            UART_ClearData(UART_CH1);
        }

        osThreadFlagsWait(0x01, osFlagsWaitAny, pdMS_TO_TICKS(50));
    }
}


/* ================================================================
 * Z轴舵机 + 吸嘴气泵 + R轴旋转 联合测试任务
 *
 * 测试流程（循环执行）：
 *   [Z轴舵机]  转到拾取角 → 开气泵 → 转到贴装角 → 关气泵
 *   [R轴旋转]  斜坡启动正转 → 停 → 斜坡启动反转 → 停
 *
 * 调试：定义 PICKPLACE_VERBOSE 开启 VACTUAL/DRV_STATUS 读回校验。
 * ================================================================ */

/* #define PICKPLACE_VERBOSE */   /* 取消注释以开启详细诊断 */

/* ---- 测试参数宏 ---- */
#define PICKPLACE_SERVO_CH       2            /* PE8 → TIM5_CH3 → 通道索引 2 */
#define PICKPLACE_PICK_ANGLE     30.0f        /* 拾取角度（吸嘴下降） */
#define PICKPLACE_PLACE_ANGLE    120.0f       /* 贴装角度（吸嘴上升） */
#define PICKPLACE_PUMP_PORT      (&Port_12VO1)  /* 吸嘴气泵 (12VO1/PE11) */
#define PICKPLACE_SERVO_PORT     (&Port_12VO4)  /* Z轴舵机供电 (12VO4/PE14) */

#define PICKPLACE_R_SPEED        80000        /* R轴目标转速（微步/秒） */
#define PICKPLACE_R_RAMP_STEP    8000         /* 斜坡每级增量（微步/秒） */
#define PICKPLACE_R_RAMP_MS      40           /* 斜坡每级延时 (ms) */
#define PICKPLACE_R_RUN_MS       1500         /* R轴全速运行时间 (ms) */
#define PICKPLACE_STEP_DELAY_MS  500          /* 动作间停顿 (ms) */

/* ================================================================
 * 内部辅助函数
 * ================================================================ */

/**
 * @brief Z轴舵机拾取：转到拾取角 → 开气泵 → 开电磁阀
 */
static void pickplace_pick(void)
{
    PrintDebug("[PickPlace] 拾取: 舵机→%.0f° + 泵ON + 阀ON\r\n",
               PICKPLACE_PICK_ANGLE);
    Servo_SetAngle(PICKPLACE_SERVO_CH, PICKPLACE_PICK_ANGLE);
    vTaskDelay(pdMS_TO_TICKS(PICKPLACE_STEP_DELAY_MS));
    Pump_On();
    /* 阀是低端开关：OUT5拉GND才导通。先HIGH再LOW确保锁存 */
    Valve_Off();                            /* 阀关断 PA6 LOW */
    vTaskDelay(pdMS_TO_TICKS(2));
    Valve_On();                             /* 阀导通 */
}
/**
 * @brief Z轴舵机贴装：关阀 → 关泵 → 转到贴装角
 */
static void pickplace_place(void)
{
    PrintDebug("[PickPlace] 贴装: 阀OFF + 泵OFF + 舵机→%.0f°\r\n",
               PICKPLACE_PLACE_ANGLE);
    Valve_Off();                            /* 阀关断 */
    Pump_Off();
    vTaskDelay(pdMS_TO_TICKS(100));
    Servo_SetAngle(PICKPLACE_SERVO_CH, PICKPLACE_PLACE_ANGLE);
    vTaskDelay(pdMS_TO_TICKS(PICKPLACE_STEP_DELAY_MS));
    PrintDebug("[PickPlace] 贴装完成\r\n");
}

/**
 * @brief R轴斜坡启动（正转）
 *
 * VACTUAL 模式无极变速/加速斜坡，直接跳全速时静摩擦力会卡住电机。
 * 从 5000 微步/秒起步，每级 +8000，逐级提升至目标转速。
 */
static void pickplace_r_forward(void)
{
    PrintDebug("[PickPlace] R轴正转 斜坡→%ld μstep/s, 持续 %dms\r\n",
               PICKPLACE_R_SPEED, PICKPLACE_R_RUN_MS);

    /* 速度斜坡 */
    for (int32_t s = 5000; s < PICKPLACE_R_SPEED; s += PICKPLACE_R_RAMP_STEP) {
        TMC_SetSpeed(s);
        vTaskDelay(pdMS_TO_TICKS(PICKPLACE_R_RAMP_MS));
    }
    TMC_SetSpeed(PICKPLACE_R_SPEED);

#ifdef PICKPLACE_VERBOSE
    {
        uint32_t val;
        if (TMC_ReadReg(TMC_REG_VACTUAL, &val) == TMC_ERR_NONE)
            PrintDebug("[PickPlace] VACTUAL=0x%08lX (%ld)\r\n", val, (int32_t)val);
        /* DRV_STATUS: stst 表示是否停转，fsact 是实际电流等级 */
        if (TMC_ReadReg(TMC_REG_DRV_STATUS, &val) == TMC_ERR_NONE)
            PrintDebug("[PickPlace] DRV_STATUS stst=%ld fsact=%ld\r\n",
                       (val >> 31) & 1, (val >> 16) & 0x1F);
    }
#endif

    vTaskDelay(pdMS_TO_TICKS(PICKPLACE_R_RUN_MS));
    TMC_SetSpeed(0);
    vTaskDelay(pdMS_TO_TICKS(PICKPLACE_STEP_DELAY_MS));
    PrintDebug("[PickPlace] R轴停止\r\n");
}

/**
 * @brief R轴斜坡启动（反转）
 */
static void pickplace_r_reverse(void)
{
    PrintDebug("[PickPlace] R轴反转 斜坡→%ld μstep/s, 持续 %dms\r\n",
               -PICKPLACE_R_SPEED, PICKPLACE_R_RUN_MS);

    for (int32_t s = -5000; s > -PICKPLACE_R_SPEED; s -= PICKPLACE_R_RAMP_STEP) {
        TMC_SetSpeed(s);
        vTaskDelay(pdMS_TO_TICKS(PICKPLACE_R_RAMP_MS));
    }
    TMC_SetSpeed(-PICKPLACE_R_SPEED);

#ifdef PICKPLACE_VERBOSE
    {
        uint32_t val;
        if (TMC_ReadReg(TMC_REG_VACTUAL, &val) == TMC_ERR_NONE)
            PrintDebug("[PickPlace] VACTUAL=0x%08lX (%ld)\r\n", val, (int32_t)val);
    }
#endif

    vTaskDelay(pdMS_TO_TICKS(PICKPLACE_R_RUN_MS));
    TMC_SetSpeed(0);
    vTaskDelay(pdMS_TO_TICKS(PICKPLACE_STEP_DELAY_MS));
    PrintDebug("[PickPlace] R轴停止\r\n");
}

/**
 * @brief Z轴舵机 + 吸嘴气泵 + R轴旋转 联合测试任务
 * @note  启动时依次初始化舵机、DRV8803、TMC2209。
 *        主循环：拾取→贴装→R正转→R反转，周而复始。
 */
void StartPickPlaceTestTask(void *argument)
{
    vTaskDelay(pdMS_TO_TICKS(500));
    PrintDebug("========================================\r\n");
    PrintDebug("  PickPlace 联合测试任务启动\r\n");
    PrintDebug("  Z轴舵机(CH%d) + 气泵(12VO1) + 电磁阀(24VO1) + R轴(TMC2209)\r\n",
               PICKPLACE_SERVO_CH);
    PrintDebug("========================================\r\n");

    /* ---- 1. 初始化 DRV8803（气泵） ---- */
    DRV8803_Init();
    DRV8803_EnableChip(1, true);
    DRV8803_EnableChip(2, true);   /* U13 24V 芯片使能（电磁阀） */
    PrintDebug("[PickPlace] DRV8803 初始化完成\r\n");

    /* ---- 2. 初始化舵机 ---- */
    Servo_Init(&htim2);
    DRV8803_SetOutput(PICKPLACE_SERVO_PORT, true);  /* 舵机上电 */
    vTaskDelay(pdMS_TO_TICKS(100));
    Servo_SetAngle(PICKPLACE_SERVO_CH, PICKPLACE_PLACE_ANGLE);
    vTaskDelay(pdMS_TO_TICKS(300));
    if (Servo_IsInitialized(PICKPLACE_SERVO_CH)) {
        PrintDebug("[PickPlace] 舵机初始化完成，当前角度=%.0f°\r\n",
                   Servo_GetAngle(PICKPLACE_SERVO_CH));
    } else {
        PrintDebug("[PickPlace] 舵机初始化失败！\r\n");
    }

    /* ---- 3. 初始化 R轴 TMC2209 ---- */
    if (!TMC_Init()) {
        PrintDebug("[PickPlace] FATAL: TMC2209 初始化失败！任务挂起。\r\n");
        vTaskSuspend(NULL);
    }
    PrintDebug("[PickPlace] TMC2209 (R轴) 初始化完成\r\n");

    /* 回读关键寄存器（始终执行，用于确认配置） */
    {
        uint32_t reg_val;
        if (TMC_ReadReg(TMC_REG_GCONF, &reg_val) == TMC_ERR_NONE)
            PrintDebug("[PickPlace] GCONF   =0x%08lX (PDN_DIS=%ld, MSTEP_REG=%ld, I_SCALE=%ld)\r\n",
                       reg_val, (reg_val>>6)&1, (reg_val>>7)&1, reg_val&1);
        if (TMC_ReadReg(TMC_REG_CHOPCONF, &reg_val) == TMC_ERR_NONE)
            PrintDebug("[PickPlace] CHOPCONF=0x%08lX (TOFF=%ld, MRES=%ld, INTPOL=%ld)\r\n",
                       reg_val, reg_val&0xF, (reg_val>>24)&0xF, (reg_val>>28)&1);
        if (TMC_ReadReg(TMC_REG_VACTUAL, &reg_val) == TMC_ERR_NONE)
            PrintDebug("[PickPlace] VACTUAL =%ld (初始值)\r\n", (int32_t)reg_val);
    }

    /* ---- 4. 主测试循环 ---- */
    uint32_t cycle = 0;
    for (;;)
    {
        cycle++;
        PrintDebug("\r\n--- PickPlace 测试周期 %lu ---\r\n", cycle);

        pickplace_pick();                       /* Z轴拾取 */
        vTaskDelay(pdMS_TO_TICKS(300));
        pickplace_place();                      /* Z轴贴装 */
        vTaskDelay(pdMS_TO_TICKS(300));
        pickplace_r_forward();                  /* R轴正转 */

        pickplace_r_reverse();                  /* R轴反转 */

        PrintDebug("--- 周期 %lu 完成 ---\r\n\r\n", cycle);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* ================================================================
 * 摄像头 + 电机联动测试
 *
 * 依次测试 P1(散料区找元件) → P3(下相机偏移) → P2(Mark点建系)。
 * 每个 Process 有独立超时，超时或出错后跳到下一个。
 *
 * ★ 收到 GOT_POS 后会实际驱动 XY 电机移动，再发 "go" 给摄像头，
 *   形成完整的位置反馈闭环。
 * ================================================================ */
#define CAM_TEST_TIMEOUT_P2   120000  /* P2 超时 ms (3个Mark) */

/* 偏移→步数换算系数 (需根据相机 FOV 实测标定！) */
#define CAM_PX_TO_STEPS       (STEPS_PER_MM / 1000.0f)   /* 像素 → 步数 */
#define CAM_MM10000_TO_STEPS  (STEPS_PER_MM / 10000.0f)  /* mm*10000 → 步数 */
#define CAM_MOVE_SPEED        100
#define CAM_MOVE_ACC          25

/* P2 完整测试 — 与 Host_Task mark_align_step 共用常量 */
#define P2_TEST_SCAN_STEP_MM    5.0f   /* P2 网格扫描步长 (mm) */
#define P2_TEST_SCAN_TIMEOUT_MS 3000   /* 每格位超时 (ms) */
#define P2_TEST_VERIFY_ERR_MM   0.3f   /* Mark3 验证允许误差 (mm) */

/**
 * @brief 运行一个视觉 Process 到完成或超时
 * @param cmd        VCMD_P1 / VCMD_P2 / VCMD_P3
 * @param timeout_ms 超时 (ms)
 * @param Coord_Get().x      当前 X 坐标 (步数, 输入输出)
 * @param Coord_Get().y      当前 Y 坐标 (步数, 输入输出)
 * @param out_angle_deg  [出参] 视觉检测角度 (deg)，仅 VISION_DONE 时写入；可为 NULL
 * @return true=完成, false=超时或出错
 */
static bool cam_p2_full_test_run(const Component_t marks[], uint32_t mark_count, uint32_t timeout_ms) {
    if (mark_count < (uint32_t)P2_MARK_COUNT || marks == NULL) {
        PrintDebug("[CAM_TEST] P2: need %d marks, got %lu\r\n", P2_MARK_COUNT, (unsigned long)mark_count);
        return false;
    }

    /* 局部状态 (复刻 Host_Task 全局变量) */
    int32_t  marks_actual[P2_MARK_COUNT][2] = {0};
    int32_t  mark_offsets[P2_MARK_COUNT][2] = {0};
    int32_t  scan_cols = 0, scan_rows = 0, scan_cur = 0;
    bool     mark_scanning = false;
    bool     mark_just_jumped = false;
    int32_t  mark_count_done = 0;
    int32_t  align_busy_cycles = 0;  /* P2 aligning 阶段 VISION_BUSY 超时次数 */
    uint32_t busy_enter_tick = 0;
    bool     in_busy = false;

    /* 计算网格扫描参数 (同 Host_Task P2 初始化) */
    int32_t scan_step = (int32_t)(P2_TEST_SCAN_STEP_MM * STEPS_PER_MM);
    int32_t area_w = g_calib.heat_platform_x_max - g_calib.heat_platform_x_min;
    int32_t area_h = g_calib.heat_platform_y_max - g_calib.heat_platform_y_min;
    if (area_w < 0) area_w = -area_w;
    if (area_h < 0) area_h = -area_h;
    if (area_w > 0 && area_h > 0) {
        scan_cols = (area_w + scan_step - 1) / scan_step;
        scan_rows = (area_h + scan_step - 1) / scan_step;
        if (scan_cols < 1) scan_cols = 1;
        if (scan_rows < 1) scan_rows = 1;
        scan_cur = 0;
        mark_scanning = true;
        safe_move_to(g_calib.heat_platform_x_min + g_calib.cam_to_nozzle_dx_steps,
                     g_calib.heat_platform_y_min + g_calib.cam_to_nozzle_dy_steps,
                     PNP_SPEED, PNP_ACC);
        PrintDebug("[CAM_TEST] P2 scan: %ldx%ld grid, step=%ld steps\r\n",
                   (long)scan_cols, (long)scan_rows, (long)scan_step);
    } else {
        mark_scanning = false;
        PrintDebug("[CAM_TEST] PCB area uncalibrated, single-spot P2.\r\n");
    }

    Vision_Start(VCMD_P2, 0);
    Vision_Go();
    PrintDebug("[CAM_TEST] Starting Mark alignment (P2, %lu marks)...\r\n", (unsigned long)mark_count);

    VisionState_t prev = Vision_GetState();
    uint32_t start_tick = osKernelGetTickCount();

    while ((osKernelGetTickCount() - start_tick) < pdMS_TO_TICKS(timeout_ms)) {
        UART_Driver_Process();

        VisionState_t state = Vision_GetState();

        if (state == VISION_RDY) {
            in_busy = false;
            Vision_Go();
            prev = state;
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        /* VISION_BUSY: 按时间超时而非计数，适配阻塞轮询 */
        if (state == VISION_BUSY) {
            if (!in_busy) {
                in_busy = true;
                busy_enter_tick = osKernelGetTickCount();
            }
            if ((osKernelGetTickCount() - busy_enter_tick) < pdMS_TO_TICKS(P2_TEST_SCAN_TIMEOUT_MS)) {
                vTaskDelay(pdMS_TO_TICKS(20));
                continue;
            }
            in_busy = false;
            /* 超时: 处理扫描 */
            if (mark_scanning) {
                scan_cur++;
                if (scan_cur >= scan_cols * scan_rows) {
                    PrintDebug("[CAM_TEST] P2 scan exhausted (%ld cells), Mark0 not found.\r\n",
                               (long)(scan_cols * scan_rows));
                    mark_scanning = false;
                    Vision_SendEnd();
                    Vision_ForceIdle();
                    return false;
                }
                /* 竖向蛇形扫描: row慢(左右步进) col快(上下扫描) */
                int32_t row = scan_cur / scan_cols;
                int32_t col = scan_cur % scan_cols;
                int32_t x_dir = (g_calib.heat_platform_x_max >= g_calib.heat_platform_x_min) ? 1 : -1;
                int32_t y_dir = (g_calib.heat_platform_y_max >= g_calib.heat_platform_y_min) ? 1 : -1;
                int32_t tx, ty;
                if (row & 1) {
                    tx = g_calib.heat_platform_x_max - x_dir * col * scan_step;
                } else {
                    tx = g_calib.heat_platform_x_min + x_dir * col * scan_step;
                }
                ty = g_calib.heat_platform_y_min + y_dir * row * scan_step;
                safe_move_to(tx + g_calib.cam_to_nozzle_dx_steps,
                             ty + g_calib.cam_to_nozzle_dy_steps,
                             PNP_SPEED, PNP_ACC);
                PrintDebug("[CAM_TEST] P2 scan [%ld,%ld] -> (%ld,%ld)\r\n",
                           (long)row, (long)col, (long)tx, (long)ty);
            } else {
                /* Mark已锁定(aligning/pos-detect)超时: 重置超时继续等Cam */
                align_busy_cycles++;
                if (align_busy_cycles == 1 || (align_busy_cycles % 10) == 0) {
                    PrintDebug("[CAM_TEST] P2 align-wait: cycles=%ld total_rx=%lu align_rx=%lu stp_ign=%lu\r\n",
                               (long)align_busy_cycles, (unsigned long)Vision_GetP2TotalRxCount(),
                               (unsigned long)Vision_GetAlignRxCount(),
                               (unsigned long)Vision_GetP2StpIgnoredCount());
                }
                busy_enter_tick = osKernelGetTickCount();
            }
            prev = state;
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        if (state != prev) {
            in_busy = false;
            const VisionResult_t *r = Vision_GetResult();

            switch (state) {
            case VISION_RDY:
                Vision_Go();
                PrintDebug("[CAM_TEST]   -> rdy received, sent go\r\n");
                break;

            case VISION_GOT_STOP:
                mark_scanning = false;
                {
                    int32_t idx = r->mark_index;
                    if (idx >= 1 && idx < (int32_t)P2_MARK_COUNT && !mark_just_jumped) {
                        /* Mark 间跳转预估 (同 Host_Task) */
                        float tdx = marks[idx].target_x - marks[idx-1].target_x;
                        float tdy = marks[idx].target_y - marks[idx-1].target_y;
                        int32_t dx = (int32_t)(tdy * STEPS_PER_MM);  // target_y → X1+X2 电机（物理 Y 轴）
                        int32_t dy = -(int32_t)(tdx * STEPS_PER_MM); // target_x → Y 电机（物理 X 轴）
                        int32_t prev_idx = idx - 1;
                        safe_move_to(marks_actual[prev_idx][0] + dx,
                                     marks_actual[prev_idx][1] + dy,
                                     PNP_SPEED, PNP_ACC);
                        mark_just_jumped = true;
                        PrintDebug("[CAM_TEST] P2 jump Mark%ld: theory(%.1f,%.1f)mm -> (%ld,%ld)\r\n",
                                   (long)idx, (double)tdx, (double)tdy,
                                   (long)Coord_Get().x, (long)Coord_Get().y);
                    }
                }
                Vision_Go();
                break;

            case VISION_GOT_POS:
                PrintDebug("[CAM_TEST] GOT_POS entered, align_rx_cnt=%lu\r\n", (unsigned long)Vision_GetAlignRxCount());
                mark_scanning = false;
                mark_just_jumped = false;
                {
                    int32_t idx = r->mark_index;
                    if (idx >= 0 && idx < (int32_t)P2_MARK_COUNT) {
                        if (r->dx != 0 || r->dy != 0) {
                            int32_t dx_s = -(int32_t)(r->dy * CAM_PX_TO_STEPS);
                            int32_t dy_s = -(int32_t)(r->dx * CAM_PX_TO_STEPS);
                            MachineCoord_t mc_before = Coord_Get();
                            int ret = safe_move_to(mc_before.x + dx_s, mc_before.y + dy_s,
                                                    CAM_MOVE_SPEED, CAM_MOVE_ACC);
                            MachineCoord_t mc_after = Coord_Get();
                            PrintDebug("[CAM_TEST] Mark%ld offset: (%ld,%ld)px move(%ld,%ld)steps ret=%d before=(%ld,%ld) after=(%ld,%ld)\r\n",
                                       (long)idx, (long)r->dx, (long)r->dy, (long)dx_s, (long)dy_s, ret,
                                       (long)mc_before.x, (long)mc_before.y, (long)mc_after.x, (long)mc_after.y);
                        }
                        marks_actual[idx][0] = Coord_Get().x;
                        marks_actual[idx][1] = Coord_Get().y;
                        mark_offsets[idx][0] = r->dx;
                        mark_offsets[idx][1] = r->dy;
                        mark_count_done = idx + 1;
                    }
                }
                Vision_Go();
                vTaskDelay(pdMS_TO_TICKS(50));  /* 确保 go 帧完整发送，摄像头有时间处理 */
                break;

            case VISION_DONE:
                mark_scanning = false;
                /* 仿射建系 + Mark3 验证 */
                {
                    int32_t a1x = marks_actual[0][0], a1y = marks_actual[0][1];
                    int32_t a2x = marks_actual[1][0], a2y = marks_actual[1][1];
                    int32_t a3x = marks_actual[2][0], a3y = marks_actual[2][1];

                    /* 电机坐标 → 相机坐标: cam_X = -(Y电机), cam_Y = X1+X2 */
                    float ac1x = -(float)a1y, ac1y = (float)a1x;
                    float ac2x = -(float)a2y, ac2y = (float)a2x;
                    float ac3x = -(float)a3y, ac3y = (float)a3x;

                    float t1x = marks[0].target_x, t1y = marks[0].target_y;
                    float t2x = marks[1].target_x, t2y = marks[1].target_y;
                    float t3x = marks[2].target_x, t3y = marks[2].target_y;

                    float theory_ang = atan2f(t2y - t1y, t2x - t1x);
                    float actual_ang = atan2f(ac2y - ac1y, ac2x - ac1x);
                    float theta = actual_ang - theory_ang;

                    float mt_x = (t1x + t2x) * 0.5f * STEPS_PER_MM;
                    float mt_y = (t1y + t2y) * 0.5f * STEPS_PER_MM;
                    float ma_x = (ac1x + ac2x) * 0.5f;
                    float ma_y = (ac1y + ac2y) * 0.5f;

                    float cos_t = cosf(theta), sin_t = sinf(theta);
                    float o_cx = ma_x - (mt_x * cos_t - mt_y * sin_t);
                    float o_cy = ma_y - (mt_x * sin_t + mt_y * cos_t);
                    int32_t origin_x = (int32_t)o_cy;    /* cam_Y → X1+X2 */
                    int32_t origin_y = (int32_t)(-o_cx); /* cam_X → Y电机(取反) */

                    float t3x_s = t3x * STEPS_PER_MM;
                    float t3y_s = t3y * STEPS_PER_MM;
                    float pcx = (t3x_s * cos_t - t3y_s * sin_t) + o_cx;
                    float pcy = (t3x_s * sin_t + t3y_s * cos_t) + o_cy;
                    int32_t pred_x = (int32_t)pcy;      /* cam_Y → X1+X2 */
                    int32_t pred_y = (int32_t)(-pcx);   /* cam_X → Y电机(取反) */
                    int32_t err_x = pred_x - a3x, err_y = pred_y - a3y;
                    float err_mm = sqrtf((float)(err_x*err_x + err_y*err_y)) / STEPS_PER_MM;
                    bool valid = (err_mm < P2_TEST_VERIFY_ERR_MM);

                    PrintDebug("[CAM_TEST] === PCB Frame ===\r\n");
                    PrintDebug("[CAM_TEST] origin=(%ld,%ld) theta=%.4frad(%.2fdeg)\r\n",
                               (long)origin_x, (long)origin_y,
                               (double)theta, (double)(theta * 57.29578f));
                    PrintDebug("[CAM_TEST] Mark3 verify: err=%.3fmm %s\r\n",
                               (double)err_mm, valid ? "OK" : "FAIL");

                    if (mark_count_done >= 3) {
                        int32_t avg_dx = (mark_offsets[0][0] + mark_offsets[1][0] + mark_offsets[2][0]) / 3;
                        int32_t avg_dy = (mark_offsets[0][1] + mark_offsets[1][1] + mark_offsets[2][1]) / 3;
                        PrintDebug("[CAM_TEST] Mark avg offset: (%ld,%ld) px\r\n",
                                   (long)avg_dx, (long)avg_dy);
                    }
                }
                Vision_SendEnd();
                return true;

            case VISION_ERROR:
                PrintDebug("[CAM_TEST] Mark alignment ERROR: %s\r\n", Vision_GetError());
                if (scan_cur < scan_cols * scan_rows - 1) {
                    mark_scanning = true;
                    Vision_BackToSearch();
                    PrintDebug("[CAM_TEST] P2 error, resuming scan from cell %ld\r\n", (long)scan_cur);
                } else {
                    mark_scanning = false;
                    Vision_SendEnd();
                    Vision_ForceIdle();
                    return false;
                }
                break;

            default:
                break;
            }
            /* Vision_Go 总是将状态转到 VISION_BUSY，用 BUSY 作为 prev
               避免 ISR 在 delay 期间已将状态切回的竞态 */
            prev = VISION_BUSY;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    PrintDebug("[CAM_TEST] P2 TIMEOUT after %lu ms, total_rx=%lu align_rx=%lu stp_ign=%lu\r\n",               (unsigned long)timeout_ms,
               (unsigned long)Vision_GetP2TotalRxCount(),
               (unsigned long)Vision_GetAlignRxCount(),
               (unsigned long)Vision_GetP2StpIgnoredCount());
    Vision_SendEnd();
    Vision_ForceIdle();
    return false;
}

/**
 * @brief 摄像头 + 电机联动测试任务
 * @note  初始化 CAN/MKS 电机 + Vision，依次执行 P1/P3/P2。
 *        根据摄像头返回的偏移实际驱动 XY 平台移动。
 */
void StartCamTestTask(void *argument) {                //1093和1094两处需要删除，仅供视觉调试使用
    vTaskDelay(pdMS_TO_TICKS(500));

    /* ---- 0. DRV8803 + 舵机 + 电磁阀初始化 ---- */
    DRV8803_Init();
    DRV8803_EnableChip(1, true);   /* U12 12V 芯片使能 */
    DRV8803_EnableChip(2, true);   /* U13 24V 芯片使能（电磁阀） */
    Coord_Init();                  /* 座标核心须在任何运动前初始化 */
    Servo_Init(&htim2);            /* Z轴舵机 TIM2_CH3 */
    DRV8803_SetOutput(&Port_12VO4, true);  /* 舵机上电 (12VO4) */
    Valve_Off();                        /* 电磁阀初始关断 (PA6=LOW) */
    osDelay(300);
	
    /* TMC2209 (R轴) 初始化 */
    if (!TMC_Init()) {
        PrintDebug("[CamTest] TMC_Init FAILED!\r\n");
    }
    TMC_SetEnable(false);          /* ENN 低有效: HIGH=关闭 */
		
//            LowerCam_Light_On();  /* 下相机补光灯 */
//            Pump_On();

    /* 从 Flash 加载标定值 */
    { uint8_t _dummy[4]; W25Q64_Read(0, _dummy, 4); }  /* 唤醒 W25Q64，清除热复位残留状态 */
    if (Calib_Load(&g_calib) != 0) {
        PrintDebug("[CamTest] Flash read failed, using defaults.\r\n");
    }
    PrintDebug("[CamTest] Calib: z_safe=%.1f z_pick=%.1f z_place=%.1f\r\n",
               (double)g_calib.z_safe_angle, (double)g_calib.z_pick_angle, (double)g_calib.z_place_angle);
    PrintDebug("[CamTest] Calib: scatter=(%ld,%ld) size=%ld\r\n",
               (long)g_calib.scatter_x_steps, (long)g_calib.scatter_y_steps, (long)g_calib.scatter_size_steps);
    PrintDebug("[CamTest] Calib: heat=(%ld,%ld)-(%ld,%ld) botcam=(%ld,%ld)\r\n",
               (long)g_calib.heat_platform_x_min, (long)g_calib.heat_platform_y_min,
               (long)g_calib.heat_platform_x_max, (long)g_calib.heat_platform_y_max,
               (long)g_calib.bottom_cam_x_steps, (long)g_calib.bottom_cam_y_steps);
    PrintDebug("[CamTest] Calib: nozzle_off=(%ld,%ld) cam_p1=%.3f cam_p3=%.3f\r\n",
               (long)g_calib.cam_to_nozzle_dx_steps, (long)g_calib.cam_to_nozzle_dy_steps,
               (double)g_calib.cam_p1_val_to_steps, (double)g_calib.cam_p3_val_to_steps);

    /* 强制覆写 Z 轴高度，不受 Flash 标定值影响 */
    g_calib.z_safe_angle  = 75.0f;
    g_calib.z_pick_angle  = 116.0f;
    g_calib.z_place_angle = 116.0f;

    /* 散料区未标定时回退到已知有效值, 并预计算单元格 + 子扫描位 */
    if (g_calib.scatter_size_steps == 0) {
        g_calib.scatter_x_steps    = 58880;
        g_calib.scatter_y_steps    = -25600;
        g_calib.scatter_size_steps = 37888;
        PrintDebug("[CamTest] Scatter area uncalibrated, using restore defaults.\r\n");
    }
    scatter_init_cells();

    /* 舵机归安全角度 */
    Servo_SetAngle(2, 75.0f);
    osDelay(300);


    /* ---- 1. CAN + 电机初始化 ---- */
    CAN_Init(&hfdcan1, NULL);
    osDelay(200);
    Motor_Init();
    osDelay(200);

    /* ---- 2. 视觉模块初始化 ---- */
    Vision_Init();

    /* ---- P0 handshake with MaixCAM ---- */
    if (!Vision_Handshake(5000)) {
        PrintDebug("[CamTest] P0 handshake FAILED, task suspended.\r\n");
        vTaskSuspend(NULL);
    }
    osDelay(100);
    PrintDebug("========================================\r\n");
    PrintDebug("  Camera + Motor Interactive Test\r\n");
    PrintDebug("  USART2 -> MaixCam  |  CAN -> X1/X2/Y\r\n");
    PrintDebug("  PX_TO_STEPS=%.1f  MM10000_TO_STEPS=%.1f\r\n",
               CAM_PX_TO_STEPS, CAM_MM10000_TO_STEPS);
    PrintDebug("========================================\r\n");


    /* ---- P2: Mark 点建系测试 (完整流程，与 Host_Task mark_align_step 一致) ---- */
    PrintDebug("\r\n--- Test: P2 Full (Mark alignment) ---\r\n");
    {
        Component_t test_marks[3];
        memset(test_marks, 0, sizeof(test_marks));
        test_marks[0].target_x = 5.0f;   test_marks[0].target_y = 5.0f;
        test_marks[1].target_x = 20.0f;  test_marks[1].target_y = 5.0f;
        test_marks[2].target_x = 5.0f;   test_marks[2].target_y = 20.0f;
        if (!cam_p2_full_test_run(test_marks, 3, CAM_TEST_TIMEOUT_P2)) {
            PrintDebug("[CAM_TEST] P2 FULL FAILED\r\n");
        } else {
            PrintDebug("[CAM_TEST] P2 FULL PASSED\r\n");
        }
    }
    /* ---- P1: 散料区元件检测 + 吸取 (与 Host_Task 相同流程) ---- */
    PrintDebug("\r\n--- Test: P1 scatter find + pick ---\r\n");
    {
        int p1_scan_pos = 0;
        int p1_retry = 0;
        int p1_align_iter = 0;   /* P1 迭代对齐计数 */
        float p1_angle = 0.0f;
        bool p1_done = false;
        bool p1_ok = false;
        uint32_t p1_start = osKernelGetTickCount();
        const uint32_t P1_TEST_TIMEOUT_MS = 30000;

        /* 移到散料区: 加 cam→nozzle 偏置, 让摄像头(而非吸嘴)对准 cell 0 */
        {
            int32_t tx = g_scatter_subpos[0][0][0] + g_calib.cam_to_nozzle_dx_steps;
            int32_t ty = g_scatter_subpos[0][0][1] + g_calib.cam_to_nozzle_dy_steps;
            MachineCoord_t mc = Coord_Get();
            PrintDebug("[CAM_TEST] Current: (%ld,%ld) -> Target: (%ld,%ld) cam_off(%ld,%ld)\r\n",
                       (long)mc.x, (long)mc.y, (long)tx, (long)ty,
                       (long)g_calib.cam_to_nozzle_dx_steps, (long)g_calib.cam_to_nozzle_dy_steps);
            safe_move_to(tx, ty, PNP_SPEED, PNP_ACC);
            mc = Coord_Get();
            PrintDebug("[CAM_TEST] After move: (%ld,%ld)\r\n", (long)mc.x, (long)mc.y);
        }
        Vision_Start(VCMD_P1, 0);  /* class_id=0 (ccapt) */

        while (!p1_done) {
            UART_Driver_Process();
            uint32_t elapsed = osKernelGetTickCount() - p1_start;
            if (elapsed >= pdMS_TO_TICKS(P1_TEST_TIMEOUT_MS)) {
                PrintDebug("[CAM_TEST] P1 TIMEOUT\r\n");
                Vision_ForceIdle();
                break;
            }

            VisionState_t vs = Vision_GetState();
            const VisionResult_t *r = Vision_GetResult();

            switch (vs) {
            case VISION_GOT_CATEGORY_QUERY:
                Vision_ClsReply();
                break;

            case VISION_GOT_STOP:
                p1_scan_pos = 0;
                Vision_Go();
                break;

            case VISION_GOT_POS: {
                /* 迭代对齐: 偏移已是电机步数, 修正后 go 复测; 5 次上限强制结束 */
                int32_t dx_s = -(int32_t)(r->dy);
                int32_t dy_s = -(int32_t)(r->dx);
                PrintDebug("[CAM_TEST] P1 pos: offset(%ld,%ld) steps\r\n", (long)r->dx, (long)r->dy);
                if (dx_s != 0 || dy_s != 0) {
                    safe_move_to(Coord_Get().x + dx_s, Coord_Get().y + dy_s,
                                 PNP_SPEED_FINE, PNP_ACC_FINE);
                }
                p1_align_iter++;
                if (p1_align_iter >= 5) {
                    PrintDebug("[CAM_TEST] P1 align %d iters, force done\r\n", p1_align_iter);
                    if (g_calib.cam_to_nozzle_dx_steps != 0 || g_calib.cam_to_nozzle_dy_steps != 0) {
                        safe_move_to(Coord_Get().x - g_calib.cam_to_nozzle_dx_steps,
                                     Coord_Get().y - g_calib.cam_to_nozzle_dy_steps,
                                     PNP_SPEED_FINE, PNP_ACC_FINE);
                    }
                    p1_done = true;
                    p1_ok = true;
                } else {
                    Vision_Go();
                }
                break;
            }

            case VISION_DONE:
                p1_angle = r->angle_valid ? (float)r->angle_x100 / 100.0f : 0.0f;
                PrintDebug("[CAM_TEST] P1 DONE, angle=%.2f deg\r\n", (double)p1_angle);
                /* 偏置补偿: 摄像头→吸嘴 */
                if (g_calib.cam_to_nozzle_dx_steps != 0 || g_calib.cam_to_nozzle_dy_steps != 0) {
                    safe_move_to(Coord_Get().x - g_calib.cam_to_nozzle_dx_steps,
                                 Coord_Get().y - g_calib.cam_to_nozzle_dy_steps,
                                 PNP_SPEED_FINE, PNP_ACC_FINE);
                }
                p1_done = true;
                p1_ok = true;
                break;

            case VISION_ERROR: {
                const char *err = Vision_GetError();
                PrintDebug("[CAM_TEST] P1 error: %s\r\n", err);
                if (strcmp(err, "err1_5") == 0 && p1_scan_pos < SCATTER_SUBPOS - 1) {
                    p1_scan_pos++;
                    safe_move_to(g_scatter_subpos[0][p1_scan_pos][0] + g_calib.cam_to_nozzle_dx_steps,
                                 g_scatter_subpos[0][p1_scan_pos][1] + g_calib.cam_to_nozzle_dy_steps,
                                 PNP_SPEED_FINE, PNP_ACC_FINE);
                    Vision_Start(VCMD_P1, 0);
                } else if (p1_retry < 3 && strcmp(err, "err1_5") != 0) {
                    p1_retry++;
                    Vision_Start(VCMD_P1, 0);
                } else {
                    p1_done = true;  /* 失败退出, p1_ok 保持 false */
                }
                break;
            }

            default:
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(20));
        }

        if (p1_ok) 
//					if (1)
						{
            /* P1 成功 → 吸取 */
            PrintDebug("[CAM_TEST] P1 PASSED, picking...\r\n");
            osDelay(800);  /* 等待电机完全停止后再吸取 */
            if (pick_component()) {
                PrintDebug("[CAM_TEST] PICK OK, angle=%.2f deg\r\n", (double)p1_angle);
                osDelay(500);  /* 等待真空稳定, 避免移动时元件脱落 */

                /* R轴矫正: P1识别完成，吸取稳定后再旋转 */
                host_start_r_correction(Vision_GetResult(), "P1");
                while (r_axis_state() == R_BUSY) { r_axis_poll(); osDelay(1); }
                osDelay(500);
                r_axis_set_zero();

                /* ---- P3: 下相机偏移检测 (与 Host_Task offset_check_step 一致) ---- */
                PrintDebug("\r\n--- Test: P3 bottom cam verify ---\r\n");
                {
                    bool p3_done = false;
                    bool p3_ok = false;
                    float p3_angle = 0.0f;
                    int p3_align_iter = 0;   /* P3 迭代对齐计数 */
                    uint32_t p3_start = osKernelGetTickCount();
                    const uint32_t P3_TEST_TIMEOUT_MS = 30000;

                    /* 移动到下相机 */
                    safe_move_to(g_calib.bottom_cam_x_steps, g_calib.bottom_cam_y_steps,
                                 PNP_SPEED, PNP_ACC);
                    LowerCam_Light_On();
									                    Pump_On();

                    Vision_Start(VCMD_P3, 0);

                    while (!p3_done) {
                        UART_Driver_Process();
                        uint32_t elapsed = osKernelGetTickCount() - p3_start;
                        if (elapsed >= pdMS_TO_TICKS(P3_TEST_TIMEOUT_MS)) {
                            PrintDebug("[CAM_TEST] P3 TIMEOUT\r\n");
                            Vision_ForceIdle();
                            break;
                        }

                        VisionState_t vs = Vision_GetState();
                        const VisionResult_t *r = Vision_GetResult();

                        switch (vs) {
                        case VISION_GOT_ERR_RETRY:
                            Vision_Go();
                            break;

                        case VISION_GOT_POS: {
                            /* 迭代对齐: 偏移已是电机步数, 修正后 go 复测; 5 次上限强制结束 */
                            int32_t dx_s = (int32_t)(r->dy);
                            int32_t dy_s = -(int32_t)(r->dx);
                            PrintDebug("[CAM_TEST] P3 pos: offset(%ld,%ld) steps\r\n", (long)r->dx, (long)r->dy);
                            if (dx_s != 0 || dy_s != 0) {
                                safe_move_to(Coord_Get().x + dx_s, Coord_Get().y + dy_s,
                                             PNP_SPEED_FINE, PNP_ACC_FINE);
                            }
                            p3_align_iter++;
                            if (p3_align_iter >= 5) {
                                PrintDebug("[CAM_TEST] P3 align %d iters, force done\r\n", p3_align_iter);
                                p3_done = true;
                                p3_ok = true;
                            } else {
                                Vision_Go();
                            }
                            break;
                        }

                        case VISION_DONE:
                            p3_angle = r->angle_valid ? (float)r->angle_x100 / 100.0f : 0.0f;
                            PrintDebug("[CAM_TEST] P3 DONE, residual angle=%.2f deg\r\n", (double)p3_angle);
                            host_start_r_correction(r, "P3");
                            while (r_axis_state() == R_BUSY) { r_axis_poll(); osDelay(1); }
                            r_axis_set_zero();
                            p3_done = true;
                            p3_ok = true;
                            break;

                        case VISION_ERROR: {
                            const char *err = Vision_GetError();
                            PrintDebug("[CAM_TEST] P3 error: %s\r\n", err);
                            if (err[0]=='e' && err[1]=='r' && err[2]=='r' &&
                                err[3]=='3' && err[4]=='_' && err[5]=='8' && err[6]=='\0') {
                                PrintDebug("[CAM_TEST] P3 nozzle empty!\r\n");
                            }
                            p3_done = true;
                            break;
                        }

                        default:
                            break;
                        }
                        vTaskDelay(pdMS_TO_TICKS(20));
                    }

                    LowerCam_Light_Off();

                    if (p3_ok) {
                        PrintDebug("[CAM_TEST] P3 PASSED, angle=%.2f deg\r\n", (double)p3_angle);
                    } else {
                        PrintDebug("[CAM_TEST] P3 FAILED\r\n");
                    }

                    /* 释放元件 (Pump_Off 内部已含 Valve_On→1s吹气→Valve_Off) */
//                    Pump_Off();
                }} else {
                PrintDebug("[CAM_TEST] PICK FAILED!\r\n");
//                Pump_Off();
            }
        }
		else {
            PrintDebug("[CAM_TEST] P1 FAILED\r\n");
        }
    }    

    PrintDebug("\r\n=== Camera + Motor Interactive Test Complete ===\r\n");
    vTaskSuspend(NULL);
}

const osThreadAttr_t espTestTask_attributes = {
    .name = "ESPTest",
    .stack_size = 1024,
    .priority = osPriorityNormal,
};

/**
 * @brief  ESP32 通信测试任务
 * @note   上电后依次执行 8 项测试: 硬件复位、SPI 链路、协议自检、WiFi 开关、故障查询、数据推送
 *         每项输出 PASS/FAIL，全部通过后挂起任务
 */
void StartESPTestTask(void *argument)
{
    static uint8_t s_tx_buf[128];
    static uint8_t s_rx_buf[128];
    static uint8_t s_pass_count;
    static uint8_t s_fail_count;
    static char    s_fail_items[10][32];
    uint8_t all_ok;
    int i;

    s_pass_count = 0;
    s_fail_count = 0;
    memset(s_fail_items, 0, sizeof(s_fail_items));

    PrintDebug("\r\n");
    PrintDebug("========================================\r\n");
    PrintDebug("  ESP32 Communication Test Suite v1.0\r\n");
    PrintDebug("  SPI4 (PE2/PE5/PE6) CS=PE3 RST=PC13\r\n");
    PrintDebug("========================================\r\n\r\n");

    /* ---- T1: 硬件复位 ---- */
    PrintDebug("--- T1: Hardware Reset + GPIO Init ---\r\n");
    ESP_GPIO_Init();
    ESP_HardReset();
    osDelay(1000);
    s_pass_count++;
    PrintDebug("[ESP_TEST] T1-HardReset ... PASS\r\n");

    /* ---- T2: SPI 链路检测 ---- */
    PrintDebug("\r\n--- T2: SPI Loopback Check ---\r\n");
    all_ok = 0;
    for (i = 0; i < 3; i++) {
        ESP_BuildHeartbeatPacket(s_tx_buf);
        int ret = ESP_SPI_Transfer(s_tx_buf, s_rx_buf);
        if (ret == 0) {
            uint8_t all_ff = 1;
            for (int j = 0; j < 128; j++) {
                if (s_rx_buf[j] != 0xFF) { all_ff = 0; break; }
            }
            if (!all_ff) {
                PrintDebug("[ESP_TEST] SPI rx[0]=0x%02X (slave responding)\r\n", s_rx_buf[0]);
                all_ok = 1;
                break;
            }
        }
        osDelay(100);
    }
    if (all_ok) {
        s_pass_count++;
        PrintDebug("[ESP_TEST] T2-SPI ... PASS\r\n");
    } else {
        s_fail_count++;
        if (s_fail_count <= 10) memcpy(s_fail_items[s_fail_count-1], "T2-SPI", 7);
        PrintDebug("[ESP_TEST] T2-SPI ... FAIL (MISO all 0xFF)\r\n");
    }

    /* ---- T3: 协议自检 (离线) ---- */
    PrintDebug("\r\n--- T3: Protocol Self-Check ---\r\n");
    all_ok = 1;
    {
        uint8_t buf[128];
        char tmp[16];
        int len;

        /* 数据包 */
        ESP_BuildDataPacket(buf, ESP_SUB_PROGRESS, "12/50", 5);
        if (buf[0] != 0x10 || buf[1] != 0x01 || buf[2] != 5) {
            PrintDebug("[ESP_TEST] DataPacket header: %02X %02X %02X\r\n", buf[0], buf[1], buf[2]);
            all_ok = 0;
        }
        /* 控制包 */
        ESP_BuildControlPacket(buf, ESP_SUB_WIFI_ON);
        if (buf[0] != 0x20 || buf[2] != 0) {
            PrintDebug("[ESP_TEST] ControlPacket: %02X %02X\r\n", buf[0], buf[2]);
            all_ok = 0;
        }
        /* 查询包 */
        ESP_BuildQueryPacket(buf, ESP_SUB_QUERY_FAULT);
        if (buf[0] != 0x30 || buf[1] != 0x01) {
            PrintDebug("[ESP_TEST] QueryPacket: %02X %02X\r\n", buf[0], buf[1]);
            all_ok = 0;
        }
        /* 解包: 模拟 FAULT 响应 */
        memset(buf, 0, 128);
        buf[0] = 0x00;
        buf[1] = 0xF1;
        buf[2] = 2;
        buf[3] = '0'; buf[4] = 'A';
        buf[126] = 5;
        if (ESP_GetResponseType(buf) != ESP_RESP_FAULT) all_ok = 0;
        uint8_t plen;
        const char *pl = ESP_GetResponsePayload(buf, &plen);
        if (plen != 2 || pl[0] != '0' || pl[1] != 'A') all_ok = 0;
        if (ESP_GetResponseSeq(buf) != 5) all_ok = 0;

        /* FormatTemp */
        len = ESP_FormatTemp(tmp, sizeof(tmp), 853);
        if (len != 4 || tmp[0]!='8' || tmp[1]!='5' || tmp[2]!='.' || tmp[3]!='3') all_ok = 0;

        /* FormatProgress */
        len = ESP_FormatProgress(tmp, sizeof(tmp), 32, 50);
        if (len != 5 || tmp[0]!='3' || tmp[1]!='2' || tmp[2]!='/' || tmp[3]!='5' || tmp[4]!='0') all_ok = 0;

        /* StateToString */
        if (strcmp(ESP_StateToString(0,0,0,0), "Waiting") != 0) all_ok = 0;
        if (strcmp(ESP_StateToString(1,0,0,0), "SMTing") != 0) all_ok = 0;
    }
    if (all_ok) {
        s_pass_count++;
        PrintDebug("[ESP_TEST] T3-Protocol ... PASS\r\n");
    } else {
        s_fail_count++;
        if (s_fail_count <= 10) memcpy(s_fail_items[s_fail_count-1], "T3-Protocol", 12);
        PrintDebug("[ESP_TEST] T3-Protocol ... FAIL\r\n");
    }

    /* ---- T4: WiFi 开启 ---- */
    PrintDebug("\r\n--- T4: WiFi ON ---\r\n");
    all_ok = 0;
    {
        uint32_t start = osKernelGetTickCount();
        uint8_t retry_count = 0;
        while ((osKernelGetTickCount() - start) < pdMS_TO_TICKS(10000)) {
            retry_count++;
            PrintDebug("[ESP_TEST] T4 send WIFI_ON (#%d)\r\n", retry_count);
            /* 连续发 2 次 WIFI_ON (间隔 50ms), 确保 ESP32 rx_buffer 稳定为 0x20 0x01 */
            ESP_BuildControlPacket(s_tx_buf, ESP_SUB_WIFI_ON);
            ESP_SPI_Transfer(s_tx_buf, s_rx_buf);
            osDelay(50);
            ESP_BuildControlPacket(s_tx_buf, ESP_SUB_WIFI_ON);
            ESP_SPI_Transfer(s_tx_buf, s_rx_buf);
            /* 空等 800ms, 不传任何 SPI 数据, 让 ESP32 主循环有充足时间读取 rx_buffer */
            osDelay(800);
            /* 发心跳查 ESP 响应 */
            ESP_BuildHeartbeatPacket(s_tx_buf);
            ESP_SPI_Transfer(s_tx_buf, s_rx_buf);
            uint8_t rt = ESP_GetResponseType(s_rx_buf);
            PrintDebug("[ESP_TEST] T4 check #%d: rx[0]=%02X rx[1]=%02X\r\n",
                       retry_count, s_rx_buf[0], rt);
            if (s_rx_buf[0] == 0x00) {
                if (rt == ESP_RESP_WIFI_STATUS || rt == ESP_RESP_COMPOUND) {
                    uint8_t plen2; const char *pl2 = ESP_GetResponsePayload(s_rx_buf, &plen2);
                    if (plen2 > 0 && pl2[0] == '1') g_esp_wifi_connected = 1;
                    if (g_esp_wifi_connected) { all_ok = 1; break; }
                }
                if (rt == ESP_RESP_FAULT) {
                    uint8_t plen2; const char *pl2 = ESP_GetResponsePayload(s_rx_buf, &plen2);
                    PrintDebug("[ESP_TEST] T4 WiFi fault: %.*s\r\n", plen2, pl2);
                }
            }
        }
    }
    if (all_ok) {
        s_pass_count++;
        PrintDebug("[ESP_TEST] T4-WiFiON ... PASS\r\n");
    } else {
        s_fail_count++;
        if (s_fail_count <= 10) memcpy(s_fail_items[s_fail_count-1], "T4-WiFiON", 10);
        PrintDebug("[ESP_TEST] T4-WiFiON ... FAIL (no connection)\r\n");
    }

    /* ---- T5: WiFi ?? ---- */
    PrintDebug("\r\n--- T5: WiFi OFF ---\r\n");
    ESP_BuildControlPacket(s_tx_buf, ESP_SUB_WIFI_OFF);
    ESP_SPI_Transfer(s_tx_buf, s_rx_buf);
    g_esp_wifi_enabled  = 0;
    g_esp_wifi_connected = 0;
    osDelay(1500);
    ESP_BuildHeartbeatPacket(s_tx_buf);
    ESP_SPI_Transfer(s_tx_buf, s_rx_buf);
    osDelay(200);  /* ?? ESP ??? WiFi OFF */
    ESP_BuildHeartbeatPacket(s_tx_buf);
    ESP_SPI_Transfer(s_tx_buf, s_rx_buf);
    {
        uint8_t rt = ESP_GetResponseType(s_rx_buf);
        if (rt == ESP_RESP_IDLE || rt == ESP_RESP_WIFI_STATUS) {
            s_pass_count++;
            PrintDebug("[ESP_TEST] T5-WiFiOFF ... PASS (rx[1]=%02X)\r\n", rt);
        } else {
            s_fail_count++;
            if (s_fail_count <= 10) memcpy(s_fail_items[s_fail_count-1], "T5-WiFiOFF", 11);
            PrintDebug("[ESP_TEST] T5-WiFiOFF ... FAIL (rx[1]=%02X)\r\n", rt);
        }
    }
/* ---- T6: ???? ---- */
    PrintDebug("\r\n--- T6: Fault Query ---\r\n");
    ESP_BuildQueryPacket(s_tx_buf, ESP_SUB_QUERY_FAULT);
    ESP_SPI_Transfer(s_tx_buf, s_rx_buf);
    osDelay(200);
    ESP_BuildHeartbeatPacket(s_tx_buf);
    ESP_SPI_Transfer(s_tx_buf, s_rx_buf);
    all_ok = 0;
    {
        uint8_t rt = ESP_GetResponseType(s_rx_buf);
        if (rt == ESP_RESP_FAULT) {
            uint8_t plen2; const char *pl2 = ESP_GetResponsePayload(s_rx_buf, &plen2);
            PrintDebug("[ESP_TEST] Fault reported: %.*s\r\n", plen2, pl2);
            all_ok = 1;
        } else if (rt == ESP_RESP_IDLE) {
            all_ok = 1;
        } else {
            uint32_t start2 = osKernelGetTickCount();
            while ((osKernelGetTickCount() - start2) < pdMS_TO_TICKS(1000)) {
                ESP_BuildHeartbeatPacket(s_tx_buf);
                ESP_SPI_Transfer(s_tx_buf, s_rx_buf);
                uint8_t rt2 = ESP_GetResponseType(s_rx_buf);
                if (rt2 == ESP_RESP_FAULT || rt2 == ESP_RESP_IDLE) { all_ok = 1; break; }
                osDelay(200);
            }
        }
    }
    if (all_ok) {
        s_pass_count++;
        PrintDebug("[ESP_TEST] T6-FaultQuery ... PASS\r\n");
    } else {
        s_fail_count++;
        if (s_fail_count <= 10) memcpy(s_fail_items[s_fail_count-1], "T6-FaultQuery", 14);
        PrintDebug("[ESP_TEST] T6-FaultQuery ... FAIL\r\n");
    }
/* ---- T7: WiFi ???? ---- */
    PrintDebug("\r\n--- T7: WiFi Query ---\r\n");
    ESP_BuildQueryPacket(s_tx_buf, ESP_SUB_QUERY_WIFI);
    ESP_SPI_Transfer(s_tx_buf, s_rx_buf);
    osDelay(200);
    ESP_BuildHeartbeatPacket(s_tx_buf);
    ESP_SPI_Transfer(s_tx_buf, s_rx_buf);
    all_ok = 0;
    {
        uint8_t rt = ESP_GetResponseType(s_rx_buf);
        if (rt == ESP_RESP_IDLE || rt == ESP_RESP_WIFI_STATUS || rt == ESP_RESP_COMPOUND) {
            all_ok = 1;
        } else {
            uint32_t start2 = osKernelGetTickCount();
            while ((osKernelGetTickCount() - start2) < pdMS_TO_TICKS(2000)) {
                ESP_BuildHeartbeatPacket(s_tx_buf);
                ESP_SPI_Transfer(s_tx_buf, s_rx_buf);
                uint8_t rt2 = ESP_GetResponseType(s_rx_buf);
                if (rt2 == ESP_RESP_IDLE || rt2 == ESP_RESP_WIFI_STATUS || rt2 == ESP_RESP_COMPOUND) { all_ok = 1; break; }
                osDelay(200);
            }
        }
    }
    if (all_ok) {
        s_pass_count++;
        PrintDebug("[ESP_TEST] T7-WiFiQuery ... PASS\r\n");
    } else {
        s_fail_count++;
        if (s_fail_count <= 10) memcpy(s_fail_items[s_fail_count-1], "T7-WiFiQuery", 13);
        PrintDebug("[ESP_TEST] T7-WiFiQuery ... FAIL\r\n");
    }
/* ---- T8: ???? ---- */
    PrintDebug("\r\n--- T8: Data Push ---\r\n");
    all_ok = 1;
    {
        static const uint8_t subs[] = {0x01, 0x02, 0x03, 0x04};
        static const char *names[] = {"PROGRESS","SMT_STATUS","HEATER_STATE","HEATER_TEMP"};
        for (i = 0; i < 4; i++) {
            char payload[16]; int pl;
            switch (subs[i]) {
            case 0x01: pl = ESP_FormatProgress(payload, sizeof(payload), 0, 100); break;
            case 0x02: pl = 7; memcpy(payload, "Waiting", 7); break;
            case 0x03: payload[0]='0'; pl=1; break;
            case 0x04: pl = ESP_FormatTemp(payload, sizeof(payload), 250); break;
            default: pl=0; break;
            }
            ESP_BuildDataPacket(s_tx_buf, subs[i], payload, (uint8_t)pl);
            int ret = ESP_SPI_Transfer(s_tx_buf, s_rx_buf);
            /* 第1次交换: 发送数据推送, ESP 回复的是上一次心跳的响应 (应为 IDLE) */
            osDelay(50);
            /* 第2次交换: 发送心跳, ESP 回复的是对数据推送的回声 (rx[0]=0x10 为正常全双工行为) */
            ESP_BuildHeartbeatPacket(s_tx_buf);
            int ret2 = ESP_SPI_Transfer(s_tx_buf, s_rx_buf);
            osDelay(50);
            /* 第3次交换: 发送心跳, ESP 回复的是对第2次心跳的真实响应 (应为 rx[0]=0x00 rx[1]=0x00 IDLE) */
            ESP_BuildHeartbeatPacket(s_tx_buf);
            int ret3 = ESP_SPI_Transfer(s_tx_buf, s_rx_buf);
            if (ret != 0 || ret2 != 0 || ret3 != 0) {
                PrintDebug("[ESP_TEST] Data push %s: SPI err %d/%d/%d\r\n", names[i], ret, ret2, ret3);
                all_ok = 0;
            } else {
                uint8_t rt = ESP_GetResponseType(s_rx_buf);
                if (s_rx_buf[0] == 0x00 && rt == ESP_RESP_IDLE) {
                    PrintDebug("[ESP_TEST] Data push %s: sent %d OK\r\n", names[i], pl);
                } else {
                    PrintDebug("[ESP_TEST] Data push %s: sent %d rx[0]=%02X rx[1]=%02X (expected IDLE)\r\n",
                               names[i], pl, s_rx_buf[0], rt);
                    all_ok = 0;
                }
            }
            osDelay(500);
        }
    }
    if (all_ok) {
        s_pass_count++;
        PrintDebug("[ESP_TEST] T8-DataPush ... PASS\r\n");
    } else {
        s_fail_count++;
        if (s_fail_count <= 10) memcpy(s_fail_items[s_fail_count-1], "T8-DataPush", 12);
        PrintDebug("[ESP_TEST] T8-DataPush ... FAIL\r\n");
    }
/* ---- ?? ---- */
    ESP_BuildControlPacket(s_tx_buf, ESP_SUB_WIFI_OFF);
    ESP_SPI_Transfer(s_tx_buf, s_rx_buf);
    g_esp_wifi_enabled  = 0;
    g_esp_wifi_connected = 0;

    /* ---- ?? ---- */
    PrintDebug("\r\n========================================\r\n");
    PrintDebug("  ESP Test Complete: PASS=%d FAIL=%d\r\n", s_pass_count, s_fail_count);
    if (s_fail_count > 0) {
        PrintDebug("  Failed items:\r\n");
        for (i = 0; i < s_fail_count && i < 10; i++) {
            PrintDebug("    - %s\r\n", s_fail_items[i]);
        }
    }
    PrintDebug("========================================\r\n\r\n");

    /* ---- ?????? (ESP32 ???? 500ms ??) ---- */
    PrintDebug("[ESP_TEST] Entering heartbeat loop (500ms)\r\n");
    {
        uint16_t hb_count = 0;
        for (;;) {
            ESP_BuildHeartbeatPacket(s_tx_buf);
            ESP_SPI_Transfer(s_tx_buf, s_rx_buf);
            uint8_t rt = ESP_GetResponseType(s_rx_buf);
            hb_count++;
            /* ? 20 ? (10 ?) ???????? */
            if (hb_count % 20 == 0) {
                uint8_t plen; const char *pl = ESP_GetResponsePayload(s_rx_buf, &plen);
                PrintDebug("[ESP_TEST] HB #%d rx[1]=%02X payload=%.*s\r\n", hb_count, rt, plen, pl);
            } else if (rt != ESP_RESP_IDLE) {
                uint8_t plen; const char *pl = ESP_GetResponsePayload(s_rx_buf, &plen);
                PrintDebug("[ESP_TEST] HB rx[1]=%02X payload=%.*s\r\n", rt, plen, pl);
            }
            osDelay(500);
        }
    }

}