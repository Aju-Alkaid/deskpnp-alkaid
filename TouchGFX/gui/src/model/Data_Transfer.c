#include "gui/model/Data_Transfer.h"
#include <string.h>

// ======================== 鍏ㄥ眬鐘舵€佸彉閲忥紙鍚戝悗鍏煎锛?===================
uint8_t  if_now_SMT       = 1;
uint8_t  total_SMT        = 30;
uint8_t  now_SMT          = 15;
uint16_t Temp             = 388;
uint8_t  if_DOWNLOAD_READY = 0;

// ======================== 闃熷垪鍙ユ焺 ========================
osMessageQueueId_t dataTransferQueue = NULL;  // System 鈫?GUI
osMessageQueueId_t guiCmdQueue       = NULL;  // GUI 鈫?System

// ======================== 璺敱琛?========================
// 瑙勫垯锛氭坊鍔?GUI鈫扴ystem 鍛戒护鏃讹紝鍦ㄦ琛ㄥ鍔犱竴琛屽嵆鍙€?
// handler 鍜?queue 鑷冲皯濉竴涓€?

static void _h_motor_move(const DT_Msg_t *msg);
static void _h_motor_stop(const DT_Msg_t *msg);
static void _h_motor_home(const DT_Msg_t *msg);
static void _h_smt_start(const DT_Msg_t *msg);
static void _h_smt_pause(const DT_Msg_t *msg);
static void _h_heater_set(const DT_Msg_t *msg);
static void _h_system_reset(const DT_Msg_t *msg);

static DT_Route_t s_routeTable[] = {
    //  type                    handler              queue锛坔andler 浼樺厛锛?
    { DT_CMD_MOTOR_MOVE,   _h_motor_move,       NULL },
    { DT_CMD_MOTOR_STOP,   _h_motor_stop,       NULL },
    { DT_CMD_MOTOR_HOME,   _h_motor_home,       NULL },
    { DT_CMD_SMT_START,    _h_smt_start,        NULL },
    { DT_CMD_SMT_PAUSE,    _h_smt_pause,        NULL },
    { DT_CMD_HEATER_SET,   _h_heater_set,       NULL },
    { DT_CMD_SYSTEM_RESET, _h_system_reset,     NULL },
    { DT_CMD_CUSTOM,       NULL,                NULL },
    { DT_CMD_WIFI_CTRL,    NULL,                NULL },  // 棰勭暀锛岀敱 Model 鑷澶勭悊
};

static const int s_routeCount = sizeof(s_routeTable) / sizeof(DT_Route_t);

void DT_Init(void)
{
    // Simulator-only runtime queue stubs (added by compatibility fix)
    #ifdef SIMULATOR
    if (dataTransferQueue == NULL) {
        dataTransferQueue = osMessageQueueNew(DT_LOG_QUEUE_DEPTH, sizeof(DT_Msg_t), NULL);
    }
    if (guiCmdQueue == NULL) {
        guiCmdQueue = osMessageQueueNew(16, sizeof(DT_Msg_t), NULL);
    }
    #endif
    // 鐩墠璺敱琛ㄤ负闈欐€佹暟缁勶紝鏃犻渶鍔ㄦ€佸垵濮嬪寲
    // 鏈潵鍙湪姝ゆ敞鍐屽姩鎬佽矾鐢?
}

// ======================== 璺敱鍒嗗彂 ========================
void DT_Dispatch(const DT_Msg_t *cmd)
{
    if (cmd == NULL) return;

    for (int i = 0; i < s_routeCount; i++) {
        if (s_routeTable[i].type == cmd->type) {
            // 1. handler 浼樺厛锛堢洿鎺ヨ皟鐢級
            if (s_routeTable[i].handler != NULL) {
                s_routeTable[i].handler(cmd);
                return;
            }
            // 2. 闃熷垪杞彂锛堣法浠诲姟閫氫俊锛?
            if (s_routeTable[i].queue != NULL) {
                osMessageQueuePut(s_routeTable[i].queue, cmd, 0, 0);
                return;
            }
            // 3. 鏃?handler 鏃?queue 鈥?闈欓粯蹇界暐
            return;
        }
    }
    // 鏈尮閰?鈥?鍙€氳繃鏃ュ織杈撳嚭璋冭瘯
}

// ======================== GUI 鈫?System 鍛戒护鍙戦€?========================
void DT_SendCommand(const DT_Msg_t *cmd)
{
    if (guiCmdQueue != NULL) {
        osMessageQueuePut(guiCmdQueue, cmd, 0, 0);
    }
}

