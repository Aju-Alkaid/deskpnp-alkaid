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
    { DT_CMD_CUSTOM,       NULL,                NULL },  // 棰勭暀锛岀敱 Model 鑷澶勭悊
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

// ======================== 鍛戒护澶勭悊鍣?Handler) =======================
// 姣忎釜 handler 浠呭仛鏈€灏戠殑鍙傛暟鎻愬彇 + 璋冪敤宸叉湁鐨勭郴缁熷嚱鏁般€?
// 澶嶆潅涓氬姟閫昏緫搴斿湪鍚勮嚜鐨勪换鍔′腑瀹炵幇銆?

#include "driver_motor.h"
#include "app_motion.h"

static void _h_motor_move(const DT_Msg_t *msg)
{
    (void)msg;
    // TODO: 灏?x,y,r 杞换涓烘ラ帮紝閫氳繃 motion_cmd_queue 鍙戦€佺粰 MotionTask_Func
    // 绀轰緥锛歁otionCmd_t cmd = { .cmd_type = MOTION_CMD_MOVE_TO, .target_x = x, ... };
    // osMessageQueuePut(motion_cmd_queue, &cmd, 0, 0);
}

static void _h_motor_stop(const DT_Msg_t *msg)
{
    // TODO: 閫氳繃 motion_cmd_queue 鍙戦€佹€ュ仠鍛戒护
    // 鍙傝€?Task/app_test.c 涓殑 axis_stop() + motorSyncTrigger()
}

static void _h_motor_home(const DT_Msg_t *msg)
{
    // TODO: 瑙﹀彂涓夎酱褰掗浂搴忓垪
    // 鍙傝€?Motor_Init() 涓殑褰掗浂閫昏緫
}

static void _h_smt_start(const DT_Msg_t *msg)
{
    smt_Start();  // 璋冪敤 Data_Transfer.c 涓凡鏈夌殑鍗犱綅鍑芥暟
}

static void _h_smt_pause(const DT_Msg_t *msg)
{
    // TODO: 璁剧疆鏆傚仠鏍囧織锛岀敱璐寸墖娴佺▼浠诲姟妫€娴?
}

static void _h_heater_set(const DT_Msg_t *msg)
{
    (void)msg;
    // TODO: 閫氳繃 CAN 鍙戦€佹俯搴辰缃懡浠ゅ埌鍔犵儹鍙帮紙CAN ID 0x10锛?
    // 鍙傝€?Drivers/ZeMCU-G4/driver_heater.c
}

static void _h_system_reset(const DT_Msg_t *msg)
{
    // TODO: 杞欢澶嶄綅锛岄渶纭繚鍚勫璁惧畨鍏ㄥ叧闂?
    // NVIC_SystemReset();
}

// ---- 鍗犱綅鍑芥暟锛堝悜鍚庡吋瀹癸紝寰呬富绋嬪簭鏇挎崲锛?---
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


