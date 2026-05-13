#include "stm32g4xx_hal.h"
#include "driver_motor.h"
#include "driver_can.h"
#include "driver_timer.h"
#include "cmsis_os.h" 
#include "app_motion.h" // 
// 根据 driver_can.h 中定义的缓冲区,假设以下ID对应XY轴


// 16齿 GT2 皮带参数计算：16齿 * 2mm(齿距) = 32mm/圈
// 16384 / 32 = 512 脉冲每毫米
#define PULSES_PER_MM  512  


 //uint8_t txBuffer[8];      //待发送数据数组（因为要传给xy轴一个数组容易出问题）
int32_t realTimeLocation;		//电机实时位置                            
												//计算出位置，可设置断点观察(示例）	
                         // realTimeLocation = (int32_t)(CanRxBuf.Data[3]<<24 | CanRxBuf.Data[4]<<16 | CanRxBuf.Data[5]<<8 | CanRxBuf.Data[6]<<0)												;
												//接收位置信息失败（1.检查连接线；2.检查电机是否上电；3.检查从机地址，波特率）
												
uint16_t runSpeed=100;    //电机运行速度
uint8_t runDir  = 1;      //电机运行方向

int32_t absoluteAxis = 163840;           //绝对坐标 163840(10圈)

int32_t realAxis = 163840;   //相对坐标 （10圈）
int16_t realTimeSpeed;				//电机实时转速 readRealTimeSpeed

int32_t inputPulses;				//电机输入脉冲数

int32_t PositionError;				//电机位置误差	



typedef struct {
    MotorState_t state;          // 当前状态
    int32_t target_pulse_x;      // 目标位置缓存
    int32_t target_pulse_y;
    uint32_t start_time;         // 用于超时检测
    uint8_t retry_count;         // 重试次数
} MotorController_t;

volatile MotorState_t g_current_xstate = MOTOR_STATE_IDLE; 
volatile MotorState_t g_current_ystate = MOTOR_STATE_IDLE; 

static MotorController_t g_motor_ctrl = {0};

//计算出误差，可设置断点观察
			// 360度 对应 51200
		//	PositionError = (int32_t)(CanRxBuf.Data[1]<<24 | CanRxBuf.Data[2]<<16 | CanRxBuf.Data[3]<<8 | CanRxBuf.Data[4]<<0) ;

/*
功能：初始化流程
输入：无
输出：无
 */
void Motor_Init(void) {
    // 1. 设置总线 FOC 模式 (广播或逐个)
    setWorkMode(0x01, SR_vFOC);  // X1
    setWorkMode(0x02, SR_vFOC);  // X2
    setWorkMode(0x03, SR_vFOC);  // Y
    osDelay(50);                 // 等待生效

    // 2. 使能所有电机
    motorEnable(0x01, 1);
    motorEnable(0x02, 1);
    motorEnable(0x03, 1);
    osDelay(50);

    // 3. 设置到达阈值（确保能收到 0x02 到位反馈）
    motorSetArrivalThreshold(0x01);
    motorSetArrivalThreshold(0x02);
    motorSetArrivalThreshold(0x03);
    osDelay(20);

    // 4. 开启同步标志（广播，双 X 轴必须）
    motorSyncEnable(1);
    osDelay(20);

    // 5. 标定零点：将当前所有电机位置作为绝对零点
    motorSetZero(0x01);
    motorSetZero(0x02);
    motorSetZero(0x03);
    osDelay(100);
}

/*
功能：读取实时转速信息
输入：slaveAddr 从机地址
输出：无
 */
void readRealTimeSpeed(uint16_t slaveAddr,uint16_t ID)
{
  // 1. 在栈上创建临时 Buffer (线程安全)
  uint8_t txBuffer[8] = {0}; 
  txBuffer[0] = 0x32;       //功能码
	CAN_Transmit_Data(&hfdcan1, ID, txBuffer, 2);
}

/**
 * @brief  等待 CAN 总线响应 (结构体版)
 * @param  delayTime: 超时时间 (ms)
 * @param  expectFunc: 期望的功能码 (如 0xF5)
 * @param  expectStatus: 期望的状态码 (如 0xFF 表示任意状态均可，0x02 表示必须到位)(裸机可用）
 * @retval 0: 成功, 1: 超时
 */
