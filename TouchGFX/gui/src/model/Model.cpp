#include <gui/model/Model.hpp>
#include <gui/model/ModelListener.hpp>
#include <string.h>

Model::Model() : modelListener(0)
{
#ifdef SIMULATOR
    DT_Init();
#endif
}

void Model::tick()
{
    processQueue();  // 姣忓抚澶勭悊 System鈫扜UI 閫氱煡
}

// ======================== System 鈫?GUI 閫氱煡澶勭悊 ========================
void Model::processQueue(void)
{
    // ---- 鏂瑰悜1: GUI鈫扴ystem 鍛戒护鍒嗗彂 ----
    if (guiCmdQueue != NULL) {
        DT_Msg_t cmd;
        while (osMessageQueueGet(guiCmdQueue, &cmd, NULL, 0) == osOK) {
            DT_Dispatch(&cmd);  // 璺敱琛ㄥ垎鍙戝埌瀵瑰簲 handler/queue
        }
    }

    // ---- 鏂瑰悜2: System鈫扜UI 閫氱煡澶勭悊 ----
    if (dataTransferQueue == NULL || modelListener == NULL) return;

    DT_Msg_t msg;
    // 寰幆鍙栧嚭鎵€鏈夊緟澶勭悊娑堟伅锛堥潪闃诲锛?
    while (osMessageQueueGet(dataTransferQueue, &msg, NULL, 0) == osOK) {
        switch (msg.type) {
            case DT_SMT_STATUS:
                modelListener->onNotifySMTStatus(msg.data.status);
                break;
            case DT_TEMP_CHANGE:
                modelListener->onNotifyTemp(msg.data.temp);
                break;
            case DT_DOWNLOAD_STATUS:
                modelListener->onNotifyDownloadStatus(msg.data.status);
                break;
            case DT_SMT_PROGRESS:
                modelListener->onNotifySMTProgress(
                    msg.data.progress.current, msg.data.progress.total);
                break;
            case DT_MOTOR_RESET_DONE:
                modelListener->onNotifyMotorResetDone();
                break;
            case DT_CUSTOM_MSG:
                modelListener->onNotifyCustom(msg.data.raw[0], msg.data.raw[1]);
                break;
            case DT_MOTOR_SPEED:
                if (modelListener) modelListener->onNotifyMotorSpeed(msg.data.motor_speed);
                break;
            case DT_LOG_TEXT:
                if (modelListener) modelListener->onNotifyLogText(msg.data.tag.code, msg.data.tag.param);
                break;
            case DT_WIFI_STATUS:
                if (modelListener) modelListener->onNotifyWifiStatus(msg.data.status);
                break;
            default:
                // 鏈瘑鍒殑娑堟伅绫诲瀷 鈥?鍙湪姝ゆ坊鍔犳棩蹇?
                break;
        }
    }
}

// ======================== GUI 鈫?System 鍛戒护鍙戦€?========================
// Presenter 璋冪敤姝ゆ柟娉曪紝灏嗙敤鎴锋搷浣滆浆鎹负绯荤粺鍛戒护
//
// 浣跨敤绀轰緥锛堝湪 Presenter 涓級锛?
//   model->sendCommand(DT_CMD_MOTOR_MOVE, 1000, 2000);  // 绉诲姩鍒?10.00mm, 20.00mm)
//   model->sendCommand(DT_CMD_HEATER_SET, 1800);          // 璁剧疆娓╁害 180.0鈩?
//   model->sendCommand(DT_CMD_SMT_START);                  // 鍚姩璐寸墖
void Model::sendCommand(DT_MsgType_t type, int32_t p1, int32_t p2)
{
    DT_Msg_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type = type;

    // 鏍规嵁鍛戒护绫诲瀷濉厖鍙傛暟
    switch (type) {
        case DT_CMD_MOTOR_MOVE:
            cmd.data.move.x = p1;   // x 鍧愭爣 (mm*100)
            cmd.data.move.y = p2;   // y 鍧愭爣 (mm*100)
            cmd.data.move.r = 0;    // r 瑙掑害锛堟殏涓嶆敮鎸侊級
            break;
        case DT_CMD_HEATER_SET:
            cmd.data.temp = (uint16_t)p1;  // 鐩爣娓╁害 (0.1鈩?
            break;
        case DT_CMD_CUSTOM:
            cmd.data.raw[0] = (uint8_t)p1;
            cmd.data.raw[1] = (uint8_t)p2;
            break;
        // 鏃犲弬鏁板懡浠わ細SMT_START, SMT_PAUSE, MOTOR_STOP, MOTOR_HOME, SYSTEM_RESET
        default:
            break;
    }

    DT_SendCommand(&cmd);  // 鏀惧叆 guiCmdQueue
}

