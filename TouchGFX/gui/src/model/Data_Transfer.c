#include "gui/model/Data_Transfer.h"
#include <string.h>

// ======================== 全局状态变量（向后兼容）====================
uint8_t  if_now_SMT       = 1;
uint8_t  total_SMT        = 30;
uint8_t  now_SMT          = 15;
uint16_t Temp             = 388;
uint8_t  if_DOWNLOAD_READY = 0;

// ======================== 队列句柄 ========================
osMessageQueueId_t dataTransferQueue = NULL;  // System → GUI
osMessageQueueId_t guiCmdQueue       = NULL;  // GUI → System

// ======================== 路由表 ========================
// 规则：添加 GUI→System 命令时，在此表增加一行即可。
// handler 和 queue 至少填一个。

static void _h_motor_move(const DT_Msg_t *msg);
static void _h_motor_stop(const DT_Msg_t *msg);
static void _h_motor_home(const DT_Msg_t *msg);
static void _h_smt_start(const DT_Msg_t *msg);
static void _h_smt_pause(const DT_Msg_t *msg);
static void _h_heater_set(const DT_Msg_t *msg);
static void _h_system_reset(const DT_Msg_t *msg);

static DT_Route_t s_routeTable[] = {
    //  type                    handler              queue（handler 优先）
    { DT_CMD_MOTOR_MOVE,   _h_motor_move,       NULL },
    { DT_CMD_MOTOR_STOP,   _h_motor_stop,       NULL },
    { DT_CMD_MOTOR_HOME,   _h_motor_home,       NULL },
    { DT_CMD_SMT_START,    _h_smt_start,        NULL },
    { DT_CMD_SMT_PAUSE,    _h_smt_pause,        NULL },
    { DT_CMD_HEATER_SET,   _h_heater_set,       NULL },
    { DT_CMD_SYSTEM_RESET, _h_system_reset,     NULL },
    { DT_CMD_CUSTOM,       NULL,                NULL },  // 预留，由 Model 自行处理
};

static const int s_routeCount = sizeof(s_routeTable) / sizeof(DT_Route_t);

void DT_Init(void)
{
    // 目前路由表为静态数组，无需动态初始化
    // 未来可在此注册动态路由
}

// ======================== 路由分发 ========================
void DT_Dispatch(const DT_Msg_t *cmd)
{
    if (cmd == NULL) return;

    for (int i = 0; i < s_routeCount; i++) {
        if (s_routeTable[i].type == cmd->type) {
            // 1. handler 优先（直接调用）
            if (s_routeTable[i].handler != NULL) {
                s_routeTable[i].handler(cmd);
                return;
            }
            // 2. 队列转发（跨任务通信）
            if (s_routeTable[i].queue != NULL) {
                osMessageQueuePut(s_routeTable[i].queue, cmd, 0, 0);
                return;
            }
            // 3. 无 handler 无 queue — 静默忽略
            return;
        }
    }
    // 未匹配 — 可通过日志输出调试
}

// ======================== GUI → System 命令发送 ========================
void DT_SendCommand(const DT_Msg_t *cmd)
{
    if (guiCmdQueue != NULL) {
        osMessageQueuePut(guiCmdQueue, cmd, 0, 0);
    }
}

// ======================== System → GUI 通知发送 ========================
static void _DT_PutToGuiQueue(const DT_Msg_t *msg)
{
    if (dataTransferQueue != NULL) {
        osMessageQueuePut(dataTransferQueue, msg, 0, 0);
    }
}

void DT_NotifySMTStatus(uint8_t is_smt)
{
    DT_Msg_t msg = { .type = DT_SMT_STATUS, .data.status = is_smt };
    _DT_PutToGuiQueue(&msg);
}

void DT_NotifyTemp(uint16_t temp)
{
    DT_Msg_t msg = { .type = DT_TEMP_CHANGE, .data.temp = temp };
    _DT_PutToGuiQueue(&msg);
}

void DT_NotifyDownloadStatus(uint8_t status)
{
    DT_Msg_t msg = { .type = DT_DOWNLOAD_STATUS, .data.status = status };
    _DT_PutToGuiQueue(&msg);
}

void DT_NotifySMTProgress(uint8_t current, uint8_t total)
{
    DT_Msg_t msg = { .type = DT_SMT_PROGRESS };
    msg.data.progress.current = current;
    msg.data.progress.total   = total;
    _DT_PutToGuiQueue(&msg);
}

void DT_NotifyMotorResetDone(void)
{
    DT_Msg_t msg = { .type = DT_MOTOR_RESET_DONE };
    _DT_PutToGuiQueue(&msg);
}

void DT_NotifyCustom(uint8_t code, uint8_t param)
{
    DT_Msg_t msg = { .type = DT_CUSTOM_MSG };
    msg.data.raw[0] = code;
    msg.data.raw[1] = param;
    _DT_PutToGuiQueue(&msg);
}

// ======================== 命令处理器（Handler）====================
// 每个 handler 仅做最少的参数提取 + 调用已有的系统函数。
// 复杂业务逻辑应在各自的任务中实现。

#include "driver_motor.h"   // Motor_Init, positionMode3Run 等
#include "app_motion.h"      // 运动控制函数

static void _h_motor_move(const DT_Msg_t *msg)
{
    int32_t x = msg->data.move.x;
    int32_t y = msg->data.move.y;
    int32_t r = msg->data.move.r;
    // TODO: 将 x,y,r 转换为步数，通过 motion_cmd_queue 发送给 MotionTask_Func
    // 示例：MotionCmd_t cmd = { .cmd_type = MOTION_CMD_MOVE_TO, .target_x = x, ... };
    // osMessageQueuePut(motion_cmd_queue, &cmd, 0, 0);
}

static void _h_motor_stop(const DT_Msg_t *msg)
{
    // TODO: 通过 motion_cmd_queue 发送急停命令
    // 参考 Task/app_test.c 中的 axis_stop() + motorSyncTrigger()
}

static void _h_motor_home(const DT_Msg_t *msg)
{
    // TODO: 触发三轴归零序列
    // 参考 Motor_Init() 中的归零逻辑
}

static void _h_smt_start(const DT_Msg_t *msg)
{
    smt_Start();  // 调用 Data_Transfer.c 中已有的占位函数
}

static void _h_smt_pause(const DT_Msg_t *msg)
{
    // TODO: 设置暂停标志，由贴片流程任务检测
}

static void _h_heater_set(const DT_Msg_t *msg)
{
    uint16_t temp = msg->data.temp;
    // TODO: 通过 CAN 发送温度设置命令到加热台（CAN ID 0x10）
    // 参考 Drivers/ZeMCU-G4/driver_heater.c
}

static void _h_system_reset(const DT_Msg_t *msg)
{
    // TODO: 软件复位，需确保各外设安全关闭
    // NVIC_SystemReset();
}

// ---- 占位函数（向后兼容，待主程序替换）----
static uint8_t motor_reset_done = 0;

void motorReset_Start(void)
{
    motor_reset_done = 0;
}

int motorReset_IsDone(void)
{
    if (motor_reset_done == 0) {
        motor_reset_done = 1;
        return 0;
    }
    return 1;
}

void smt_Start(void)
{
    // TODO: 启动贴片流程（由贴片任务实现）
}