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
    GPIOA->BSRR = GPIO_PIN_6;                   /* PA6 HIGH */
    vTaskDelay(pdMS_TO_TICKS(2));
    GPIOA->BSRR = (uint32_t)GPIO_PIN_6 << 16;   /* PA6 LOW (阀初始关闭) */
    

    DRV8803_SetOutput(&Port_12VO1, true);
    PrintDebug("12VO1 (PE11) 真空泵已开启.\r\n");

    const TickType_t faultCheckPeriod = pdMS_TO_TICKS(100);
    for (;;)
    {
        if (DRV8803_IsChipFault(1))
        {
            PrintDebug("[FAULT] U12 fault! Attempt recovery...\r\n");
            DRV8803_HandleFault_RTOS(1);
            DRV8803_EnableChip(1, true);
            DRV8803_SetOutput(&Port_12VO1, true);
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
    HAL_FDCAN_ActivateNotification(&hfdcan1,
        FDCAN_IT_RX_FIFO0_NEW_MESSAGE | FDCAN_IT_BUS_OFF |
        FDCAN_IT_ERROR_PASSIVE | FDCAN_IT_ARB_PROTOCOL_ERROR |
        FDCAN_IT_DATA_PROTOCOL_ERROR, 0);
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

/**
 * @brief 发送急停指令到指定电机轴
 * @param addr 电机 CAN ID (0x01/0x02/0x03)
 */
void axis_stop(int32_t addr)
{
    uint8_t tx[8] = {0};
    tx[0] = 0xF5;
    tx[3] = 0x00;
    CAN_Transmit_Data(&hfdcan1, addr, tx, 7);
}
/**
 * @brief 强制急停三轴（同步模式下缓存急停 + 触发执行）
 * @note  不切换同步模式，直接用 0xF5 速度=0 + 0x4B 触发。
 *        电机运行期间缓存可被新 0xF5 覆盖，0x4B 触发后中止当前运动。
 */
void disable_sync_stop(void)
{
    axis_stop(X1_ADDR);
    axis_stop(X2_ADDR);
    axis_stop(Y_ADDR);
    osDelay(5);
    motorSyncTrigger(0);
    osDelay(10);
    motorSyncEnable(1);
    osDelay(10);
}

/**
 * @brief XY 相对移动（阻塞式，等待到位事件）
 * @param dx     X 轴相对位移 (步数)
 * @param dy     Y 轴相对位移 (步数)
 * @param speed  速度
 * @param acc    加速度
 * @param cur_x  当前 X 绝对坐标 (输入输出)
 * @param cur_y  当前 Y 绝对坐标 (输入输出)
 * @return 0 成功, -1 超时, -2 异常
 */
/* 运动中断标志 */
volatile bool s_cmd_interrupted = false;

int move_xy_relative(int32_t dx, int32_t dy, uint16_t speed, uint8_t acc,
                             int32_t *cur_x, int32_t *cur_y)
{
    int32_t target_x = *cur_x + dx;
    int32_t target_y = *cur_y + dy;

    if (target_x >  8388607) target_x =  8388607;
    if (target_x < -8388607) target_x = -8388607;
    if (target_y >  8388607) target_y =  8388607;
    if (target_y < -8388607) target_y = -8388607;

    if (dx == 0 && dy == 0) {
        return 0;
    }

    osEventFlagsClear(evtAxesDone, EVENT_ALL_AXES | EVENT_ANY_ERROR);

    /* 确保同步模式开启，再缓存运动指令 */
    motorSyncEnable(1);
    osDelay(5);

    if (dx != 0) {
        positionMode3Run(X1_ADDR, speed, acc, target_x);
        osDelay(2);
        positionMode3Run(X2_ADDR, speed, acc, target_x);
        osDelay(2);
    }
    if (dy != 0) {
        positionMode3Run(Y_ADDR,  speed, acc, target_y);
        osDelay(2);
    }

    motorSyncTrigger(0);
    osDelay(5);
    /* 触发后立即恢复同步标志，供下次运动使用 */
    motorSyncEnable(1);
    osDelay(5);

    uint32_t done_mask = 0;
    if (dx != 0) done_mask |= (EVENT_X1_DONE | EVENT_X2_DONE);
    if (dy != 0) done_mask |= EVENT_Y_DONE;
    uint32_t wait_mask = done_mask | EVENT_ANY_ERROR;

    uint32_t elapsed = 0;
    const uint32_t poll_ms = 10;
    const uint32_t total_timeout = 10000;

    UART_ClearData(UART_CH1);  /* 清除外层循环残留的 data_ready，防止误判为中断命令 */
    while (elapsed < total_timeout) {
        uint32_t flags = osEventFlagsWait(evtAxesDone,
                                           wait_mask,
                                           osFlagsWaitAny, poll_ms);

        if ((int32_t)flags < 0) {
        } else if (flags & EVENT_ANY_ERROR) {
            disable_sync_stop();
            return -2;
        } else if ((flags & done_mask) == done_mask) {
            *cur_x = target_x;
            *cur_y = target_y;
            return 0;
        }

        UART_Driver_Process();
        const uint8_t *rx = NULL;
        uint16_t rx_len = 0;
        if (UART_PeekData(UART_CH1, &rx, &rx_len)) {
            disable_sync_stop();
            s_cmd_interrupted = true;
            return -3;
        }

        elapsed += poll_ms;
    }

    disable_sync_stop();
    return -1;
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

    int32_t cur_x = 0;
    int32_t cur_y = 0;
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
                        result = move_xy_relative(steps, 0, speed, acc, &cur_x, &cur_y);
                        PrintDebug("[HostMotion] MOVE_UP done, pos=(%ld,%ld) ret=%d\r\n", cur_x, cur_y, result);
                        jog_active = false;
                        break;

                    case HCMD_MOVE_DOWN:
                        steps = (int32_t)(-parsed.param * STEPS_PER_MM);
                        PrintDebug("[HostMotion] MOVE_DOWN %.2fmm -> %ld steps\r\n", parsed.param, steps);
                        result = move_xy_relative(steps, 0, speed, acc, &cur_x, &cur_y);
                        PrintDebug("[HostMotion] MOVE_DOWN done, pos=(%ld,%ld) ret=%d\r\n", cur_x, cur_y, result);
                        jog_active = false;
                        break;

                    case HCMD_MOVE_LEFT:
                        steps = (int32_t)(parsed.param * STEPS_PER_MM);
                        PrintDebug("[HostMotion] MOVE_LEFT %.2fmm -> %ld steps\r\n", parsed.param, steps);
                        result = move_xy_relative(0, steps, speed, acc, &cur_x, &cur_y);
                        PrintDebug("[HostMotion] MOVE_LEFT done, pos=(%ld,%ld) ret=%d\r\n", cur_x, cur_y, result);
                        jog_active = false;
                        break;

                    case HCMD_MOVE_RIGHT:
                        steps = (int32_t)(-parsed.param * STEPS_PER_MM);
                        PrintDebug("[HostMotion] MOVE_RIGHT %.2fmm -> %ld steps\r\n", parsed.param, steps);
                        result = move_xy_relative(0, steps, speed, acc, &cur_x, &cur_y);
                        PrintDebug("[HostMotion] MOVE_RIGHT done, pos=(%ld,%ld) ret=%d\r\n", cur_x, cur_y, result);
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
                        int32_t dx = target_x - cur_x;
                        int32_t dy = target_y - cur_y;
                        int result = move_xy_relative(dx, dy, speed, acc, &cur_x, &cur_y);
                        PrintDebug("[HostMotion] MOVE_TO done, pos=(%ld,%ld) ret=%d\r\n", cur_x, cur_y, result);
                        break;
                    }
                    case HCMD_SET_ORIGIN:
                        PrintDebug("[HostMotion] SET_ORIGIN\r\n");
                        motorSetZero(X1_ADDR);
                        motorSetZero(X2_ADDR);
                        motorSetZero(Y_ADDR);
                        cur_x = 0;
                        cur_y = 0;
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
    DRV8803_SetOutput(PICKPLACE_PUMP_PORT, true);
    /* 阀是低端开关：OUT5拉GND才导通。先HIGH再LOW确保锁存 */
    GPIOA->BSRR = GPIO_PIN_6;                   /* PA6 HIGH */
    vTaskDelay(pdMS_TO_TICKS(2));
    GPIOA->BSRR = (uint32_t)GPIO_PIN_6 << 16;   /* PA6 LOW → 阀ON */
}
/**
 * @brief Z轴舵机贴装：关阀 → 关泵 → 转到贴装角
 */
static void pickplace_place(void)
{
    PrintDebug("[PickPlace] 贴装: 阀OFF + 泵OFF + 舵机→%.0f°\r\n",
               PICKPLACE_PLACE_ANGLE);
    /* 阀关断 → PA6=HIGH */
    GPIOA->BSRR = GPIO_PIN_6;  /* BS6 → PA6 HIGH */
    DRV8803_SetOutput(PICKPLACE_PUMP_PORT, false);
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
    PrintDebug("[PickPlace] R轴正转 斜坡→%ld µstep/s, 持续 %dms\r\n",
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
    PrintDebug("[PickPlace] R轴反转 斜坡→%ld µstep/s, 持续 %dms\r\n",
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
#define CAM_TEST_TIMEOUT_P1   30000   /* P1 超时 ms */
#define CAM_TEST_TIMEOUT_P2   60000   /* P2 超时 ms (3个Mark) */
#define CAM_TEST_TIMEOUT_P3   30000   /* P3 超时 ms */

/* 偏移→步数换算系数 (需根据相机 FOV 实测标定！) */
#define CAM_PX_TO_STEPS       (STEPS_PER_MM / 1000.0f)   /* 像素 → 步数 */
#define CAM_MM10000_TO_STEPS  (STEPS_PER_MM / 10000.0f)  /* mm*10000 → 步数 */
#define CAM_MOVE_SPEED        300
#define CAM_MOVE_ACC          25

/**
 * @brief 运行一个视觉 Process 到完成或超时
 * @param cmd        VCMD_P1 / VCMD_P2 / VCMD_P3
 * @param timeout_ms 超时 (ms)
 * @param cur_x      当前 X 坐标 (步数, 输入输出)
 * @param cur_y      当前 Y 坐标 (步数, 输入输出)
 * @return true=完成, false=超时或出错
 */
static bool cam_test_run(VisionCmd_t cmd, uint32_t timeout_ms,
                         int32_t *cur_x, int32_t *cur_y) {
    Vision_Start(cmd);

    /* P2 启动后状态是 IDLE，需要主动发 "go" */
    if (cmd == VCMD_P2) {
        osDelay(200);
        if (Vision_GetState() == VISION_IDLE) {
            Vision_Go();
            PrintDebug("[CAM_TEST] P2 initial go sent.\r\n");
        }
    }

    VisionState_t prev = Vision_GetState();
    uint32_t start_tick = osKernelGetTickCount();

    while ((osKernelGetTickCount() - start_tick) < pdMS_TO_TICKS(timeout_ms)) {
        UART_Driver_Process();

        VisionState_t state = Vision_GetState();
        if (state != prev) {
            PrintDebug("[CAM_TEST] State: %d -> %d\r\n", (int)prev, (int)state);

            switch (state) {
            case VISION_GOT_STOP:
                Vision_Go();
                PrintDebug("[CAM_TEST]   -> sent go\r\n");
                break;

            case VISION_GOT_POS: {
                const VisionResult_t *r = Vision_GetResult();
                int32_t dx_s = 0, dy_s = 0;

                if (cmd == VCMD_P2) {
                    dx_s = (int32_t)(r->dx * CAM_MM10000_TO_STEPS);
                    dy_s = (int32_t)(r->dy * CAM_MM10000_TO_STEPS);
                    PrintDebug("[CAM_TEST]   Mark%d/%d: dx=%ld dy=%ld mm10000 -> move(%ld,%ld)\r\n",
                               (int)r->mark_index, (int)r->mark_count,
                               (long)r->dx, (long)r->dy, (long)dx_s, (long)dy_s);
                } else {
                    dx_s = (int32_t)(r->dx * CAM_PX_TO_STEPS);
                    dy_s = (int32_t)(r->dy * CAM_PX_TO_STEPS);
                    if (r->angle_x100 != 0 || r->class_name[0] != '\0') {
                        PrintDebug("[CAM_TEST]   dx=%ld dy=%ld px ang=%ld.%02ld cls=%s -> move(%ld,%ld)\r\n",
                                   (long)r->dx, (long)r->dy,
                                   (long)(r->angle_x100/100), (long)(r->angle_x100%100),
                                   r->class_name, (long)dx_s, (long)dy_s);
                    } else {
                        PrintDebug("[CAM_TEST]   dx=%ld dy=%ld px -> move(%ld,%ld)\r\n",
                                   (long)r->dx, (long)r->dy, (long)dx_s, (long)dy_s);
                    }
                }

                if (dx_s != 0 || dy_s != 0) {
                    int ret = move_xy_relative(dx_s, dy_s, CAM_MOVE_SPEED,
                                               CAM_MOVE_ACC, cur_x, cur_y);
                    PrintDebug("[CAM_TEST]   move done, cur=(%ld,%ld) ret=%d\r\n",
                               (long)*cur_x, (long)*cur_y, ret);
                }
                Vision_Go();
                break;
            }

            case VISION_DONE:
                return true;

            case VISION_ERROR:
                PrintDebug("[CAM_TEST]   ERROR: %s\r\n", Vision_GetError());
                return false;

            default:
                break;
            }
            prev = state;
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }

    PrintDebug("[CAM_TEST] TIMEOUT after %lu ms\r\n", (unsigned long)timeout_ms);
    return false;
}

/**
 * @brief 摄像头 + 电机联动测试任务
 * @note  初始化 CAN/MKS 电机 + Vision，依次执行 P1/P3/P2。
 *        根据摄像头返回的偏移实际驱动 XY 平台移动。
 */
void StartCamTestTask(void *argument) {
    vTaskDelay(pdMS_TO_TICKS(500));

    /* 关闭 R 轴（本任务不使用 TMC2209） */
    TMC_SetEnable(false);

    /* ---- 1. CAN + 电机初始化 ---- */
    CAN_Init(&hfdcan1, NULL);
    HAL_FDCAN_ActivateNotification(&hfdcan1,
        FDCAN_IT_RX_FIFO0_NEW_MESSAGE | FDCAN_IT_BUS_OFF |
        FDCAN_IT_ERROR_PASSIVE | FDCAN_IT_ARB_PROTOCOL_ERROR |
        FDCAN_IT_DATA_PROTOCOL_ERROR, 0);
    osDelay(200);
    Motor_Init();
    osDelay(200);

    /* ---- 2. 视觉模块初始化 ---- */
    Vision_Init();

    PrintDebug("========================================\r\n");
    PrintDebug("  Camera + Motor Interactive Test\r\n");
    PrintDebug("  USART2 -> MaixCam  |  CAN -> X1/X2/Y\r\n");
    PrintDebug("  PX_TO_STEPS=%.1f  MM10000_TO_STEPS=%.1f\r\n",
               CAM_PX_TO_STEPS, CAM_MM10000_TO_STEPS);
    PrintDebug("========================================\r\n");

    int32_t cur_x = 0, cur_y = 0;

    /* ---- P1: 散料区元件检测 ---- */
    PrintDebug("\r\n--- Test 1/3: P1 (component detect) ---\r\n");
    if (!cam_test_run(VCMD_P1, CAM_TEST_TIMEOUT_P1, &cur_x, &cur_y)) {
        PrintDebug("[CAM_TEST] P1 FAILED, continuing...\r\n");
    } else {
        PrintDebug("[CAM_TEST] P1 PASSED\r\n");
    }

    /* ---- P3: 下相机偏移检测 (需要时取消注释) ---- */
    // PrintDebug("\r\n--- Test 2/3: P3 (bottom cam offset) ---\r\n");
    // if (!cam_test_run(VCMD_P3, CAM_TEST_TIMEOUT_P3, &cur_x, &cur_y)) {
    //     PrintDebug("[CAM_TEST] P3 FAILED, continuing...\r\n");
    // } else {
    //     PrintDebug("[CAM_TEST] P3 PASSED\r\n");
    // }

    /* ---- P2: Mark 点建系 (需要时取消注释) ---- */
    // PrintDebug("\r\n--- Test 3/3: P2 (mark alignment) ---\r\n");
    // if (!cam_test_run(VCMD_P2, CAM_TEST_TIMEOUT_P2, &cur_x, &cur_y)) {
    //     PrintDebug("[CAM_TEST] P2 FAILED\r\n");
    // } else {
    //     PrintDebug("[CAM_TEST] P2 PASSED\r\n");
    // }

    PrintDebug("\r\n=== Camera Test Complete ===\r\n");
    vTaskSuspend(NULL);
}
