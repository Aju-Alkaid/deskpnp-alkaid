#include "cmsis_os2.h"

// 任务句柄
osThreadId_t MotionTaskHandle;
osThreadId_t VisionTaskHandle;
// ...

osMessageQueueId_t MotionCmdQueue;   // 运动指令队列
osMutexId_t SystemStateMutex;        // 保护全局状态

extern void MotionTask_Func(void *argument);


void Tasks_Create(void) {
    // 运动任务
    const osThreadAttr_t motion_attr = {
        .name = "MotionTask",
        .stack_size = 512,  // 根据实际调用情况增大
        .priority = osPriorityHigh
    };
    MotionTaskHandle = osThreadNew(MotionTask_Func, NULL, &motion_attr);

    // 视觉任务
    const osThreadAttr_t vision_attr = {
        .name = "VisionTask",
        .stack_size = 512,
        .priority = osPriorityNormal
    };
//    VisionTaskHandle = osThreadNew(VisionTask_Func, NULL, &vision_attr);

    // ... 同理创建 CommuTask, HeatTask, GUITask, SystemTask
}

//void Comm_Init(void) {
//MotionCmdQueue = osMessageQueueNew(16, sizeof(MotionCmd_t), NULL);
//SystemStateMutex = osMutexNew(NULL);
//}

