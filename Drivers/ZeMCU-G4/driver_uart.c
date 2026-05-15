#include "driver_uart.h"
#include "dma.h"
#include "usart.h"
#include "tmc_protocol.h" // 寮曞叆 TMC 鍗忚澶存枃浠?
#include "app_test.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* FreeRTOS 澶存枃浠讹紝鐢ㄤ簬涓寸晫鍖轰繚鎶?*/
#include "FreeRTOS.h"
#include "task.h"

extern void Host_UartRecvCallback(uint8_t *data, int len);
extern void CamUart_RecvCallback(uint8_t *data, int len);

 /* @纭欢杩炴帴 :
 * - UART1: TX=PE0, RX=PE1 
 * - UART2: TX=PD5, RX=PD6
 * - UART3: TX=PB9, RX=PB11
*/

/* ================= 鍐呴儴閰嶇疆瀹?================= */
#define RX_BUFFER_SIZE      256     // DMA 鎺ユ敹缂撳啿鍖哄ぇ灏?瓒冲瀹圭撼澶氬抚鍙婂彲鑳界殑鍣０
#define MAX_TIMEOUT_MS      1000    // 鍙戦€佽秴鏃舵椂闂?


// 鍗曚釜 UART 閫氶亾鐨勬帶鍒跺潡
typedef struct {
    UART_HandleTypeDef *huart;      // CubeMX 鐢熸垚鐨勫彞鏌勬寚閽?
    DMA_HandleTypeDef *hdmarx;      // RX DMA 鍙ユ焺 (鐢ㄤ簬鑾峰彇鍓╀綑璁℃暟)
    
    uint8_t rx_dma_buf[RX_BUFFER_SIZE]; // DMA 鍘熷缂撳啿鍖?
    uint8_t rx_app_buf[RX_BUFFER_SIZE]; // 搴旂敤灞傝鍙栫紦鍐插尯
    
    volatile uint16_t last_ndtr;    // 涓婃 NDTR (鐢ㄤ簬璋冭瘯鎴栧鏉傞€昏緫)
    volatile uint16_t data_len;     // 褰撳墠寰呭鐞嗘暟鎹暱搴?
    volatile bool data_ready;       // 鏁版嵁灏辩华鏍囧織
    
    volatile bool is_rx_active;     // 鎺ユ敹鏄惁姝ｅ湪杩愯
    volatile uint32_t overflow_count;
} UART_Channel_t;

// 鍏ㄥ眬閫氶亾鏁扮粍
static UART_Channel_t uart_channels[UART_DRIVER_COUNT];

 void UART_StartReceive_DMA(UART_Channel_t *ch);
/* 鎺ユ敹缂撳啿鍖?(鐢ㄤ簬DMA) */
 /**
 * @brief 涓撶敤浜?TMC2209 鐨勫彂閫佸嚱鏁?(闃诲寮?
 * @note 浣跨敤 HAL_UART_Transmit 纭繚鏁版嵁绔嬪嵆鍙戦€佸畬姣曪紝閬垮厤 DMA 绔炰簤
 */