/**
*	uint8_t waitingForACK(uint32_t delayTime, uint8_t expectFunc, uint8_t expectStatus)
*	{
*			uint32_t startTick = HAL_GetTick();
*			CAN_Rx_Packet_t rx_packet;
*
*			while ((HAL_GetTick() - startTick) < delayTime)
*			{
*					// 尝试从队列获取数据包 (非阻塞)
*					if (osMessageQueueGet(can_rx_queue, &rx_packet, NULL, 0) == osOK)
*					{
*							// 1. 检查功能码是否匹配
*							if (rx_packet.FuncCode == expectFunc)
*							{
*									// 2. 检查状态码是否匹配 (如果 expectStatus 为 0xFF，则忽略状态检查)
*									if (expectStatus == 0xFF || rx_packet.Status == expectStatus)
*									{
*											return 0; // 匹配成功
*									}
*									// 如果功能码对但状态不对（比如还在运行中），继续等待
*							}
*					}
*
*					// 让出 CPU
*					osDelay(1);
*			}
*
*    return 1; // 超时
*	}
*/

void motorSyncTrigger(uint8_t slaveAddr) {
	   //  在栈上创建临时 Buffer (线程安全)
    uint8_t txBuffer[8] = {0}; 
		
    // 根据说明书 12.3.2，同步执行指令功能码为 0x4B
    // 通常使用广播地址 0 发送
    txBuffer[0] = 0x4B; 
    // 说明书示例中，校验和计算可能只需要功能码，或者包含ID。
    // 根据你的代码风格，可能需要设置 CAN_ID = 0 (广播)
    uint16_t broadcastId = 0; 
    CAN_Transmit_Data(&hfdcan1, broadcastId, txBuffer, 1); // 长度为1
}


/*
功能：串口发送速度模式运行指令
输入：slaveAddr 从机地址
      dir       运行方向
      speed     运行速度
      acc       加速度
*/
void speedModeRun(uint8_t slaveAddr,uint8_t dir,uint16_t speed,uint8_t acc)
{
    // 1. 在栈上创建临时 Buffer (线程安全)
    uint8_t txBuffer[8] = {0}; 
		
    txBuffer[0] = 0xF6;
    txBuffer[1] = (dir << 7) | (speed >> 8); // 高字节
    txBuffer[2] = speed & 0xFF;              // 低字节
    txBuffer[3] = acc; 											// 加速度
    
    // 发送时不包含 CRC，由 CAN_Transmit_Data 自动计算添加
    // 注意：这里 Length 传 4，函数内部会自动算 CRC 放在第 5 位
	 // 形成最终报文: ID + 0xF6 + Speed(2B) + Acc + CRC
    CAN_Transmit_Data(&hfdcan1, slaveAddr, txBuffer, 4); 
    
    // 等待应答 (修正后的函数需要传入期望的功能码 0xF6)
     //waitingForACK(100, 0xF6); 
}

/*
功能：读取实时位置信息
输入：slaveAddr 从机地址
输出：无
 */
void readRealTimeLocation(uint8_t slaveAddr)
{
    uint8_t txBuffer[8] = {0};
    txBuffer[0] = 0x31;       // 功能码
    CAN_Transmit_Data(&hfdcan1, slaveAddr, txBuffer, 1);
}

/*
功能：读取位置误差信息
输入：slaveAddr 从机地址
输出：无
 */
void readPositionError(uint8_t slaveAddr)
{
   // 1. 在栈上创建临时 Buffer (线程安全)
  uint8_t txBuffer[8] = {0}; 	
	
	//CAN_ID = slaveAddr;				//ID
  txBuffer[0] = 0x39;       //功能码
	CAN_Transmit_Data(&hfdcan1, slaveAddr, txBuffer, 2);
}


/*
功能：读取输入脉冲数信息
输入：slaveAddr 从机地址
输出：无
 */
void readInputPulses(uint8_t slaveAddr)
{
  // 1. 在栈上创建临时 Buffer (线程安全)
  uint8_t txBuffer[8] = {0}; 
	//CAN_ID = slaveAddr;				//ID
  txBuffer[0] = 0x33;       //功能码
	CAN_Transmit_Data(&hfdcan1, slaveAddr, txBuffer, 2);
}

/*
功能：读取IO端口状态信息
输入：slaveAddr 从机地址
输出：无
 */
void readIoStatus(uint8_t slaveAddr)
{
  // 1. 在栈上创建临时 Buffer (线程安全)
  uint8_t txBuffer[8] = {0}; 
	//CAN_ID = slaveAddr;				//ID
  txBuffer[0] = 0x34;       //功能码
	CAN_Transmit_Data(&hfdcan1, slaveAddr, txBuffer, 2);
}


