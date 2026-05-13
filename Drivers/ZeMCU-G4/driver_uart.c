#include "driver_uart.h"
#include "dma.h"
#include "usart.h"
#include "tmc_protocol.h" // 引入 TMC 协议头文件
#include "app_test.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* FreeRTOS 头文件，用于临界区保护 */
#include "FreeRTOS.h"
#include "task.h"

extern void Host_UartRecvCallback(uint8_t *data, int len);
extern void CamUart_RecvCallback(uint8_t *data, int len);

 /* @硬件连接 :
 * - UART1: TX=PE0, RX=PE1 
 * - UART2: TX=PD5, RX=PD6
 * - UART3: TX=PB9, RX=PB11
*/

/* ================= 内部配置宏 ================= */
#define RX_BUFFER_SIZE      256     // DMA 接收缓冲区大小 足够容纳多帧及可能的噪声
#define MAX_TIMEOUT_MS      1000    // 发送超时时间


// 单个 UART 通道的控制块
typedef struct {
    UART_HandleTypeDef *huart;      // CubeMX 生成的句柄指针
    DMA_HandleTypeDef *hdmarx;      // RX DMA 句柄 (用于获取剩余计数)
    
    uint8_t rx_dma_buf[RX_BUFFER_SIZE]; // DMA 原始缓冲区
    uint8_t rx_app_buf[RX_BUFFER_SIZE]; // 应用层读取缓冲区
    
    volatile uint16_t last_ndtr;    // 上次 NDTR (用于调试或复杂逻辑)
    volatile uint16_t data_len;     // 当前待处理数据长度
    volatile bool data_ready;       // 数据就绪标志
    
    volatile bool is_rx_active;     // 接收是否正在运行
    volatile uint32_t overflow_count;
} UART_Channel_t;

// 全局通道数组
static UART_Channel_t uart_channels[UART_DRIVER_COUNT];

 void UART_StartReceive_DMA(UART_Channel_t *ch);
/* 接收缓冲区 (用于DMA) */
 /**
 * @brief 专用于 TMC2209 的发送函数 (阻塞式)
 * @note 使用 HAL_UART_Transmit 确保数据立即发送完毕，避免 DMA 竞争
 */
UART_Status_t UART_TMC_Send(Uart_Id_t id, uint8_t *data, uint16_t size) {
	
    if ((int)id >= UART_DRIVER_COUNT || data == NULL || size == 0) return UART_ERROR_PARAM;
    
    //  获取通道指针 (修复：定义 ch 指针)
    UART_Channel_t *ch = &uart_channels[id];
    UART_HandleTypeDef *huart = ch->huart;

    // 注意：TMC2209 单线通信时，发送期间最好暂停接收DMA，防止回环干扰
    // 但如果你的硬件TX/RX是分开的（全双工），则不需要暂停。
    // 如果是单线半双工，需要在此处 HAL_UART_DMAStop(huart);

    // 暂停 DMA 接收
    // 原因：TMC2209 是单线半双工。如果不停止 DMA，发送的数据会被 RX 引脚读回，
    // 导致 DMA 缓冲区填满发送的数据，而不是我们想要的回复数据。
    if (ch->is_rx_active) { 
        HAL_UART_DMAStop(huart);
        ch->is_rx_active = false;
    }

        // 切换到发送模式
    HAL_HalfDuplex_EnableTransmitter(huart);

    // 4. 执行阻塞发送
    // 超时时间设为 10ms，对于 8 字节短包足够
    HAL_StatusTypeDef hal_status = HAL_UART_Transmit(huart, data, size, 10);
		for (volatile uint32_t i = 0; i < 5000; i++)
		    // 等待发送器完全关闭，总线恢复高电平
    for (volatile uint32_t i = 0; i < 5000; i++);  // 几十微秒，根据主频调整

        // 切回接收模式
    HAL_HalfDuplex_EnableReceiver(huart);
		
    // 5. 关键步骤：重启 DMA 接收
    // 无论发送成功与否，都必须尽快恢复接收状态，以便接收 TMC 的回复
    // 注意：TMC 回复有延迟，DMA 需要处于监听状态
//    vTaskDelay(1); // 短暂等待总线稳定，避免发送后立即启动 DMA 导致干扰
     UART_StartReceive_DMA(ch); 
    // 6. 返回结果
    if (hal_status == HAL_OK) {
        return UART_OK;
    } else {
        return UART_ERROR_TRANSMIT_FAILED;
    }
}

