#include "stm32g4xx_hal.h"
#include "fdcan.h"
#include <string.h>
#include "driver_can.h"
#include "timestamp.h"
#include "stm32g4xx_hal_fdcan.h"
#include "cmsis_os.h"
#include "app_motion.h"
#include "queue.h"
#include "app_test.h"

// 定义队列缓冲区（假设最大深度为 10）
uint8_t rx_queue_buffer[10]; 

volatile uint32_t g_rx_irq_count = 0;//全局计数器检查中断是否触发

uint16_t  CAN_ID;

uint8_t  CAN_RxDone = FALSE;  //接收标致位

// CAN通信发送缓冲区 

// 定义接收缓冲区变量 (根据你的 HAL 库版本调整)
FDCAN_RxHeaderTypeDef RxHeader;
uint8_t RxData[8];


// 电机共享区

// CAN1对应的ID
uint8_t CAN1_0x1fe_Tx_Data[8];
uint8_t CAN1_0x1ff_Tx_Data[8];
uint8_t CAN1_0x200_Tx_Data[8];
uint8_t CAN1_0x2fe_Tx_Data[8];
uint8_t CAN1_0x2ff_Tx_Data[8];
uint8_t CAN1_0x3fe_Tx_Data[8];
uint8_t CAN1_0x4fe_Tx_Data[8];

struct Struct_CAN_Manage_Object CAN1_Manage_Object = {NULL};


/**
 * @brief 配置CAN的过滤器
 * 默认开了fifo0和fifo1的全通滤波器, 但由于fifo0和fifo1匹配规则一致, 因此fifo1理论上不会被触发, 即使fifo0满
 * 如若出现接收满的情况, 可配置掩码选择性接收, 合理分担总线带宽
 * 此外, Cortex-M7内核的滤波器配置在每个CAN实例都是独立的, 且滤波器编号也都是独立的
 * 如, F4系列芯片的CAN1和CAN2的滤波器编号分别是0-13和14-27, 但H7系列芯片的FDCAN1和FDCAN2的滤波器编号都可以从0开始
 *
 * @param hfdcan CAN编号
 */
void can_filter_mask_config(FDCAN_HandleTypeDef *hfdcan)
{
    FDCAN_FilterTypeDef can_filter_init_structure;

    // 配置fifo0全通滤波器
    can_filter_init_structure.IdType = FDCAN_STANDARD_ID;
    can_filter_init_structure.FilterIndex = 0;
    can_filter_init_structure.FilterType = FDCAN_FILTER_MASK;
    can_filter_init_structure.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
    can_filter_init_structure.FilterID1 = 0x00000000;//标准ID
    can_filter_init_structure.FilterID2 = 0xFFE00000;//掩码（全通）      FilterID2：掩码位（1=忽略该位，0=必须匹配）
    HAL_FDCAN_ConfigFilter(hfdcan, &can_filter_init_structure);

    // 全局滤波器, 直接拒绝不符合规则的标准数据帧, 扩展数据帧, 标准遥控帧, 扩展遥控帧
//    HAL_FDCAN_ConfigGlobalFilter(hfdcan, FDCAN_REJECT, FDCAN_REJECT, FDCAN_FILTER_REMOTE, FDCAN_FILTER_REMOTE);

    // 启动CAN中断与总线
    HAL_FDCAN_ActivateNotification(hfdcan, FDCAN_IT_RX_FIFO0_NEW_MESSAGE | FDCAN_IT_BUS_OFF | FDCAN_IT_ERROR_PASSIVE | FDCAN_IT_ARB_PROTOCOL_ERROR | FDCAN_IT_DATA_PROTOCOL_ERROR, 0);
}

/**
 * @brief 初始化CAN总线
 *
 * @param hfdcan CAN编号
 * @param Callback_Function 处理回调函数
 */
void CAN_Init(FDCAN_HandleTypeDef *hfdcan, CAN_Callback Callback_Function)
{
    if (hfdcan->Instance == FDCAN1)
    {
        CAN1_Manage_Object.CAN_Handler = hfdcan;

    }
    

    can_filter_mask_config(hfdcan);// 这行只负责配置滤波器，不再激活中断

    HAL_FDCAN_Start(hfdcan);
		    // 启动后再激活中断
//    HAL_FDCAN_ActivateNotification(hfdcan,
//        FDCAN_IT_RX_FIFO0_NEW_MESSAGE | FDCAN_IT_BUS_OFF |
//        FDCAN_IT_ERROR_PASSIVE | FDCAN_IT_ARB_PROTOCOL_ERROR |
//        FDCAN_IT_DATA_PROTOCOL_ERROR, 0);
}

/**
 * @brief 发送数据帧
 * @param hcan CAN编号
 * @param CAN_ID ID
 * @param Data 被发送的数据指针
 * @param Length 长度
 * @return uint8_t 执行状态
 * @note 使用标准CAN
 */
