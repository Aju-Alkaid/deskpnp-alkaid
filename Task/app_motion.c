#include "app_motion.h"
#include "driver_motor.h"
#include "driver_can.h"
#include "cmsis_os2.h"
#include <string.h>
#include <stdbool.h>
#include "driver_servo.h"
#include "driver_tmc2209.h"   
#include <math.h>   // 解决 fabsf 未声明
#include "app_test.h"

extern TIM_HandleTypeDef htim5;


//信号量

//事件组
osEventFlagsId_t evtAxesDone = NULL;   // 用于三轴到位同步

/* ---------- 电机地址定义 ---------- */
#define X1_ADDR   0x01
#define X2_ADDR   0x02
#define Y_ADDR    0x03

/* ---------- 舵机配置定义 ---------- */
#define SERVO_CH_Z   2        // 对应你在 Servo_Init 中配置的 channel 索引

#define ANGLE_UP     120.0f   // 吸嘴升起角度 (请根据实际机械调)
#define ANGLE_DOWN   60.0f    // 吸嘴下降角度

// 吸嘴气泵 GPIO (根据你的硬件修改)
#define NOZZLE_GPIO_PORT   GPIOE
#define NOZZLE_GPIO_PIN    GPIO_PIN_11

// R 轴参数（需根据实测调整）
#define R_MICROSTEPS    256
#define R_STEPS_PER_REV (200 * R_MICROSTEPS)  // 51200 微步/圈
#define R_ACCEL_DELAY   50                    // 加减速平滑延时（ms）

/* 协议常量 */
#define FUNC_ABS_POS   0xF5       // 坐标绝对运动功能码
#define STATUS_SYNC_RECV  0x05    // 同步模式：已接收指令，等待同步执行
#define STATUS_RUN_COMPLETE 0x02  // 运行完成
#define STATUS_START      0x01    // 普通模式：运行开始 (未使用同步时可能收到)

#define ACK_TIMEOUT_MS   2000      // 单轴到位超时 (ms)

/* ---------- 内部辅助函数 ---------- */

/**
 * @brief 发送单轴坐标绝对运动指令 (不等待)
 */
static void send_axis_abs(int32_t addr, int32_t abs_coord, uint16_t speed, uint8_t acc)
{
    // 坐标范围限制在 int24_t (-8388607 ~ 8388607)
    if (abs_coord >  8388607) abs_coord =  8388607;
    if (abs_coord < -8388607) abs_coord = -8388607;

    positionMode3Run(addr, speed, acc, abs_coord);  // 直接调用你的底层驱动
}

/**
 * @brief 向指定电机发送急停（立即减速停止）
 *        acc=0 时立即停止，否则减速停止
 */
static void send_axis_stop(int32_t addr, uint8_t acc)
{
    uint8_t tx[8] = {0};
    tx[0] = 0xF5;
    tx[1] = 0x00;
    tx[2] = 0x00;
    tx[3] = acc;
    
    // 坐标任意（0）
    tx[4] = 0x00;
    tx[5] = 0x00;
    tx[6] = 0x00;
    // 你的库函数会自动加 CRC
    CAN_Transmit_Data(&hfdcan1, addr, tx, 7);
}

/**
 * @brief 阻塞等待指定电机进入“运行完成”状态 (0x02)      已被事件组代替，暂无用
 * @param addr 电机 CAN ID
 * @param timeout_ms 超时 (ms)
 * @retval 0 成功, -1 超时
 */
//static int wait_axis_ready(int32_t addr, uint32_t timeout_ms)
//{
//    uint32_t start = osKernelGetTickCount();
//    CAN_Rx_Packet_t pkt;

//    while ((osKernelGetTickCount() - start) < timeout_ms) {
//        if (osMessageQueueGet(motor_event_queue, &pkt, NULL, 10) == osOK) {
//            // 只关心当前电机的坐标绝对运动响应
//            if (pkt.ID == addr && pkt.FuncCode == FUNC_ABS_POS) {
//                if (pkt.Status == STATUS_RUN_COMPLETE) {
//                    return 0;   // 到位
//                }
//                // 其他状态 (0x05 同步接收, 0x01 开始) 忽略，继续等待
//            }
//        }
//    }
//    return -1;  // 超时
//}

/**
 * @brief 执行一次完整的 X/Y 同步移动
 * @param x_abs  X 轴绝对坐标 (两个 X 电机目标相同)
 * @param y_abs  Y 轴绝对坐标
 * @param speed  速度 (RPM)
 * @param acc    加速度
 * @retval 0 成功, -1 失败
 */