/*
功能：串口发送位置模式1运行指令
输入：slaveAddr 从机地址
      dir       运行方向
      speed     运行速度
      acc       加速度
      pulses    脉冲数
*/
void positionMode1Run(uint8_t slaveAddr,uint8_t dir,uint16_t speed,uint8_t acc,uint32_t pulses)
{
    // 1. 在栈上创建临时 Buffer (线程安全)
    uint8_t txBuffer[8] = {0}; 
	//CAN_ID = slaveAddr;				//ID
  txBuffer[0] = 0xFD;       //功能码
  txBuffer[1] = (dir<<7) | ((speed>>8)&0x0F); //方向和速度高4位
  txBuffer[2] = speed&0x00FF;   //速度低8位
  txBuffer[3] = acc;            //加速度
  txBuffer[4] = (pulses >> 16)&0xFF;  //脉冲数 bit23 - bit16
  txBuffer[5] = (pulses >> 8)&0xFF;   //脉冲数 bit15 - bit8
  txBuffer[6] = (pulses >> 0)&0xFF;   //脉冲数 bit7 - bit0
	
CAN_Transmit_Data(&hfdcan1, slaveAddr, txBuffer, 8);
}

/*
功能：串口发送位置模式2运行指令
输入：slaveAddr 从机地址
      speed     运行速度
      acc       加速度
      relAxis   相对坐标
*/
void positionMode2Run(uint8_t slaveAddr,uint16_t speed,uint8_t acc,int32_t relAxis)
{
    // 1. 在栈上创建临时 Buffer (线程安全)
    uint8_t txBuffer[8] = {0}; 
	//CAN_ID = slaveAddr;				//ID
  txBuffer[0] = 0xF4;       //功能码
  txBuffer[1] = (speed>>8)&0x00FF; //速度高8位
  txBuffer[2] = speed&0x00FF;     //速度低8位
  txBuffer[3] = acc;            //加速度
  txBuffer[4] = (relAxis >> 16)&0xFF;  //相对坐标 bit23 - bit16
  txBuffer[5] = (relAxis >> 8)&0xFF;   //相对坐标 bit15 - bit8
  txBuffer[6] = (relAxis >> 0)&0xFF;   //相对坐标 bit7 - bit0
	
	CAN_Transmit_Data(&hfdcan1, slaveAddr, txBuffer, 8);
}

/*
功能：串口发送位置模式3运行指令
输入：slaveAddr 从机地址
      speed     运行速度
      acc       加速度
      absAxis   绝对坐标
*/
void positionMode3Run(uint8_t slaveAddr,uint16_t speed,uint16_t acc,int32_t absAxis)
{
	    // 1. 在栈上创建临时 Buffer (线程安全)
    uint8_t txBuffer[8] = {0}; 
    // CAN_ID = slaveAddr;				//ID
    txBuffer[0] = 0xF5;       //功能码
    txBuffer[1] = (speed>>8)&0x00FF; //速度高8位 
    txBuffer[2] = speed&0x00FF;     //速度低8位
    txBuffer[3] = acc;            //加速度
    txBuffer[4] = (absAxis >> 16)&0xFF;  //绝对坐标 bit23 - bit16
    txBuffer[5] = (absAxis >> 8)&0xFF;   //绝对坐标 bit15 - bit8
    txBuffer[6] = (absAxis >> 0)&0xFF;   //绝对坐标 bit7 - bit0
    // txBuffer[7] 会被 CAN_Transmit_Data 自动填充 CRC	
    CAN_Transmit_Data(&hfdcan1, slaveAddr, txBuffer, 7);
}


uint8_t motor_send_move_cmd(MotorController_t *motor, 
                            uint8_t slave_addr, 
                            int32_t absAxis,uint16_t speed,uint8_t acc){
	
	// 1. 在栈上创建临时 Buffer (线程安全)
  uint8_t txBuffer[8] = {0}; 
	
	    // TODO: 这里需要填充 positionMode3Run 的逻辑
    // 根据你的需求，这里应该是填充 txBuffer 数据
    // 例如：positionMode3Run 的逻辑...
		//CAN_ID = slave_addr;				//ID

    txBuffer[0] = 0xF5; // 功能码：坐标绝对运动
    txBuffer[1] = (speed >> 8) & 0xFF;
    txBuffer[2] = speed & 0xFF;
    txBuffer[3] = acc;
    txBuffer[4] = (absAxis >> 16) & 0xFF;
    txBuffer[5] = (absAxis >> 8) & 0xFF;
    txBuffer[6] = absAxis & 0xFF;
    // txBuffer[7] 通常为 CRC 或保留，根据协议填充
	  // 如果需要手动加 CRC，长度是 8；如果由 CAN 驱动处理数据域，长度传 7
    // 这里只负责发送，不关心结果
	uint8_t result;
	result = CAN_Transmit_Data(&hfdcan1, slave_addr, txBuffer, 7); // 7 字节数据，CRC 由函数自动计算添加

if (result == 0) {
    // 发送成功，进入等待状态
    motor->state = MOTOR_STATE_WAITING;
} else {
    // 发送失败（比如总线忙），进行重试或报错
    error_handling();
}
//    result = positionMode3Run(slave_addr, 300, 100, pulses); 
    return result; 
}