uint8_t CAN_Transmit_Data(FDCAN_HandleTypeDef *hfdcan, uint16_t can_id, uint8_t *Data, uint16_t Length)
{
	uint8_t total_len;
    FDCAN_TxHeaderTypeDef tx_header;
	// 在栈上创建临时缓冲区
    uint8_t buf[8] = {0};
	    // --- 参数检查 ---
    if (Data == NULL || Length == 0 || Length > 7) return 1; // 最多7字节有效数据，留1字节给CRC
	// --- 数据预处理 ---
	
    uint8_t data_len = (Length > 7) ? 7 : Length;

    memcpy(buf, Data, Length);
    // 2. 计算 CRC (公式: ID + Data[0] 到 Data[Length-1])
    uint8_t crc = (uint8_t)can_id; // 地址参与校验
    for(int i = 0; i < Length; i++) {
        crc += Data[i];
    }

    //  将 CRC 放入发送缓冲区的最后位置(此处可能需要修改)
    // 注意：如果 Length 已经是 8，这里会溢出，所以限制 Length <= 7
    if (Length < 8) {
    buf[Length] = crc;
    total_len = data_len + 1;  // 数据长度 + CRC
   } 


    tx_header.Identifier = can_id;
    tx_header.IdType = FDCAN_STANDARD_ID;
    tx_header.TxFrameType = FDCAN_DATA_FRAME;
    tx_header.DataLength = total_len;
    tx_header.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    tx_header.BitRateSwitch = FDCAN_BRS_OFF;
    tx_header.FDFormat = FDCAN_CLASSIC_CAN;
    tx_header.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
    tx_header.MessageMarker = 0;
	     // ---- 调试打印（暂时加入）----
    PrintDebug("TX ID=%d LEN=%d DATA:", can_id, total_len);
    for (int i = 0; i < total_len; i++) {
        PrintDebug(" %02X", buf[i]);
    }
    PrintDebug("\r\n");

	HAL_StatusTypeDef status =  HAL_FDCAN_AddMessageToTxFifoQ(hfdcan, &tx_header,  buf);

    // 转换返回值：HAL_OK(0) -> 返回0; 其他错误 -> 返回1
    return (status == HAL_OK) ? 0 : 1;	
}

/**
 * @brief CAN的TIM定时器中断发送回调函数
 *
 */
void TIM_100us_CAN_PeriodElapsedCallback()
{
	// 后续根据电机的具体型号改动回调函数内容
	
}


///**
// * @brief CAN的TIM定时器中断发送回调函数
// *
// */
//void TIM_1ms_CAN_PeriodElapsedCallback()
//{
//    // DJI电机专属

//    static int mod2 = 0;
//    mod2++;
//    if (mod2 == 2)
//    {
//        mod2 = 0;

//        // 发送实例
//        // CAN_Transmit_Data(&hfdcan2, 0x1fe, CAN2_0x1fe_Tx_Data, 8);
//    }

//    CAN_Transmit_Data(&hfdcan1, 0x3fe, CAN1_0x3fe_Tx_Data, 8);
//}

/**
 * @brief HAL库CAN接收FIFO0中断
 *
 * @param hfdcan CAN编号
 */
void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *hfdcan, uint32_t RxFifo0ITs)
{
    if((RxFifo0ITs & FDCAN_IT_RX_FIFO0_NEW_MESSAGE) != RESET) {
				g_rx_irq_count++;
        FDCAN_RxHeaderTypeDef header;
        uint8_t rx_data[8] = {0};
        
        if (HAL_FDCAN_GetRxMessage(hfdcan, FDCAN_RX_FIFO0, &header, rx_data) == HAL_OK) {
            
            // 1. 解析数据包 (假设 data[1] 是状态字，0x02 代表到位)
            CAN_Rx_Packet_t pkt;
			pkt.ID = header.Identifier; // 记录 ID，否则不知道是谁发的
            pkt.Timestamp = HAL_GetTick();
            // pkt.Data[0] 将对应功能码 (FuncCode)
            // pkt.Data[1] 将对应状态码 (Status)
            memcpy(pkt.Data, rx_data, 8); 
						pkt.FuncCode = rx_data[0];
						if (header.DataLength >= 2) {
								pkt.Status = rx_data[1];    // 状态码
						} else {
								pkt.Status = 0xFF;          // 无意义
						}
						
						 PrintDebug("RX: ID=%d LEN=%d DATA:", header.Identifier, header.DataLength);
            for (int i = 0; i < header.DataLength; i++) {
                PrintDebug(" %02X", rx_data[i]);
            }
            PrintDebug("\r\n");
						
            osMessageQueuePut(motor_event_queue, &pkt, 0, 0);            
        }    
             // 重新激活中断           
            HAL_FDCAN_ActivateNotification(hfdcan, FDCAN_IT_RX_FIFO0_NEW_MESSAGE, 0);
        
    }
}

/**
 * @brief HAL库CAN错误中断
 *
 * @param hfdcan CAN编号
 * @param ErrorStatusITs 错误状态
 */
void HAL_FDCAN_ErrorStatusCallback(FDCAN_HandleTypeDef *hfdcan, uint32_t ErrorStatusITs)
{
    if ((ErrorStatusITs & FDCAN_IT_BUS_OFF) != RESET)
    {
        // CAN总线离线, 重新启动CAN
        HAL_FDCAN_Stop(hfdcan);
        HAL_FDCAN_Start(hfdcan);
    }
}

//计算校验和
uint8_t canCRC_ATM(uint8_t *buf,uint8_t len) //CRC_SUM8
{
	uint32_t i;
	uint8_t check_sum;
	uint32_t sum = 0;
	
	for(i=0;i<len;i++)
	{
		sum += buf[i];
	}
	sum += CAN_ID;
	check_sum = sum & 0xFF;
	return check_sum;
}

/**
 * @brief  电机错误处理函数
 * @note   这是一个占位实现，用于解决链接错误。
 *         请根据实际需求在此处添加错误处理逻辑（如急停、报警等）。
 */
void error_handling(void)
{
    // 1. 防止编译器优化警告
    // 这里可以添加你的错误处理逻辑，例如：
    // - 设置一个全局错误标志位
    // - 点亮错误指示灯
    // - 发送错误日志到串口
    // - 触发系统复位或急停

    // 示例：简单的死循环（调试用，实际项目慎用）
    // while(1);

    // 示例：仅作为空实现，避免链接报错
    }