// ======================== System 鈫?GUI 閫氱煡鍙戦€?========================
static void _DT_PutToGuiQueue(const DT_Msg_t *msg)
{
    if (dataTransferQueue != NULL) {
        osMessageQueuePut(dataTransferQueue, msg, 0, 0);
    }
}

void DT_NotifySMTStatus(uint8_t is_smt)
{
    DT_Msg_t msg; memset(&msg, 0, sizeof(msg));
    msg.type = DT_SMT_STATUS;
    msg.data.status = is_smt;
    _DT_PutToGuiQueue(&msg);
}

void DT_NotifyTemp(uint16_t temp)
{
    DT_Msg_t msg; memset(&msg, 0, sizeof(msg));
    msg.type = DT_TEMP_CHANGE;
    msg.data.temp = temp;
    _DT_PutToGuiQueue(&msg);
}

void DT_NotifyDownloadStatus(uint8_t status)
{
    DT_Msg_t msg; memset(&msg, 0, sizeof(msg));
    msg.type = DT_DOWNLOAD_STATUS;
    msg.data.status = status;
    _DT_PutToGuiQueue(&msg);
}

void DT_NotifySMTProgress(uint8_t current, uint8_t total)
{
    DT_Msg_t msg; memset(&msg, 0, sizeof(msg));
    msg.type = DT_SMT_PROGRESS;
    msg.data.progress.current = current;
    msg.data.progress.total   = total;
    _DT_PutToGuiQueue(&msg);
}

void DT_NotifyMotorResetDone(void)
{
    DT_Msg_t msg; memset(&msg, 0, sizeof(msg));
    msg.type = DT_MOTOR_RESET_DONE;
    _DT_PutToGuiQueue(&msg);
}

void DT_NotifyCustom(uint8_t code, uint8_t param)
{
    DT_Msg_t msg; memset(&msg, 0, sizeof(msg));
    msg.type = DT_CUSTOM_MSG;
    msg.data.raw[0] = code;
    msg.data.raw[1] = param;
    _DT_PutToGuiQueue(&msg);
}

void DT_NotifyMotorSpeed(uint16_t speed)
{
    DT_Msg_t msg; memset(&msg, 0, sizeof(msg));
    msg.type = DT_MOTOR_SPEED;
    msg.data.motor_speed = speed;
    _DT_PutToGuiQueue(&msg);
}

void DT_NotifyLogText(uint8_t code, uint8_t param)
{
    DT_Msg_t msg; memset(&msg, 0, sizeof(msg));
    msg.type = DT_LOG_TEXT;
    msg.data.tag.code = code;
    msg.data.tag.param = param;
    _DT_PutToGuiQueue(&msg);
}


void DT_NotifyWifiStatus(uint8_t connected)
{
    DT_Msg_t msg; memset(&msg, 0, sizeof(msg));
    msg.type = DT_WIFI_STATUS;
    msg.data.status = connected;
    _DT_PutToGuiQueue(&msg);
}
// ---- GUI→System 命令处理器：全部委托给主控端 bridge 层 ----
// 这些函数由 Task/app_touchgfx_bridge.c 提供实现
extern void Bridge_MotorMove(int32_t x, int32_t y, int32_t r);
extern void Bridge_MotorStop(void);
extern void Bridge_MotorHome(void);
extern void Bridge_SMTStart(void);
extern void Bridge_SMTPause(void);
extern void Bridge_HeaterSet(uint16_t temp);
extern void Bridge_SystemReset(void);

static void _h_motor_move(const DT_Msg_t *msg)
{
    Bridge_MotorMove(msg->data.move.x, msg->data.move.y, msg->data.move.r);
}

static void _h_motor_stop(const DT_Msg_t *msg)
{
    (void)msg;
    Bridge_MotorStop();
}

static void _h_motor_home(const DT_Msg_t *msg)
{
    (void)msg;
    Bridge_MotorHome();
}

static void _h_smt_start(const DT_Msg_t *msg)
{
    (void)msg;
    Bridge_SMTStart();
}

static void _h_smt_pause(const DT_Msg_t *msg)
{
    (void)msg;
    Bridge_SMTPause();
}

static void _h_heater_set(const DT_Msg_t *msg)
{
    Bridge_HeaterSet(msg->data.temp);
}

static void _h_system_reset(const DT_Msg_t *msg)
{
    (void)msg;
    Bridge_SystemReset();
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
    // TODO: 鍚姩璐寸墖娴佺▼锛堢敱璐寸墖浠诲姟瀹炵幇锛?
}