static int move_to(int32_t x_abs, int32_t y_abs, uint16_t speed, uint8_t acc)
{
    osEventFlagsClear(evtAxesDone, EVENT_ALL_AXES | EVENT_ANY_ERROR);
    send_axis_abs(X1_ADDR, x_abs, speed, acc);
    send_axis_abs(X2_ADDR, x_abs, speed, acc);
    send_axis_abs(Y_ADDR,  y_abs, speed, acc);
    motorSyncTrigger(0);

    uint32_t flags = osEventFlagsWait(evtAxesDone,
                                       EVENT_ALL_AXES | EVENT_ANY_ERROR,
                                       osFlagsWaitAny, ACK_TIMEOUT_MS);
    if (flags & EVENT_ANY_ERROR) {
        return -2;   // 表示发生错误
    }
    if ((flags & EVENT_ALL_AXES) == EVENT_ALL_AXES) {
        return 0;
    }
    return -1;   // 超时
}

static void nozzle_on(void) {
    HAL_GPIO_WritePin(NOZZLE_GPIO_PORT, NOZZLE_GPIO_PIN, GPIO_PIN_SET);
}
static void nozzle_off(void) {
    HAL_GPIO_WritePin(NOZZLE_GPIO_PORT, NOZZLE_GPIO_PIN, GPIO_PIN_RESET);
}

/* ---------- 封装 Z 轴基本动作 ---------- */

// 阻塞式等待舵机稳定 (简单延时，MG995 大约 0.2s/60°)
static void servo_delay_ms(uint32_t ms) {
    osDelay(ms);   // FreeRTOS 下的毫秒延时
}

static void z_down(void) {
    Servo_SetAngle(SERVO_CH_Z, ANGLE_DOWN);
    servo_delay_ms(300);   // 等待到位
}

static void z_up(void) {
    Servo_SetAngle(SERVO_CH_Z, ANGLE_UP);
    servo_delay_ms(300);
}

/* ---------- 组合的吸取 / 放置流程 ---------- */

static void pick_component(void) {
    z_down();
    nozzle_on();
    servo_delay_ms(100);   // 吸稳
    z_up();
}

static void place_component(void) {
    z_down();
    nozzle_off();
    servo_delay_ms(100);   // 释放
    z_up();
}

/**
 * @brief 将角度转换为微步数
 * @param angle 角度 (0.0 ~ 360.0)
 * @return 微步数
 */
static int32_t angle_to_usteps(float angle) {
    return (int32_t)(angle / 360.0f * R_STEPS_PER_REV);
}

/**
 * @brief 将微步数转换为 VACTUAL 速度值
 * @param speed_rpm 转速 (RPM)
 * @return VACTUAL 值 (带符号)
 */
static int32_t speed_to_vactual(float speed_rpm, uint8_t dir) {
    // 1 RPM = 1/60 转/秒 = R_STEPS_PER_REV / 60 微步/秒
    float usteps_per_sec = speed_rpm * R_STEPS_PER_REV / 60.0f;
    // vactual = usteps_per_sec / 0.715 (12MHz 内部时钟)
    int32_t vactual = (int32_t)(usteps_per_sec / 0.715f);//此处若 TMC_SetSpeed 直接接受微步/秒，则不应除以 0.715，直接 return (int32_t)(usteps_per_sec)。
    // 若其需要 TMC2209 的 VACTUAL 原始值，正确公式为 VACTUAL = (usteps_per_sec * 16777216) / 12000000，约为 1.3981 * usteps_per_sec。
    return (dir == 0) ? vactual : -vactual;
}

/**
 * @brief R 轴旋转到指定角度 (开环，基于时间)
 * @param angle  目标角度 (绝对角度，0~360)
 * @param speed_rpm 转速
 * @note  当前角度未记录，需先在系统任务中维护
 */
static void r_axis_rotate(float angle, float speed_rpm) {
    // 假设 R 轴当前位置为 cur_angle (可在应用层维护)
    float cur_angle = 0.0f;  // TODO: 从系统状态获取
    float delta = angle - cur_angle;
    if (delta < -180.0f) delta += 360.0f;
    else if (delta > 180.0f) delta -= 360.0f;   // 选择最短路径

    if (fabsf(delta) < 0.5f) return;            // 角度太近，忽略

    uint8_t dir = (delta >= 0) ? 0 : 1;         // 0=正向, 1=反向
    int32_t usteps = angle_to_usteps(fabsf(delta));
    float usteps_per_sec = speed_rpm * R_STEPS_PER_REV / 60.0f;
    uint32_t run_time_ms = (uint32_t)(usteps * 1000.0f / usteps_per_sec);

    // 设置速度并运行指定时间
    TMC_SetSpeed(speed_to_vactual(speed_rpm, dir));
    osDelay(run_time_ms);

    // 停止（速度设为 0）
    TMC_SetSpeed(0);
    osDelay(R_ACCEL_DELAY);   // 等待电机停稳

    // 更新当前位置 (这里应更新全局变量，可由调用者负责)
    // current_r_angle = angle;
}

