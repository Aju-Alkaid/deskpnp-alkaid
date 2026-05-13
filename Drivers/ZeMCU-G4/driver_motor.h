#ifndef __DRIVER_MOTOR_H
#define __DRIVER_MOTOR_H 
#include "stm32g4xx_hal.h"

// 工作模式定义 (指令 0x82 参数)
#define CR_OPEN   0x00   // 脉冲开环
#define CR_CLOSE  0x01   // 脉冲闭环
#define CR_vFOC   0x02   // 脉冲FOC
#define SR_OPEN   0x03   // 总线开环
#define SR_CLOSE  0x04   // 总线闭环
#define SR_vFOC   0x05   // 总线FOC

// 请根据实际接线调整这些ID
#define MOTOR_X1_ID  1  // 对应 CAN1_0x200_Tx_Data
#define MOTOR_X2_ID  2  // 对应 CAN1_0x1ff_Tx_Data
#define MOTOR_Y_ID  3  // 对应 CAN1_0x1fe_Tx_Data

// 内部状态机枚举
typedef enum {
    MOTOR_STATE_IDLE = 0,      // 状态0：空闲
    MOTOR_STATE_SENDING = 1,   // 状态1：正在发送指令
    MOTOR_STATE_WAITING = 2,   // 状态2：指令已发，正在等待电机到位（这就是 STATE_WAITING 的来源）
    MOTOR_STATE_COMPLETE = 3,  // 状态3：运动完成
    MOTOR_STATE_ERROR = 4      // 状态4：出错
	} MotorState_t;

void readRealTimeSpeed(uint16_t slaveAddr,uint16_t ID);

uint8_t waitingForACK(uint32_t delayTime, uint8_t expectFunc, uint8_t expectStatus);

void runFail(void);

void runOK(void);

void NVIC_INIT(void);

void speedModeRun(uint8_t slaveAddr,uint8_t dir,uint16_t speed,uint8_t acc);

void readRealTimeLocation(uint8_t slaveAddr);

void setWorkMStep(uint8_t slaveAddr,uint8_t MStep);

void setIWorkMode(uint8_t slaveAddr,uint16_t Ma);

void positionMode3Run(uint8_t slaveAddr,uint16_t speed,uint16_t acc,int32_t absAxis);

void motorEnable(uint8_t slaveAddr, uint8_t enable);

void motorSetArrivalThreshold(uint8_t slaveAddr);

void motorSyncEnable(uint8_t enable);

void motorSetZero(uint8_t slaveAddr);

void setWorkMode(uint8_t slaveAddr,uint8_t Mode);

void Motor_Init(void);

void motorSyncTrigger(uint8_t slaveAddr);

#endif