//HAL_StatusTypeDef UART_TMC_Receive(uint8_t *data, uint16_t size) {
//    return HAL_LPUART_Receive(&hlpuart1, data, size, 100);
//}

/**
 * @brief 启动 DMA 接收 (带空闲中断检测)
 * @note 此函数应在初始化或数据处理完成后立即调用
 */
void UART_StartReceive_DMA(UART_Channel_t *ch)
{
//    HAL_HalfDuplex_EnableReceiver(ch->huart);
    if (ch->is_rx_active) 
    {
        // 如果已经激活，先强制停止，防止状态混乱
        HAL_UART_DMAStop(ch->huart); // 关键：确保 DMA 停止
        ch->is_rx_active = false;
    }
//		return; // 防止重复启动
		
     //清空 DMA 缓冲区 (防止残留数据导致误判长度)
//    memset(ch->rx_dma_buf, 0, RX_BUFFER_SIZE);
	// 重置状态
//    ch->data_len = 0;
//    ch->data_ready = false;
//    ch->last_ndtr = RX_BUFFER_SIZE;

	// 清除可能残留的中断标志
		__HAL_UART_CLEAR_IDLEFLAG(ch->huart);
		__HAL_UART_CLEAR_OREFLAG(ch->huart); // 清除溢出标志	
		
    // 启动接收，当总线空闲时触发回调
    if (HAL_UARTEx_ReceiveToIdle_DMA(ch->huart, ch->rx_dma_buf, RX_BUFFER_SIZE) == HAL_OK) {
        ch->is_rx_active = true;
	
        // 确保空闲中断已开启 (CubeMX 通常会自动开启，但显式确认更安全)
//        __HAL_UART_ENABLE_IT(ch->huart, UART_IT_IDLE);
    } else {
        ch->is_rx_active = false;
        // 可在此处添加错误日志
    }
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{

    UART_Channel_t *ch = NULL;
    // 1. 查找对应通道
    for (int i = 0; i < UART_DRIVER_COUNT; i++) {
        if (uart_channels[i].huart == huart) {
            ch = &uart_channels[i];
            break;
        }
    }
    if (ch == NULL) return;

    if (huart == &huart1) {
        Host_UartRecvCallback((uint8_t*)huart->pRxBuffPtr, Size);
    } else if (huart == &huart2) {
        CamUart_RecvCallback((uint8_t*)huart->pRxBuffPtr, Size);
    }
    if (huart->Instance == USART3) {
        PrintDebug("RxEvt Size=%d\r\n", Size);
    }
    // 2. 直接使用 HAL 传进来的 Size (这是最准确的)
    if (Size > 0 && Size <= RX_BUFFER_SIZE) {
        // 这里我们直接操作结构体，或者放入队列
        // 注意：不要在这里做耗时操作，只做数据拷贝或标记
        
        // 假设你有一个全局缓冲区或使用临界区

        ch->data_len = Size;
        ch->data_ready = true;

    }	

}


/**
 * @brief 初始化 UART 驱动器
 */
void UART_Driver_Init(void)
{
    // 外部引用 CubeMX 生成的句柄
    extern UART_HandleTypeDef huart1;
    extern UART_HandleTypeDef huart2;
    extern UART_HandleTypeDef huart3;
    extern UART_HandleTypeDef hlpuart1;

    extern DMA_HandleTypeDef hdma_usart1_rx; // 确保使用了 RX DMA
    extern DMA_HandleTypeDef hdma_usart2_rx;
    extern DMA_HandleTypeDef hdma_usart3_rx;
//    extern DMA_HandleTypeDef hdma_lpuart1_rx;

    // 配置 UART1
    uart_channels[UART_CH1].huart = &huart1;
    uart_channels[UART_CH1].hdmarx = &hdma_usart1_rx;
    uart_channels[UART_CH1].is_rx_active = false;
    huart1.gState = HAL_UART_STATE_READY; 
    // 配置 UART2
    uart_channels[UART_CH2].huart = &huart2;
    uart_channels[UART_CH2].hdmarx = &hdma_usart2_rx;
    uart_channels[UART_CH2].is_rx_active = false;
    huart2.gState = HAL_UART_STATE_READY; 
    // 配置 UART3
    uart_channels[UART_CH3].huart = &huart3;
    uart_channels[UART_CH3].hdmarx = &hdma_usart3_rx;
    uart_channels[UART_CH3].is_rx_active = false;
    huart3.gState = HAL_UART_STATE_READY; 
    //配置 LPUART1
    uart_channels[UART_CH4].huart = &hlpuart1;
    uart_channels[UART_CH4].hdmarx = NULL;
    uart_channels[UART_CH4].is_rx_active = false;
    hlpuart1.gState = HAL_UART_STATE_READY; 



    // 启动所有通道的接收
    for (int i = 0; i < UART_DRIVER_COUNT; i++) {
			  if (i == UART_CH3) {
         // UART3 由 TMC2209 专用，不启用 DMA 空闲接收
					continue;
        }
        UART_StartReceive_DMA(&uart_channels[i]);
    }
}

/**
 * @brief 重启指定通道的 DMA 接收（先停止再启动）
 */
void UART_RestartRX(Uart_Id_t id) {
    if ((int)id >= UART_DRIVER_COUNT) return;
    UART_Channel_t *ch = &uart_channels[id];
    HAL_UART_DMAStop(ch->huart);
    ch->is_rx_active = false;
    UART_StartReceive_DMA(ch);
}

/**
 * @brief 发送数据块 (DMA 非阻塞)
 * @param id UART 通道 ID
 * @param data 数据指针
 * @param size 数据长度
 * @return UART_Status_t 状态码
 * @note 如果返回 BUSY，说明上一次发送尚未完成。
 */
UART_Status_t UART_SendData(Uart_Id_t id, uint8_t *data, uint16_t size)
{
    if ((int)id >= UART_DRIVER_COUNT || data == NULL || size == 0) 
		return UART_ERROR_PARAM;
    
    UART_Channel_t *ch = &uart_channels[id];
		
		// 检查UART状态
    HAL_UART_StateTypeDef state = HAL_UART_GetState(ch->huart);
		
		// 简单阻塞检查，后续改为用队列
//    if (HAL_UART_GetState(ch->huart) != HAL_UART_STATE_READY) {
//        return UART_ERROR_BUSY;
//    }
    HAL_UART_StateTypeDef txState = ch->huart->gState;
    if (txState == HAL_UART_STATE_BUSY_TX || txState == HAL_UART_STATE_BUSY_TX_RX) {
        return UART_ERROR_BUSY; // 只有 TX 真正忙的时候才拒绝
    }
    if (HAL_UART_Transmit_DMA(ch->huart, data, size) != HAL_OK) {
        return UART_ERROR_TRANSMIT_FAILED;
    }
    return UART_OK;
    
//    // 如果UART正忙，使用队列机制而不是阻塞等待
//    if (state == HAL_UART_STATE_BUSY_TX || 
//        state == HAL_UART_STATE_BUSY_TX_RX) 
//			{
//        // 方案1：返回忙状态，让上层处理
//        return UART_ERROR_BUSY;
//        
//        // 方案2：使用发送队列（推荐）
//        //return UART_QueueSend(id, data, size);			hdma_usart1_rx		
			
//			}
    
 // 启动DMA传输
//    HAL_StatusTypeDef hal_status = HAL_UART_Transmit_DMA(ch->huart, (uint8_t*)data, size);
//    
//    if (hal_status != HAL_OK) {
//        return UART_ERROR_HAL;
//    }
//    
//    // 设置传输完成标志或回调
//    ch->tx_busy = true;
//    
//    return UART_OK;
}




/**
  * @brief： UART_SendString 发送字符串
  * @param： Uart_Id_t:UART 通道 ID
  * @param： UART_SendData
  * @note  
 */

UART_Status_t UART_SendString(Uart_Id_t id, const char *str)
{
    if ((int)id >= UART_DRIVER_COUNT || str == NULL) 
		return UART_ERROR_PARAM;
    
    uint16_t len = strlen(str);
    if (len == 0) return UART_OK;
		return UART_SendData(id, (uint8_t*)str, len);

}

void TMC_UART_Transmit(uint8_t *data, uint16_t size) {
    // 发送数据 (阻塞模式)
    HAL_UART_Transmit(&huart3, data, size, 100); 
}

uint8_t TMC_UART_Receive(void) {
    uint8_t data = 0;
    // 接收数据 (阻塞模式)
    HAL_UART_Receive(&huart3, &data, 1, 100);
    return data;
}

/**
 * @brief 驱动处理函数
 * @note 需在 FreeRTOS 任务或主循环中高频调用，用于处理接收到的数据并重启 DMA
 */
void UART_Driver_Process(void)
{

    for (int i = 0; i < UART_DRIVER_COUNT; i++) {
        UART_Channel_t *ch = &uart_channels[i];

        if (ch->data_ready) {
            // 进入临界区保护
            taskENTER_CRITICAL();

            uint16_t current_len = ch->data_len;
            if (current_len > 0 && current_len <= RX_BUFFER_SIZE) {
                // 拷贝到应用缓冲区（注意：不清除 data_ready 标志）
                memcpy(ch->rx_app_buf, ch->rx_dma_buf, current_len);
                // data_len 保留，供上层读取
            } else {
                // 异常长度，放弃本次数据（将 data_ready 置 false）
                ch->data_ready = false;
                ch->data_len = 0;
            }

            taskEXIT_CRITICAL();

            // 重新启动 DMA 接收（注意：data_ready 可能仍为 true，但 DMA 缓冲区已被拷贝，可安全覆盖）
            if (ch->huart->gState != HAL_UART_STATE_BUSY_RX) {
                UART_StartReceive_DMA(ch);
            }
        }
    }
}

/**
 * @brief UART_GetRxCount 获取接收到的数据长度
 * @param id UART 通道 ID
 * @note 
 */

uint16_t UART_GetRxCount(Uart_Id_t id) 
{
    if ((int)id >= UART_DRIVER_COUNT) return 0;
    // 仅在 data_ready 时有效，否则返回 0 或实时计算 NDTR
    if (uart_channels[id].data_ready) {
        return uart_channels[id].data_len;
    }
    return 0;
}
/**
 * @brie UART_GetRxBuffer 获取应用层缓冲区指针
 * @param id UART 通道 ID
 * @note 
 */
const uint8_t* UART_GetRxBuffer(Uart_Id_t id) 
	{
    if ((int)id >= UART_DRIVER_COUNT) 
			return NULL;
    return uart_channels[id].rx_app_buf;
}

/**
 * @brief UART 中断回调处理
 * @param  huart: UART句柄
 * @param  Size:  接收到的数据长度 (由 HAL_UARTEx_RxEventCallback 传入)
 * @note 在 stm32g4xx_it.c 的 HAL_UARTEx_RxEventCallback 中调用此函数
 */
//void UART_IRQ_Handler(UART_HandleTypeDef *huart)
    //{
    //    UART_Channel_t *ch = NULL;
    //		uint16_t received_len  ; 
    //    // 查找对应通道
    //    for (int i = 0; i < UART_DRIVER_COUNT; i++) {
    //        if (uart_channels[i].huart == huart) {
    //            ch = &uart_channels[i];
    //            break;
    //        }
    //    }
    //    
    //    if (ch == NULL )return;
    //		
    //		// 2. 清除标志位
    //    __HAL_UART_CLEAR_IDLEFLAG(huart);
    //		

    //    // 公式：接收到的长度 = 缓冲区总大小 - DMA 当前剩余计数
    //    received_len = RX_BUFFER_SIZE - __HAL_DMA_GET_COUNTER(ch->hdmarx);
    //		// 4. 安全检查
    //    if (received_len > 0 && received_len <= RX_BUFFER_SIZE)
    //    {
    //        // 5. 保存数据长度
    //        ch->data_len = received_len;
    //        ch->data_ready = true;
    //        
    //        // 6. 标记接收停止，等待 Process 重启
    //        ch->is_rx_active = false; 
    //    }
    //    else
    //    {
    //        // 长度异常，可能是溢出或干扰，重启 DMA
    //        ch->is_rx_active = false;
    //        UART_StartReceive_DMA(ch);
    //    }
    //}

// 获取溢出计数 (用于调试)
uint32_t UART_Get_Overflow_Count(Uart_Id_t id) {
    if ((int)id >= UART_DRIVER_COUNT) return 0;
    return uart_channels[id].overflow_count;
}

/**
 * @brief UART 错误回调入口
 * @param huart UART 句柄指针
 * @note 需在 stm32g4xx_it.c 的 HAL_UART_ErrorCallback 中调用此函数。
 *       示例:
 *       void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart) {
 *           UART_Error_Handler(huart);
 *       }
 */

void UART_Error_Handler(UART_HandleTypeDef *huart)
{
    UART_Channel_t *ch = NULL;
    for (int i = 0; i < UART_DRIVER_COUNT; i++) {
        if (uart_channels[i].huart == huart) {
            ch = &uart_channels[i];
            break;
        }
    }
    if (ch == NULL) return;

    // 清除错误标志 (STM32G4 系列)
    __HAL_UART_CLEAR_OREFLAG(huart);
    __HAL_UART_CLEAR_NEFLAG(huart);
    __HAL_UART_CLEAR_FEFLAG(huart);
    __HAL_UART_CLEAR_PEFLAG(huart);

    // 停止当前 DMA
  //  HAL_DMA_Abort(ch->hdmarx);//中止正在进行中的 DMA 传输   如果 DMA 已经死锁，这一步会卡住或失败
    ch->overflow_count++;
    ch->is_rx_active = false;

    UART_StartReceive_DMA(ch);
		
}


/**
 * @brief DMA传输完成回调函数（需要在HAL配置中设置）
 */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    // 找到对应的通道
    for (int i = 0; i < UART_DRIVER_COUNT; i++) {
        if (uart_channels[i].huart == huart) {
//            uart_channels[i].tx_busy = false;
            
            // 如果有发送队列，可以在这里触发下一个发送
            // UART_ProcessQueue(i);
            
            // 如果使用了动态分配的缓冲区，在这里释放
//            vPortFree(uart_channels[i].tx_buffer);
            
            break;
        }
    }
}
		
		
/**
 * @brief 暂停指定通道的 DMA 接收，为阻塞发送做准备
 */
