#include <gui/model/Model.hpp>
#include <gui/model/ModelListener.hpp>
#include <touchgfx/hal/HAL.hpp>
#include <platform/driver/button/ButtonController.hpp>

// 前向声明全局变量（定义在 Data_Transfer.c）
extern osMessageQueueId_t dataTransferQueue;

Model::Model() : modelListener(0)
{

}

void Model::tick()
{
    // 按键扫描已移至独立的 Key_Task，此处不再轮询
    // 改为处理队列中的按键事件
    processQueue();
}

void Model::processQueue(void)
{
    // 处理数据更新队列（来自主系统）
    if (dataTransferQueue != NULL) {
        DataTransferMsg_t msg;
        while (osMessageQueueGet(dataTransferQueue, &msg, NULL, 0) == osOK) {
            switch (msg.type) {
                case DT_SMT_STATUS:
                    // TODO: 通知 UI 更新 SMT 状态
                    // modelListener->onSMTStatusChanged(msg.data.u8_val);
                    break;
                case DT_TEMP_CHANGE:
                    // TODO: 通知 UI 更新温度
                    // modelListener->onTempChanged(msg.data.u16_val);
                    break;
                case DT_DOWNLOAD_STATUS:
                    // TODO: 通知 UI 更新下载状态
                    // modelListener->onDownloadStatus(msg.data.u8_val);
                    break;
                case DT_SMT_PROGRESS:
                    // TODO: 通知 UI 更新进度
                    // modelListener->onSMTProgress(msg.data.raw[0], msg.data.raw[1]);
                    break;
                case DT_MOTOR_RESET_DONE:
                    // TODO: 通知 UI 电机复位完成
                    break;
                case DT_KEY_EVENT:
                    // 按键事件：通过 modelListener 传递
                    if (modelListener) {
                        modelListener->onKeyPressed(msg.data.raw[0]);
                    }
                    break;
                case DT_CUSTOM_MSG:
                    // 自定义消息处理
                    break;
                default:
                    break;
            }
        }
    }
    
    // 按键事件由 KeyController::sample() 统一消费（松开触发），避免双路竞争
}