UART_Status_t UART_TMC_Send(Uart_Id_t id, uint8_t *data, uint16_t size) {
	
    if ((int)id >= UART_DRIVER_COUNT || data == NULL || size == 0) return UART_ERROR_PARAM;
    
    //  鑾峰彇閫氶亾鎸囬拡 (淇锛氬畾涔?ch 鎸囬拡)
    UART_Channel_t *ch = &uart_channels[id];
    UART_HandleTypeDef *huart = ch->huart;

    // 娉ㄦ剰锛歍MC2209 鍗曠嚎閫氫俊鏃讹紝鍙戦€佹湡闂存渶濂芥殏鍋滄帴鏀禗MA锛岄槻姝㈠洖鐜共鎵?
    // 浣嗗鏋滀綘鐨勭‖浠禩X/RX鏄垎寮€鐨勶紙鍏ㄥ弻宸ワ級锛屽垯涓嶉渶瑕佹殏鍋溿€?
    // 濡傛灉鏄崟绾垮崐鍙屽伐锛岄渶瑕佸湪姝ゅ HAL_UART_DMAStop(huart);

    // 鏆傚仠 DMA 鎺ユ敹
    // 鍘熷洜锛歍MC2209 鏄崟绾垮崐鍙屽伐銆傚鏋滀笉鍋滄 DMA锛屽彂閫佺殑鏁版嵁浼氳 RX 寮曡剼璇诲洖锛?
    // 瀵艰嚧 DMA 缂撳啿鍖哄～婊″彂閫佺殑鏁版嵁锛岃€屼笉鏄垜浠兂瑕佺殑鍥炲鏁版嵁銆?
    if (ch->is_rx_active) { 
        HAL_UART_DMAStop(huart);
        ch->is_rx_active = false;
    }

        // 鍒囨崲鍒板彂閫佹ā寮?
    HAL_HalfDuplex_EnableTransmitter(huart);

    // 4. 鎵ц闃诲鍙戦€?
    // 瓒呮椂鏃堕棿璁句负 10ms锛屽浜?8 瀛楄妭鐭寘瓒冲
    HAL_StatusTypeDef hal_status = HAL_UART_Transmit(huart, data, size, 10);
		for (volatile uint32_t i = 0; i < 5000; i++)
		    // 绛夊緟鍙戦€佸櫒瀹屽叏鍏抽棴锛屾€荤嚎鎭㈠楂樼數骞?
    for (volatile uint32_t i = 0; i < 5000; i++);  // 鍑犲崄寰锛屾牴鎹富棰戣皟鏁?

        // 鍒囧洖鎺ユ敹妯″紡
    HAL_HalfDuplex_EnableReceiver(huart);
		
    // 5. 鍏抽敭姝ラ锛氶噸鍚?DMA 鎺ユ敹
    // 鏃犺鍙戦€佹垚鍔熶笌鍚︼紝閮藉繀椤诲敖蹇仮澶嶆帴鏀剁姸鎬侊紝浠ヤ究鎺ユ敹 TMC 鐨勫洖澶?
    // 娉ㄦ剰锛歍MC 鍥炲鏈夊欢杩燂紝DMA 闇€瑕佸浜庣洃鍚姸鎬?
//    vTaskDelay(1); // 鐭殏绛夊緟鎬荤嚎绋冲畾锛岄伩鍏嶅彂閫佸悗绔嬪嵆鍚姩 DMA 瀵艰嚧骞叉壈
     UART_StartReceive_DMA(ch); 
    // 6. 杩斿洖缁撴灉
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
 * @brief 鍚姩 DMA 鎺ユ敹 (甯︾┖闂蹭腑鏂娴?
 * @note 姝ゅ嚱鏁板簲鍦ㄥ垵濮嬪寲鎴栨暟鎹鐞嗗畬鎴愬悗绔嬪嵆璋冪敤
 */
void UART_StartReceive_DMA(UART_Channel_t *ch)
{
//    HAL_HalfDuplex_EnableReceiver(ch->huart);
    if (ch->is_rx_active) 
    {
        // 濡傛灉宸茬粡婵€娲伙紝鍏堝己鍒跺仠姝紝闃叉鐘舵€佹贩涔?
        HAL_UART_DMAStop(ch->huart); // 鍏抽敭锛氱‘淇?DMA 鍋滄
        ch->is_rx_active = false;
    }
//		return; // 闃叉閲嶅鍚姩
		
     //娓呯┖ DMA 缂撳啿鍖?(闃叉娈嬬暀鏁版嵁瀵艰嚧璇垽闀垮害)
//    memset(ch->rx_dma_buf, 0, RX_BUFFER_SIZE);
	// 閲嶇疆鐘舵€?
//    ch->data_len = 0;
//    ch->data_ready = false;
//    ch->last_ndtr = RX_BUFFER_SIZE;

	// 娓呴櫎鍙兘娈嬬暀鐨勪腑鏂爣蹇?
		__HAL_UART_CLEAR_IDLEFLAG(ch->huart);
		__HAL_UART_CLEAR_OREFLAG(ch->huart); // 娓呴櫎婧㈠嚭鏍囧織	
		
    // 鍚姩鎺ユ敹锛屽綋鎬荤嚎绌洪棽鏃惰Е鍙戝洖璋?
    if (HAL_UARTEx_ReceiveToIdle_DMA(ch->huart, ch->rx_dma_buf, RX_BUFFER_SIZE) == HAL_OK) {
        ch->is_rx_active = true;
	
        // 纭繚绌洪棽涓柇宸插紑鍚?(CubeMX 閫氬父浼氳嚜鍔ㄥ紑鍚紝浣嗘樉寮忕‘璁ゆ洿瀹夊叏)
//        __HAL_UART_ENABLE_IT(ch->huart, UART_IT_IDLE);
    } else {
        ch->is_rx_active = false;
        // 鍙湪姝ゅ娣诲姞閿欒鏃ュ織
    }
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{

    UART_Channel_t *ch = NULL;
    // 1. 鏌ユ壘瀵瑰簲閫氶亾
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
    // 2. 鐩存帴浣跨敤 HAL 浼犺繘鏉ョ殑 Size (杩欐槸鏈€鍑嗙‘鐨?
    if (Size > 0 && Size <= RX_BUFFER_SIZE) {
        // 杩欓噷鎴戜滑鐩存帴鎿嶄綔缁撴瀯浣擄紝鎴栬€呮斁鍏ラ槦鍒?
        // 娉ㄦ剰锛氫笉瑕佸湪杩欓噷鍋氳€楁椂鎿嶄綔锛屽彧鍋氭暟鎹嫹璐濇垨鏍囪
        
        // 鍋囪浣犳湁涓€涓叏灞€缂撳啿鍖烘垨浣跨敤涓寸晫鍖?

        ch->data_len = Size;
        ch->data_ready = true;

    }	

}


/**
 * @brief 鍒濆鍖?UART 椹卞姩鍣?
 */
void UART_Driver_Init(void)
{
    // 澶栭儴寮曠敤 CubeMX 鐢熸垚鐨勫彞鏌?
    extern UART_HandleTypeDef huart1;
    extern UART_HandleTypeDef huart2;
    extern UART_HandleTypeDef huart3;
    extern UART_HandleTypeDef hlpuart1;

    extern DMA_HandleTypeDef hdma_usart1_rx; // 纭繚浣跨敤浜?RX DMA
    extern DMA_HandleTypeDef hdma_usart2_rx;
    extern DMA_HandleTypeDef hdma_usart3_rx;
//    extern DMA_HandleTypeDef hdma_lpuart1_rx;

    // 閰嶇疆 UART1
    uart_channels[UART_CH1].huart = &huart1;
    uart_channels[UART_CH1].hdmarx = &hdma_usart1_rx;
    uart_channels[UART_CH1].is_rx_active = false;
    huart1.gState = HAL_UART_STATE_READY; 
    // 閰嶇疆 UART2
    uart_channels[UART_CH2].huart = &huart2;
    uart_channels[UART_CH2].hdmarx = &hdma_usart2_rx;
    uart_channels[UART_CH2].is_rx_active = false;
    huart2.gState = HAL_UART_STATE_READY; 
    // 閰嶇疆 UART3
    uart_channels[UART_CH3].huart = &huart3;
    uart_channels[UART_CH3].hdmarx = &hdma_usart3_rx;
    uart_channels[UART_CH3].is_rx_active = false;
    huart3.gState = HAL_UART_STATE_READY; 
    //閰嶇疆 LPUART1
    uart_channels[UART_CH4].huart = &hlpuart1;
    uart_channels[UART_CH4].hdmarx = NULL;
    uart_channels[UART_CH4].is_rx_active = false;
    hlpuart1.gState = HAL_UART_STATE_READY; 



    // 鍚姩鎵€鏈夐€氶亾鐨勬帴鏀?
    for (int i = 0; i < UART_DRIVER_COUNT; i++) {
			  if (i == UART_CH3) {
         // UART3 鐢?TMC2209 涓撶敤锛屼笉鍚敤 DMA 绌洪棽鎺ユ敹
					continue;
        }
        UART_StartReceive_DMA(&uart_channels[i]);
    }
}

/**
 * @brief 閲嶅惎鎸囧畾閫氶亾鐨?DMA 鎺ユ敹锛堝厛鍋滄鍐嶅惎鍔級
 */
void UART_RestartRX(Uart_Id_t id) {
    if ((int)id >= UART_DRIVER_COUNT) return;
    UART_Channel_t *ch = &uart_channels[id];
    HAL_UART_DMAStop(ch->huart);
    ch->is_rx_active = false;
    UART_StartReceive_DMA(ch);
}

/**
 * @brief 鍙戦€佹暟鎹潡 (DMA 闈為樆濉?
 * @param id UART 閫氶亾 ID
 * @param data 鏁版嵁鎸囬拡
 * @param size 鏁版嵁闀垮害
 * @return UART_Status_t 鐘舵€佺爜
 * @note 濡傛灉杩斿洖 BUSY锛岃鏄庝笂涓€娆″彂閫佸皻鏈畬鎴愩€?
 */
UART_Status_t UART_SendData(Uart_Id_t id, uint8_t *data, uint16_t size)
{
    if ((int)id >= UART_DRIVER_COUNT || data == NULL || size == 0) 
		return UART_ERROR_PARAM;
    
    UART_Channel_t *ch = &uart_channels[id];
		
		// 妫€鏌ART鐘舵€?
    HAL_UART_StateTypeDef state = HAL_UART_GetState(ch->huart);
		
		// 绠€鍗曢樆濉炴鏌ワ紝鍚庣画鏀逛负鐢ㄩ槦鍒?
//    if (HAL_UART_GetState(ch->huart) != HAL_UART_STATE_READY) {
//        return UART_ERROR_BUSY;
//    }
    HAL_UART_StateTypeDef txState = ch->huart->gState;
    if (txState == HAL_UART_STATE_BUSY_TX || txState == HAL_UART_STATE_BUSY_TX_RX) {
        return UART_ERROR_BUSY; // 鍙湁 TX 鐪熸蹇欑殑鏃跺€欐墠鎷掔粷
    }
    if (HAL_UART_Transmit_DMA(ch->huart, data, size) != HAL_OK) {
        return UART_ERROR_TRANSMIT_FAILED;
    }
    return UART_OK;
    
//    // 濡傛灉UART姝ｅ繖锛屼娇鐢ㄩ槦鍒楁満鍒惰€屼笉鏄樆濉炵瓑寰?
//    if (state == HAL_UART_STATE_BUSY_TX || 
//        state == HAL_UART_STATE_BUSY_TX_RX) 
//			{
//        // 鏂规1锛氳繑鍥炲繖鐘舵€侊紝璁╀笂灞傚鐞?
//        return UART_ERROR_BUSY;
//        
//        // 鏂规2锛氫娇鐢ㄥ彂閫侀槦鍒楋紙鎺ㄨ崘锛?
//        //return UART_QueueSend(id, data, size);			hdma_usart1_rx		
			
//			}
    
 // 鍚姩DMA浼犺緭
//    HAL_StatusTypeDef hal_status = HAL_UART_Transmit_DMA(ch->huart, (uint8_t*)data, size);
//    
//    if (hal_status != HAL_OK) {
//        return UART_ERROR_HAL;
//    }
//    
//    // 璁剧疆浼犺緭瀹屾垚鏍囧織鎴栧洖璋?
//    ch->tx_busy = true;
//    
//    return UART_OK;
}




/**
  * @brief锛?UART_SendString 鍙戦€佸瓧绗︿覆
  * @param锛?Uart_Id_t:UART 閫氶亾 ID
  * @param锛?UART_SendData
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
    // 鍙戦€佹暟鎹?(闃诲妯″紡)
    HAL_UART_Transmit(&huart3, data, size, 100); 
}

uint8_t TMC_UART_Receive(void) {
    uint8_t data = 0;
    // 鎺ユ敹鏁版嵁 (闃诲妯″紡)
    HAL_UART_Receive(&huart3, &data, 1, 100);
    return data;
}

/**
 * @brief 椹卞姩澶勭悊鍑芥暟
 * @note 闇€鍦?FreeRTOS 浠诲姟鎴栦富寰幆涓珮棰戣皟鐢紝鐢ㄤ簬澶勭悊鎺ユ敹鍒扮殑鏁版嵁骞堕噸鍚?DMA
 */
void UART_Driver_Process(void)
{

    for (int i = 0; i < UART_DRIVER_COUNT; i++) {
        UART_Channel_t *ch = &uart_channels[i];

        if (ch->data_ready) {
            // 杩涘叆涓寸晫鍖轰繚鎶?
            taskENTER_CRITICAL();

            uint16_t current_len = ch->data_len;
            if (current_len > 0 && current_len <= RX_BUFFER_SIZE) {
                // 鎷疯礉鍒板簲鐢ㄧ紦鍐插尯锛堟敞鎰忥細涓嶆竻闄?data_ready 鏍囧織锛?
                memcpy(ch->rx_app_buf, ch->rx_dma_buf, current_len);
                // data_len 淇濈暀锛屼緵涓婂眰璇诲彇
            } else {
                // 寮傚父闀垮害锛屾斁寮冩湰娆℃暟鎹紙灏?data_ready 缃?false锛?
                ch->data_ready = false;
                ch->data_len = 0;
            }

            taskEXIT_CRITICAL();

            // 閲嶆柊鍚姩 DMA 鎺ユ敹锛堟敞鎰忥細data_ready 鍙兘浠嶄负 true锛屼絾 DMA 缂撳啿鍖哄凡琚嫹璐濓紝鍙畨鍏ㄨ鐩栵級
            if (ch->huart->gState != HAL_UART_STATE_BUSY_RX) {
                UART_StartReceive_DMA(ch);
            }
        }
    }
}

/**
 * @brief UART_GetRxCount 鑾峰彇鎺ユ敹鍒扮殑鏁版嵁闀垮害
 * @param id UART 閫氶亾 ID
 * @note 
 */

uint16_t UART_GetRxCount(Uart_Id_t id) 
{
    if ((int)id >= UART_DRIVER_COUNT) return 0;
    // 浠呭湪 data_ready 鏃舵湁鏁堬紝鍚﹀垯杩斿洖 0 鎴栧疄鏃惰绠?NDTR
    if (uart_channels[id].data_ready) {
        return uart_channels[id].data_len;
    }
    return 0;
}
/**
 * @brie UART_GetRxBuffer 鑾峰彇搴旂敤灞傜紦鍐插尯鎸囬拡
 * @param id UART 閫氶亾 ID
 * @note 
 */
const uint8_t* UART_GetRxBuffer(Uart_Id_t id) 
	{
    if ((int)id >= UART_DRIVER_COUNT) 
			return NULL;
    return uart_channels[id].rx_app_buf;
}

/**
 * @brief UART 涓柇鍥炶皟澶勭悊
 * @param  huart: UART鍙ユ焺
 * @param  Size:  鎺ユ敹鍒扮殑鏁版嵁闀垮害 (鐢?HAL_UARTEx_RxEventCallback 浼犲叆)
 * @note 鍦?stm32g4xx_it.c 鐨?HAL_UARTEx_RxEventCallback 涓皟鐢ㄦ鍑芥暟
 */
//void UART_IRQ_Handler(UART_HandleTypeDef *huart)
    //{
    //    UART_Channel_t *ch = NULL;
    //		uint16_t received_len  ; 
    //    // 鏌ユ壘瀵瑰簲閫氶亾
    //    for (int i = 0; i < UART_DRIVER_COUNT; i++) {
    //        if (uart_channels[i].huart == huart) {
    //            ch = &uart_channels[i];
    //            break;
    //        }
    //    }
    //    
    //    if (ch == NULL )return;
    //		
    //		// 2. 娓呴櫎鏍囧織浣?
    //    __HAL_UART_CLEAR_IDLEFLAG(huart);
    //		

    //    // 鍏紡锛氭帴鏀跺埌鐨勯暱搴?= 缂撳啿鍖烘€诲ぇ灏?- DMA 褰撳墠鍓╀綑璁℃暟
    //    received_len = RX_BUFFER_SIZE - __HAL_DMA_GET_COUNTER(ch->hdmarx);
    //		// 4. 瀹夊叏妫€鏌?
    //    if (received_len > 0 && received_len <= RX_BUFFER_SIZE)
    //    {
    //        // 5. 淇濆瓨鏁版嵁闀垮害
    //        ch->data_len = received_len;
    //        ch->data_ready = true;
    //        
    //        // 6. 鏍囪鎺ユ敹鍋滄锛岀瓑寰?Process 閲嶅惎
    //        ch->is_rx_active = false; 
    //    }
    //    else
    //    {
    //        // 闀垮害寮傚父锛屽彲鑳芥槸婧㈠嚭鎴栧共鎵帮紝閲嶅惎 DMA
    //        ch->is_rx_active = false;
    //        UART_StartReceive_DMA(ch);
    //    }
    //}

// 鑾峰彇婧㈠嚭璁℃暟 (鐢ㄤ簬璋冭瘯)
uint32_t UART_Get_Overflow_Count(Uart_Id_t id) {
    if ((int)id >= UART_DRIVER_COUNT) return 0;
    return uart_channels[id].overflow_count;
}

/**
 * @brief UART 閿欒鍥炶皟鍏ュ彛
 * @param huart UART 鍙ユ焺鎸囬拡
 * @note 闇€鍦?stm32g4xx_it.c 鐨?HAL_UART_ErrorCallback 涓皟鐢ㄦ鍑芥暟銆?
 *       绀轰緥:
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

    // 娓呴櫎閿欒鏍囧織 (STM32G4 绯诲垪)
    __HAL_UART_CLEAR_OREFLAG(huart);
    __HAL_UART_CLEAR_NEFLAG(huart);
    __HAL_UART_CLEAR_FEFLAG(huart);
    __HAL_UART_CLEAR_PEFLAG(huart);

    // 鍋滄褰撳墠 DMA
  //  HAL_DMA_Abort(ch->hdmarx);//涓姝ｅ湪杩涜涓殑 DMA 浼犺緭   濡傛灉 DMA 宸茬粡姝婚攣锛岃繖涓€姝ヤ細鍗′綇鎴栧け璐?
    ch->overflow_count++;
    ch->is_rx_active = false;

    UART_StartReceive_DMA(ch);
		
}


/**
 * @brief DMA浼犺緭瀹屾垚鍥炶皟鍑芥暟锛堥渶瑕佸湪HAL閰嶇疆涓缃級
 */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    // 鎵惧埌瀵瑰簲鐨勯€氶亾
    for (int i = 0; i < UART_DRIVER_COUNT; i++) {
        if (uart_channels[i].huart == huart) {
//            uart_channels[i].tx_busy = false;
            
            // 濡傛灉鏈夊彂閫侀槦鍒楋紝鍙互鍦ㄨ繖閲岃Е鍙戜笅涓€涓彂閫?
            // UART_ProcessQueue(i);
            
            // 濡傛灉浣跨敤浜嗗姩鎬佸垎閰嶇殑缂撳啿鍖猴紝鍦ㄨ繖閲岄噴鏀?
//            vPortFree(uart_channels[i].tx_buffer);
            
            break;
        }
    }
}
		
		
/**
 * @brief 鏆傚仠鎸囧畾閫氶亾鐨?DMA 鎺ユ敹锛屼负闃诲鍙戦€佸仛鍑嗗
 */
void UART_SuspendRX(Uart_Id_t id) {
    if ((int)id >= UART_DRIVER_COUNT) return;
    UART_Channel_t *ch = &uart_channels[id];
    HAL_UART_DMAStop(ch->huart);
    HAL_UART_Abort(ch->huart);   // 褰诲簳閲婃斁璧勬簮
    ch->is_rx_active = false;
}

/**
 * @brief 鎭㈠鎸囧畾閫氶亾鐨?DMA 绌洪棽涓柇鎺ユ敹
 */
 
void UART_ResumeRX(Uart_Id_t id) {
    if ((int)id >= UART_DRIVER_COUNT) return;
    UART_Channel_t *ch = &uart_channels[id];
    UART_StartReceive_DMA(ch);    // 鍐呴儴 static 浣嗚繖閲屽彲浠ヨ皟鐢?
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