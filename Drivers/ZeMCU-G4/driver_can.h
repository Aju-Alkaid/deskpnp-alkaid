#ifndef __DRIVER_CAN_H
#define __DRIVER_CAN_H

#include <stdint.h>
#include "fdcan.h"
#include <string.h>
#include <stdbool.h>
#include "cmsis_os.h"
#include "FreeRTOS.h"
#include "queue.h" 

typedef uint8_t      boolean_t;
extern volatile uint32_t g_rx_irq_count;
#ifndef TRUE
  /** Value is true (boolean_t type) */
  #define TRUE        ((uint8_t) 1u)
#endif

#ifndef FALSE
  /** Value is false (boolean_t type) */
  #define FALSE       ((uint8_t) 0u)
#endif  

/**
 * @brief CAN通信接收回调函数数据类型
 *
 */
typedef void (*CAN_Callback)(FDCAN_RxHeaderTypeDef *Header, uint8_t *Buffer);

// 用于任务间通信的消息类型
typedef enum {
    MOTOR_EVENT_NONE,
    MOTOR_EVENT_MOVE_COMPLETE, // 运动完成事件
    MOTOR_EVENT_ERROR          // 错误事件
} MotorEventType_t;

// 队列传递的数据结构
typedef struct {
    MotorEventType_t type;
    uint8_t motor_id;
    uint32_t timestamp;
} MotorEventMsg_t;

typedef struct {
    FDCAN_RxHeaderTypeDef Header;       // 包含 ID, DLC, 标志位等
    uint8_t Data[8];                   // 数据域 (CAN 最大 8字节)
//    uint64_t Timestamp;                 // 时间戳 (用于计算延迟或同步)
    // uint32_t SwOstamp;               // 可选：记录进入队列的系统时间
} Struct_CAN_Message_t;


/**
 * @brief CAN通信处理结构体
 *
 */
struct Struct_CAN_Manage_Object{

    FDCAN_HandleTypeDef *CAN_Handler;
    //CAN_Callback Callback_Function;
	
	 // 这里定义为 void * 类型，可以传递数据指针，或者也可以定义专门的结构体
    QueueHandle_t rx_queue; 

    // 与接收相关的数据
    FDCAN_RxHeaderTypeDef Rx_Header;
    uint8_t Rx_Buffer[64];

    
    uint64_t Rx_Timestamp;// 接收时间戳
	
};

// 定义 CAN 接收数据结构体
typedef struct {
	uint16_t ID;        // 新增：CAN 标准 ID (0x01, 0x03 等)	
  uint8_t FuncCode;   // 功能码 (如 0xF5, 0xF1)
  uint8_t Status;     // 状态码 (如 0x01运行中, 0x02到位)
  uint8_t Data[8];    // 其他数据 (如位置、电流等，根据需要解析)
  uint32_t Timestamp; // 接收时间戳 (可选，用于调试)
	uint8_t  DataLength; 
} CAN_Rx_Packet_t;


extern osMessageQueueId_t can_rx_queue;

extern bool init_finished;

extern struct Struct_CAN_Manage_Object CAN1_Manage_Object;

extern uint8_t CAN1_0x1fe_Tx_Data[];
extern uint8_t CAN1_0x1ff_Tx_Data[];
extern uint8_t CAN1_0x200_Tx_Data[];
extern uint8_t CAN1_0x2fe_Tx_Data[];
extern uint8_t CAN1_0x2ff_Tx_Data[];
extern uint8_t CAN1_0x3fe_Tx_Data[];
extern uint8_t CAN1_0x4fe_Tx_Data[];

extern uint8_t CAN_Supercap_Tx_Data[];

extern uint16_t  CAN_ID;
extern uint8_t  CAN_RxDone;  //接收标致位


void CAN_Init(FDCAN_HandleTypeDef *hfdcan, CAN_Callback Callback_Function);

uint8_t CAN_Transmit_Data(FDCAN_HandleTypeDef *hfdcan, uint16_t ID, uint8_t *Data, uint16_t Length);

//void TIM_100us_CAN_PeriodElapsedCallback();

//void TIM_1ms_CAN_PeriodElapsedCallback();

uint8_t canCRC_ATM(uint8_t *buf,uint8_t len) ;//CRC_SUM8

void error_handling(void);
#endif
