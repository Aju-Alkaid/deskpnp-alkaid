#include <gui/model/Model.hpp>
#include <gui/model/ModelListener.hpp>
#include <string.h>

Model::Model() : modelListener(0)
{
}

void Model::tick()
{
    processQueue();  // 每帧处理 System→GUI 通知
}

// ======================== System → GUI 通知处理 ========================
void Model::processQueue(void)
{
    // ---- 方向1: GUI→System 命令分发 ----
    if (guiCmdQueue != NULL) {
        DT_Msg_t cmd;
        while (osMessageQueueGet(guiCmdQueue, &cmd, NULL, 0) == osOK) {
            DT_Dispatch(&cmd);  // 路由表分发到对应 handler/queue
        }
    }

    // ---- 方向2: System→GUI 通知处理 ----
    if (dataTransferQueue == NULL || modelListener == NULL) return;

    DT_Msg_t msg;
    // 循环取出所有待处理消息（非阻塞）
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
            default:
                // 未识别的消息类型 — 可在此添加日志
                break;
        }
    }
}

// ======================== GUI → System 命令发送 ========================
// Presenter 调用此方法，将用户操作转换为系统命令
//
// 使用示例（在 Presenter 中）：
//   model->sendCommand(DT_CMD_MOTOR_MOVE, 1000, 2000);  // 移动到(10.00mm, 20.00mm)
//   model->sendCommand(DT_CMD_HEATER_SET, 1800);          // 设置温度 180.0℃
//   model->sendCommand(DT_CMD_SMT_START);                  // 启动贴片
void Model::sendCommand(DT_MsgType_t type, int32_t p1, int32_t p2)
{
    DT_Msg_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type = type;

    // 根据命令类型填充参数
    switch (type) {
        case DT_CMD_MOTOR_MOVE:
            cmd.data.move.x = p1;   // x 坐标 (mm*100)
            cmd.data.move.y = p2;   // y 坐标 (mm*100)
            cmd.data.move.r = 0;    // r 角度（暂不支持）
            break;
        case DT_CMD_HEATER_SET:
            cmd.data.temp = (uint16_t)p1;  // 目标温度 (0.1℃)
            break;
        case DT_CMD_CUSTOM:
            cmd.data.raw[0] = (uint8_t)p1;
            cmd.data.raw[1] = (uint8_t)p2;
            break;
        // 无参数命令：SMT_START, SMT_PAUSE, MOTOR_STOP, MOTOR_HOME, SYSTEM_RESET
        default:
            break;
    }

    DT_SendCommand(&cmd);  // 放入 guiCmdQueue
}