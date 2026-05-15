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
#define STEPS_PER_MM     3276.8f
#define JOG_MAX_STEPS    8388607
#define X1_ADDR          0x01
#define X2_ADDR          0x02
#define Y_ADDR           0x03


/* 外部变量：CAN接收队列（已在 driver_can.c 中定义） */
extern osMessageQueueId_t motor_event_queue;
extern UART_HandleTypeDef huart1; 
extern UART_HandleTypeDef huart3;
extern TIM_HandleTypeDef htim5;
static char s_debug_buf[128]; 

/* 测试点位（仅 X 轴坐标，一圈 = 16384） */
static const int32_t x_test_points[] = {
    0,              // 原点
    16384 * 5,      // 正向 5 圈
    16384 * 10,     // 正向 10 圈
    16384 * 5,      // 返回 5 圈
    0               // 回原点
};


// 定义一个任务句柄（可选，用于调试）
TaskHandle_t UartTestTaskHandle = NULL;

// 任务函数声明
void PrintDebug(const char* fmt, ...);
void StartUartTestTask(void *argument);
void StartMotorTestTask(void *argument);
// 调试函数：在串口上打印调试信息
void PrintDebug(const char* fmt, ...){
    va_list args;
    va_start(args, fmt);
    
    // 1. 格式化字符串到缓冲区
    // 使用 vsnprintf 确保不会写出 s_debug_buf 的边界
    int len = vsnprintf(s_debug_buf, sizeof(s_debug_buf), fmt, args);
    
    va_end(args);

    // 2. 发送串口
    if (len > 0) {
        // 注意：HAL_UART_Transmit 是阻塞的。
        // 如果你的打印频率非常高，建议改用 DMA 发送或串口空闲中断，
        // 否则可能会影响电机控制的实时性。
        // 这里为了简单，保持阻塞方式，但加了超时保护。
        HAL_UART_Transmit(&huart1, (uint8_t*)s_debug_buf, len, 50); 
    }
}


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
        TMC_SetSpeed(speed);
        PrintDebug("Speed set to %ld\r\n", speed);
        vTaskDelay(pdMS_TO_TICKS(2000));

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
     PrintDebug("--- IN2 Always ON Test ---\r\n");

    // 1. 配置所有芯片初始电平（GPIO 模式已在 CubeMX 配置）
    DRV8803_Dual_Config();

    // 2. 仅使能 U12 (12V)，U13 保持禁用
    DRV8803_EnableChip(1, true);   // U12 使能 (nENBL1 低)
    DRV8803_EnableChip(2, false);  // U13 禁用 (nENBL2 高)

    // 3. 清零所有通道（可选，确保初始状态正确）
    DRV8803_SetChipChannels(1, 0x00);
    DRV8803_SetChipChannels(2, 0x00);

    // 4. 打开 IN2 (对应全局通道 CH2)
    DRV8803_SetGlobalChannel(CH2, true);
    PrintDebug("IN2 (PE13) is now HIGH, 12V output enabled.\r\n");

    // 5. 主循环：仅维持 IN2 常开，并监控故障
    const TickType_t faultCheckPeriod = pdMS_TO_TICKS(100); // 100ms 检查一次
    for (;;)
    {
        // 检查 U12 故障
        if (DRV8803_IsChipFault(1))
        {	
						
            PrintDebug("[FAULT] U12 fault! Attempt recovery...\r\n");
            
            // 故障恢复流程（禁用 → 延时 → 复位 → 重检）
            DRV8803_HandleFault_RTOS(1);

            // 恢复后重新使能并打开 IN2
            DRV8803_EnableChip(1, true);
            DRV8803_SetGlobalChannel(CH2, true);
            PrintDebug("U12 re-enabled, IN2 restored.\r\n");
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
static void axis_stop(int32_t addr)
{
    uint8_t tx[8] = {0};
    tx[0] = 0xF5;
    tx[3] = 0x00;
    CAN_Transmit_Data(&hfdcan1, addr, tx, 7);
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
static int move_xy_relative(int32_t dx, int32_t dy, uint16_t speed, uint8_t acc,
                             int32_t *cur_x, int32_t *cur_y)
{
    int32_t target_x = *cur_x + dx;
    int32_t target_y = *cur_y + dy;

    if (target_x >  8388607) target_x =  8388607;
    if (target_x < -8388607) target_x = -8388607;
    if (target_y >  8388607) target_y =  8388607;
    if (target_y < -8388607) target_y = -8388607;

    osEventFlagsClear(evtAxesDone, EVENT_ALL_AXES | EVENT_ANY_ERROR);

    positionMode3Run(X1_ADDR, speed, acc, target_x);
    positionMode3Run(X2_ADDR, speed, acc, target_x);
    positionMode3Run(Y_ADDR,  speed, acc, target_y);
    motorSyncTrigger(0);

    uint32_t flags = osEventFlagsWait(evtAxesDone,
                                       EVENT_ALL_AXES | EVENT_ANY_ERROR,
                                       osFlagsWaitAny, 500);
    if (flags & EVENT_ANY_ERROR) {
        axis_stop(X1_ADDR);
        axis_stop(X2_ADDR);
        axis_stop(Y_ADDR);
        return -2;
    }
    if ((flags & EVENT_ALL_AXES) == EVENT_ALL_AXES) {
        *cur_x = target_x;
        *cur_y = target_y;
        return 0;
    }
    axis_stop(X1_ADDR);
    axis_stop(X2_ADDR);
    axis_stop(Y_ADDR);
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
    const uint8_t  acc   = 10;

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
                        jog_active = true;
                        positionMode3Run(X1_ADDR, (uint16_t)parsed.param, acc, JOG_MAX_STEPS);
                        positionMode3Run(X2_ADDR, (uint16_t)parsed.param, acc, JOG_MAX_STEPS);
                        motorSyncTrigger(0);
                        break;

                    case HCMD_MOVE_DOWN_START:
                        PrintDebug("[HostMotion] JOG DOWN speed=%.2f\r\n", parsed.param);
                        jog_active = true;
                        positionMode3Run(X1_ADDR, (uint16_t)parsed.param, acc, -JOG_MAX_STEPS);
                        positionMode3Run(X2_ADDR, (uint16_t)parsed.param, acc, -JOG_MAX_STEPS);
                        motorSyncTrigger(0);
                        break;

                    case HCMD_MOVE_LEFT_START:
                        PrintDebug("[HostMotion] JOG LEFT speed=%.2f\r\n", parsed.param);
                        jog_active = true;
                        positionMode3Run(Y_ADDR, (uint16_t)parsed.param, acc, JOG_MAX_STEPS);
                        motorSyncTrigger(0);
                        break;

                    case HCMD_MOVE_RIGHT_START:
                        PrintDebug("[HostMotion] JOG RIGHT speed=%.2f\r\n", parsed.param);
                        jog_active = true;
                        positionMode3Run(Y_ADDR, (uint16_t)parsed.param, acc, -JOG_MAX_STEPS);
                        motorSyncTrigger(0);
                        break;

                    case HCMD_MOVE_STOP:
                        PrintDebug("[HostMotion] STOP\r\n");
                        axis_stop(X1_ADDR);
                        axis_stop(X2_ADDR);
                        axis_stop(Y_ADDR);
												motorSyncTrigger(0); 
                        jog_active = false;
                        break;

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
            UART_ClearData(UART_CH1);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
