#ifndef __DRIVER_UART_H
#define __DRIVER_UART_H

#include <stdint.h>
#include <stdbool.h>
#include "usart.h" // 包含 HAL 定义
#include "stm32g4xx_hal.h" 

#ifdef __cplusplus
extern "C" {
#endif

/* ================= 配置宏 ================= */
// 定义支持的 UART 通道数量，需与 driver_uart.c 中的数组大小一致
#define UART_DRIVER_COUNT   4 

extern UART_HandleTypeDef hlpuart1;
/* ================= 枚举定义 ================= */

/**
 * @brief UART 通道 ID 定义
 * @note 对应 CubeMX 中配置的 USART 实例 (如 USART1, USART2, USART3)
 */
typedef enum {
    UART_CH1 = 0,
    UART_CH2 = 1,
    UART_CH3 = 2,
    UART_CH4 = 3, // 如果需要更多通道，继续添加
} Uart_Id_t;
/**
 * @brief 驱动操作状态码
 */
typedef enum {
    UART_OK                 = 0x00,
    UART_ERROR_PARAM        = 0x01, // 参数错误
    UART_ERROR_BUSY         = 0x02, // 总线忙
    UART_ERROR_TIMEOUT      = 0x03, // 超时
    UART_ERROR_TRANSMIT_FAILED = 0x04, // 发送失败
    UART_ERROR_RECEIVE_FAILED  = 0x05, // 接收失败/CRC错误
    UART_ERROR_UNKNOWN      = 0xFF,
    UART_ERROR_RING_FULL     = 0x06  // 发送环形缓冲区满
} UART_Status_t;



/* ================= 公共 API 声明 ================= */

void UART_SuspendRX(Uart_Id_t id) ;
void UART_ResumeRX(Uart_Id_t id) ;
/**
 * @brief 初始化 UART 驱动器
 * @note 需在 HAL_Init 和 MX_USARTx_UART_Init 之后调用。
 *       会自动启动所有通道的 DMA 空闲中断接收。
 */
void UART_Driver_Init(void);

/**
 * @brief 发送数据块 (DMA 非阻塞)
 * @param id UART 通道 ID
 * @param data 数据指针
 * @param size 数据长度
 * @return UART_Status_t 状态码
 * @note 如果返回 BUSY，说明上一次发送尚未完成。
 */
UART_Status_t UART_SendData(Uart_Id_t id, uint8_t *data, uint16_t size);

/**
 * @brief 发送字符串 (DMA 非阻塞)
 * @param id UART 通道 ID
 * @param str 字符串指针
 * @return UART_Status_t 状态码
 */
UART_Status_t UART_SendString(Uart_Id_t id, const char *str);

UART_Status_t UART_Write_DMA(Uart_Id_t id, const uint8_t *data, uint16_t size);

/**
 * @brief 驱动数据处理函数
 * @note **必须在主循环或高优先级 FreeRTOS 任务中高频调用**。
 *       负责将 DMA 缓冲区的数据拷贝到应用缓冲区，并重启 DMA 接收。
 *       如果不调用此函数，接收到的数据将无法被读取且 DMA 不会重启。
 */
void UART_Driver_Process(void);

/**
 * @brief 获取当前待处理的数据长度
 * @param id UART 通道 ID
 * @return uint16_t 数据长度 (若无新数据则为 0)
 * @note 仅在 UART_Driver_Process 检测到新数据后有效，直到下一次 Process 调用清除标志。
 */
uint16_t UART_GetRxCount(Uart_Id_t id);

/**
 * @brief 获取接收数据缓冲区指针
 * @param id UART 通道 ID
 * @return const uint8_t* 数据指针
 * @note 指针指向的数据在下次 UART_Driver_Process 调用前有效。
 */
const uint8_t* UART_GetRxBuffer(Uart_Id_t id);

/**
 * @brief UART 中断回调入口
 * @param huart UART 句柄指针
 * @note 需在 stm32g4xx_it.c 的 HAL_UARTEx_RxEventCallback 中调用此函数。
 *       示例: 
 *       void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart) {
 *           UART_IRQ_Handler(huart);
 *       }
 */
void UART_IRQ_Handler(UART_HandleTypeDef *huart);
/**
 * @brief 专用于 TMC2209 的发送函数 (阻塞式)
 * @note TMC2209 通信需要严格的时序，建议使用阻塞发送以确保数据完整性
 * @param id UART 通道 ID (例如 UART_CH1)
 * @param data 数据指针
 * @param size 数据长度 (TMC2209 固定为 8 字节或 4 字节)
 * @return UART_Status_t 状态码
 */
UART_Status_t UART_TMC_Send(Uart_Id_t id, uint8_t *data, uint16_t size);

/**
 * @brief UART 错误回调入口
 * @param huart UART 句柄指针
 * @note 需在 stm32g4xx_it.c 的 HAL_UART_ErrorCallback 中调用此函数。
 *       示例:
 *       void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart) {
 *           UART_Error_Handler(huart);
 *       }
 */
void UART_Error_Handler(UART_HandleTypeDef *huart);

// 获取溢出计数 (用于调试)
uint32_t UART_Get_Overflow_Count(Uart_Id_t id);

// 接收数据 (非阻塞或DMA模式更佳，此处先用阻塞示例)
HAL_StatusTypeDef UART_TMC_Receive(uint8_t *data, uint16_t size);

/**
 * @brief 检查指定通道是否有新数据
 * @param id UART 通道 ID
 * @return true 表示有未处理的数据，false 表示无数据
 */
bool UART_HasData(Uart_Id_t id);

/**
 * @brief 获取接收到的数据（不自动清除标志）
 * @param id   UART 通道 ID
 * @param data 输出数据指针（指向内部缓冲区，下次调用前有效）
 * @param len  输出数据长度
 * @return true 表示成功获取到数据，false 表示无新数据
 * @note 获取后不会自动清除就绪标志，需配合 UART_ClearData 使用
 */
bool UART_PeekData(Uart_Id_t id, const uint8_t **data, uint16_t *len);

/**
 * @brief 清除指定通道的数据就绪标志
 * @param id UART 通道 ID
 * @note 调用后该通道的数据将被视为已处理，下次 DMA 接收会覆盖缓冲区
 */
void UART_ClearData(Uart_Id_t id);

void TMC_UART_Transmit(uint8_t *data, uint16_t size) ;
uint8_t TMC_UART_Receive(void) ;

void UART_RestartRX(Uart_Id_t id);

#ifdef __cplusplus
}
#endif

#endif /* __DRIVER_UART_H */