/* ---------- 任务入口 ---------- */
void MotionTask_Func(void *argument)
{
    /* ----- 1. 初始化资源和电机 ----- */
    //if (motor_event_queue == NULL) {
    //    motor_event_queue = osMessageQueueNew(32, sizeof(CAN_Rx_Packet_t), NULL);
    //}

    // 电机硬件初始化 (包括设置模式、使能、同步标志、零点等)
    Motor_Init();   // 你的 Motor_Init 已经包含了同步使能(0x4A)
    Servo_Init(&htim5);       // 舵机初始化，参数按你的实际定时器填写
    TMC_Init();               // TMC2209 初始化
    // 可选：将开机当前位置设为工作零点
    // motorSetZero(0x01); ...

    // 跟踪当前绝对坐标 (编码器值)
    int32_t cur_x = 0;
    int32_t cur_y = 0;

    MotionCmd_t cmd;

    /* ----- 2. 主循环 ----- */
    while (1) {
        if (osMessageQueueGet(motion_cmd_queue, &cmd, NULL, osWaitForever) == osOK) {
            switch (cmd.cmd_type) {

            case MOTION_CMD_MOVE_TO:

                osEventFlagsClear(evtAxesDone, EVENT_ALL_AXES);  // 清空事件组
                positionMode3Run(X1_ADDR, cmd.speed, cmd.acc, cmd.target_x);
                positionMode3Run(X2_ADDR, cmd.speed, cmd.acc, cmd.target_x);
                positionMode3Run(Y_ADDR, cmd.speed, cmd.acc, cmd.target_y);
                motorSyncTrigger(0);

                uint32_t flags = osEventFlagsWait(evtAxesDone, EVENT_ALL_AXES | EVENT_ANY_ERROR,
                                   osFlagsWaitAny, 10000);

                if (flags & EVENT_ANY_ERROR) {
                    // 急停处理
                    send_axis_stop(X1_ADDR, 0);
                    send_axis_stop(X2_ADDR, 0);
                    send_axis_stop(Y_ADDR, 0);
                    PrintDebug("Emergency stop! \r\n");
                } else if ((flags & EVENT_ALL_AXES) == EVENT_ALL_AXES) {
                    PrintDebug("Move to (%ld, %ld) done.\r\n", cmd.target_x, cmd.target_y);
                    cur_x = cmd.target_x;
                    cur_y = cmd.target_y;                    
                    // 正常到位
                }else {
                    // 超时处理
                    send_axis_stop(X1_ADDR, 0);
                    send_axis_stop(X2_ADDR, 0);
                    send_axis_stop(Y_ADDR, 0);
                    PrintDebug("Move timeout! \r\n");
                }
                break;

            case MOTION_CMD_HOME:
                // 回零：向坐标 0 移动，速度稍慢
                move_to(0, 0, 100, 50);
                cur_x = 0;
                cur_y = 0;
                break;

            case MOTION_CMD_STOP:
                // 所有轴急停 (立即停止)
                send_axis_stop(X1_ADDR, 0);
                send_axis_stop(X2_ADDR, 0);
                send_axis_stop(Y_ADDR, 0);
                break;

            case MOTION_CMD_DISABLE:
                motorEnable(X1_ADDR, 0);
                motorEnable(X2_ADDR, 0);
                motorEnable(Y_ADDR, 0);
                break;
            case MOTION_CMD_Z_DOWN:   z_down(); break;

            case MOTION_CMD_Z_UP:     z_up();   break;

            case MOTION_CMD_PICK:     pick_component(); break;

            case MOTION_CMD_PLACE:    place_component(); break;

            case MOTION_CMD_WAIT:
                osDelay(cmd.param2);
                break;

            case MOTION_CMD_R_ROTATE:
            r_axis_rotate((float)cmd.target_r / 100.0f, (float)cmd.speed);
            break;   // cmd.target_r 可约定为 0.01° 单位
						
            default:
                break;
            }
        }
    }
}
//信号量初始化
//事件组初始化
void Event_Init(void) {
    evtAxesDone = osEventFlagsNew(NULL);
}

/**
 * @brief can处理任务函数
 * @note 从motor_event_queue 取数据并释放信号量。
 */

void CAN_Process_Task(void *argument) {
    CAN_Rx_Packet_t pkt;
    while (1) {
        // 阻塞等待队列数据
        if (osMessageQueueGet(motor_event_queue, &pkt, NULL, osWaitForever) == osOK) {
            // 检查是否是位置运动完成
            if (pkt.FuncCode == 0xF5 && pkt.Status == 0x02) {
                if (pkt.ID == 1) osEventFlagsSet(evtAxesDone, EVENT_X1_DONE);
                else if (pkt.ID == 2) osEventFlagsSet(evtAxesDone, EVENT_X2_DONE);
                else if (pkt.ID == 3) osEventFlagsSet(evtAxesDone, EVENT_Y_DONE);
            }
            // 如果状态为 0x03（限位停止），设置错误位
            else if (pkt.FuncCode == 0xF5 && pkt.Status == 0x03) {
                osEventFlagsSet(evtAxesDone, EVENT_ANY_ERROR);  // 立即通知
            }
        }
    }
}