void UART_SuspendRX(Uart_Id_t id) {
    if ((int)id >= UART_DRIVER_COUNT) return;
    UART_Channel_t *ch = &uart_channels[id];
    HAL_UART_DMAStop(ch->huart);
    HAL_UART_Abort(ch->huart);   // 彻底释放资源
    ch->is_rx_active = false;
}

/**
 * @brief 恢复指定通道的 DMA 空闲中断接收
 */
 
void UART_ResumeRX(Uart_Id_t id) {
    if ((int)id >= UART_DRIVER_COUNT) return;
    UART_Channel_t *ch = &uart_channels[id];
    UART_StartReceive_DMA(ch);    // 内部 static 但这里可以调用
}
		
bool UART_HasData(Uart_Id_t id)
{
    if ((int)id >= UART_DRIVER_COUNT) return false;
    return uart_channels[id].data_ready;
}

bool UART_PeekData(Uart_Id_t id, const uint8_t **data, uint16_t *len)
{
    if ((int)id >= UART_DRIVER_COUNT || data == NULL || len == NULL)
        return false;

    UART_Channel_t *ch = &uart_channels[id];
    if (!ch->data_ready) return false;

    *data = ch->rx_app_buf;
    *len = ch->data_len;
    return true;
}

void UART_ClearData(Uart_Id_t id)
{
    if ((int)id >= UART_DRIVER_COUNT) return;
    UART_Channel_t *ch = &uart_channels[id];
    ch->data_ready = false;
    ch->data_len = 0;
}