/*
功能：设置工作模式
输入：slaveAddr 从机地址
			Mode 工作模式
输出：无
 */
void setWorkMode(uint8_t slaveAddr,uint8_t Mode)
{   
	// 1. 在栈上创建临时 Buffer (线程安全)
  uint8_t txBuffer[8] = {0}; 
	//CAN_ID = slaveAddr;				//ID
  txBuffer[0] = 0x82;       //功能码
	txBuffer[1] = Mode;
	CAN_Transmit_Data(&hfdcan1, slaveAddr, txBuffer, 2);
  osDelay(10); // 等待总线稳定，具体时间根据实际情况调整
}

/*
功能：设置工作细分
输入：slaveAddr 从机地址
			MStep 工作细分
输出：无
 */
void setWorkMStep(uint8_t slaveAddr,uint8_t MStep)
{  
	// 1. 在栈上创建临时 Buffer (线程安全)
  uint8_t txBuffer[8] = {0}; 
	// CAN_ID = slaveAddr;				//ID
  txBuffer[0] = 0x84;       //功能码
	txBuffer[1] = MStep;			//细分		
	CAN_Transmit_Data(&hfdcan1, slaveAddr, txBuffer,2);
}

/*
功能：设置工作电流
输入：slaveAddr 从机地址
			Ma 工作电流
输出：无
 */
void setIWorkMode(uint8_t slaveAddr,uint16_t Ma)
{    
	// 1. 在栈上创建临时 Buffer (线程安全)
  uint8_t txBuffer[8] = {0}; 
	//CAN_ID = slaveAddr;				//ID
  txBuffer[0] = 0x83;       //功能码
	txBuffer[1] = Ma>>8;			//电流高8位
	txBuffer[2] = Ma;					//电流低8位
	CAN_Transmit_Data(&hfdcan1, slaveAddr, txBuffer,3);
}

/*
功能：恢复出厂参数
输入：slaveAddr 从机地址
			
输出：无
 */
void setRestore(uint8_t slaveAddr)
{    
	// 1. 在栈上创建临时 Buffer (线程安全)
  uint8_t txBuffer[8] = {0}; 
	//CAN_ID = slaveAddr;				//ID
  txBuffer[0] = 0x3F;       //功能码
	CAN_Transmit_Data(&hfdcan1, slaveAddr, txBuffer,2);
}

/* 新增函数：使能电机 (0xF3) */
void motorEnable(uint8_t slaveAddr, uint8_t enable) {
    uint8_t tx[8] = {0};
    tx[0] = 0xF3;
    tx[1] = enable ? 0x01 : 0x00;
    CAN_Transmit_Data(&hfdcan1, slaveAddr, tx, 2);
}

/* 设置位置到达阈值 (0x95) 保持默认值200，保证到位反馈 */
void motorSetArrivalThreshold(uint8_t slaveAddr) {
    uint8_t tx[8] = {0};
    tx[0] = 0x95;
    tx[1] = 0x01;        // enable = 1
    tx[2] = 0x00;        // values 高字节，默认200 => 0x00C8
    tx[3] = 0xC8;        // 低字节
    CAN_Transmit_Data(&hfdcan1, slaveAddr, tx, 4);
}

/* 开启多电机同步标志 (0x4A) */
void motorSyncEnable(uint8_t enable) {
    uint8_t tx[8] = {0};
    tx[0] = 0x4A;
    tx[1] = enable ? 0x01 : 0x00;
    CAN_Transmit_Data(&hfdcan1, 0x00, tx, 2); // 广播
}

/* 设置当前位置为零点 (0x92) */
void motorSetZero(uint8_t slaveAddr) {
    uint8_t tx[8] = {0};
    tx[0] = 0x92;
    CAN_Transmit_Data(&hfdcan1, slaveAddr, tx, 1);
}


//运行失败
void runFail(void)
{
			while(1)                
			{
				
			}	
}

//运行成功
void runOK(void)
{
			while(1)                
			{
				
			}	
}







