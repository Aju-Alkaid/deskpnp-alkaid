#include "driver_spiflash_w25q64.h"
//#include "driver_lcd.h"
#include "driver_timer.h"
#include "stm32g4xx_hal.h"
#include <string.h>
#include "app_config.h"
#include "app_test.h"
//#include "tim.h"
//#include "adc.h"

extern SPI_HandleTypeDef hspi3;
static SPI_HandleTypeDef *g_HSPI_Flash = &hspi3;

static void flash_lock(void) {
    if (w25q64_mutex != NULL) {
        osMutexAcquire(w25q64_mutex, osWaitForever);
    }
}

static void flash_unlock(void) {
    if (w25q64_mutex != NULL) {
        osMutexRelease(w25q64_mutex);
    }
}

/* CS: PA15
 * SPI3_MISO: PC11
 * SPI3_MOSI: PC12
 * SPI3_SCK:  PC10
 */
 
#define W25Q64_CS_GPIO_GROUP GPIOA
#define W25Q64_CS_GPIO_PIN   GPIO_PIN_15
#define W25Q64_TIMEOUT       500


/**
 * 函数名称： W25Q64_Select
 * 功能描述： 选中W25Q64
 * 输入参数： 无
 * 输出参数： 无
 * 返 回 值： 无
*/
static void W25Q64_Select(void)
{
    HAL_GPIO_WritePin(W25Q64_CS_GPIO_GROUP, W25Q64_CS_GPIO_PIN, GPIO_PIN_RESET);
}

/**
 * 函数名称： W25Q64_Deselect
 * 功能描述： 不选中W25Q64
 * 输入参数： 无
 * 输出参数： 无
 * 返 回 值： 无
**/
static void W25Q64_Deselect(void)
{
    HAL_GPIO_WritePin(W25Q64_CS_GPIO_GROUP, W25Q64_CS_GPIO_PIN, GPIO_PIN_SET);
}

/**
 * 函数名称： W25Q64_TxRx
 * 功能描述： 使用SPI发送/接收数据(注意这个函数没有设置片选信号)
 * 输入参数： pTxData - 要发送的数据
 *            Size    - 数据长度
 *            Timeout - 超时时间(单位ms)
 * 输出参数： pRxData - 接收缓冲区
 * 返 回 值： 0 - 成功, (-1)-失败
**/
static int  W25Q64_TxRx(uint8_t *pTxData, uint8_t *pRxData, uint16_t Size, uint32_t Timeout)
{
    if (HAL_OK == HAL_SPI_TransmitReceive(g_HSPI_Flash, pTxData, pRxData, Size, Timeout))
        return 0;
    else
        return -1;
}

/**
 * 函数名称： W25Q64_Tx
 * 功能描述： 使用SPI发送数据(注意这个函数没有设置片选信号)
 * 输入参数： pTxData - 要发送的数据
 *            Size    - 数据长度
 *            Timeout - 超时时间(单位ms)
 * 输出参数： 无
 * 返 回 值： 0 - 成功, (-1)-失败
**/
static int  W25Q64_Tx(uint8_t *pTxData, uint16_t Size, uint32_t Timeout)
{
    if (HAL_OK == HAL_SPI_Transmit(g_HSPI_Flash, pTxData, Size, Timeout))
        return 0;
    else
        return -1;
}

/**
 * 函数名称： W25Q64_Rx
 * 功能描述： 使用SPI读取数据(注意这个函数没有设置片选信号)
 * 输入参数： Size    - 数据长度
 *            Timeout - 超时时间(单位ms)
 * 输出参数： pRxData - 接收缓冲区
 * 返 回 值： 0 - 成功, (-1)-失败
**/
static int  W25Q64_Rx(uint8_t *pRxData, uint16_t Size, uint32_t Timeout)
{
    if (HAL_OK == HAL_SPI_Receive(g_HSPI_Flash, pRxData, Size, Timeout))
        return 0;
    else
        return -1;
}


/**
 * 函数名称： W25Q64_WaitReady
 * 功能描述： 等待W25Q64就绪
 * 输入参数： 无
 * 输出参数： 无
 * 返 回 值： 0 - 成功, (-1)-失败
**/
static int W25Q64_WaitReady(void)
{
    unsigned char tx_buf[2];
    unsigned char rx_buf[2];
    int i;

    tx_buf[0] = 0x05; /* 读状态 */
    tx_buf[1] = 0xff;
    
    for (i = 0; i < W25Q64_TIMEOUT; i++)
    {
        rx_buf[0] = rx_buf[1] = 0;
        W25Q64_Select();
        W25Q64_TxRx(tx_buf, rx_buf, 2, W25Q64_TIMEOUT);
        W25Q64_Deselect();
        if ((rx_buf[1] & 1) == 0)
            return 0;
        mdelay(1);
    }

    return -1;  /* timeout */
}

/**
 * 函数名称： W25Q64_WriteEnable
 * 功能描述： 写使能
 * 输入参数： 无
 * 输出参数： 无
 * 返 回 值： 0 - 成功, (-1)-失败
**/
static int W25Q64_WriteEnable(void)
{
    unsigned char tmpbuf[1];
    int err;
    
    tmpbuf[0] = 0x06; /* 写使能 */
    W25Q64_Select();
    err = W25Q64_Tx(tmpbuf, 1, W25Q64_TIMEOUT);
    W25Q64_Deselect();

    return err;
}


/**
 * 函数名称： W25Q64_Init
 * 功能描述： W25Q64的初始化函数
 * 输入参数： 无
 * 输出参数： 无
 * 返 回 值： 无
**/
void W25Q64_Init(void)
{
    /* 片选信号PA15在MX_GPIO_Init中被配置为输出引脚 */
    /* SPI在MX_SPI3_Init中也被配置好了 */
}

/**
 * 函数名称： W25Q64_Read
 * 功能描述： W25Q64读函数
 * 输入参数： offset - 读哪个地址
 *            len    - 读多少字节
 * 输出参数： buf - 用来保存数据
 * 返 回 值： 非负数 - 读取了多少字节的数据, (-1) - 失败
**/
static int W25Q64_ReadInternal(uint32_t offset, uint8_t *buf, uint32_t len)
{
    unsigned char tmpbuf[4];
    int err;
    
    /* 自己实现SPI Flash的读操作 */
    tmpbuf[0] = 0x03;
    tmpbuf[1] = (offset >> 16) & 0xff;
    tmpbuf[2] = (offset >> 8) & 0xff;
    tmpbuf[3] = (offset >> 0) & 0xff;
    
    W25Q64_Select();

    /* 发送读命令 */
    err = W25Q64_Tx(tmpbuf, 4, W25Q64_TIMEOUT);
    if (err)
    {
        W25Q64_Deselect();
        return -1;
    }

    /* 读数据 */
    err = W25Q64_Rx(buf, len, W25Q64_TIMEOUT);
    if (err)
    {
        W25Q64_Deselect();
        return -1;
    }

    W25Q64_Deselect();    
    return len;
}


/**
 * 函数名称： W25Q64_Write
 * 功能描述： W25Q64写函数(需要先擦除)
 * 输入参数： offset - 写哪个地址
 *            buf    - 数据buffer
 *            len    - 写多少字节
 * 输出参数： 无
 * 返 回 值： 非负数 - 写了多少字节的数据, (-1) - 失败
**/
static int W25Q64_WriteInternal(uint32_t offset, uint8_t *buf, uint32_t len)
{
    uint8_t tmpbuf[4];
    uint32_t phy_pos = offset;
    int err;
    uint32_t cur_len;
    uint32_t remain_len;

    /* 写数据 */
    phy_pos = offset;
    remain_len = len;

    /* 可能不是从Page开头写数据(1个Page是256字节) */
    cur_len = offset & (256-1);
    cur_len = 256 - cur_len;
    if (cur_len > len)
        cur_len = len;
    
    for (; phy_pos < offset + len; )
    {
        /* 写使能 */
        err = W25Q64_WriteEnable();
        if (err)
        {
            return -1;
        }
        
        tmpbuf[0] = 0x02; /* page program */
        tmpbuf[1] = (phy_pos >> 16) & 0xff;
        tmpbuf[2] = (phy_pos >> 8) & 0xff;
        tmpbuf[3] = (phy_pos >> 0) & 0xff;

        /* 发送page program命令 */
        W25Q64_Select();
        err = W25Q64_Tx(tmpbuf, 4, W25Q64_TIMEOUT);
        if (err)
        {
            W25Q64_Deselect();
            return -1;
        }
        
        /* 发送数据 */
        err = W25Q64_Tx(buf, cur_len, W25Q64_TIMEOUT);
        if (err)
        {
            W25Q64_Deselect();
            return -1;
        }
        W25Q64_Deselect();
        
        /* 读状态 */
        err = W25Q64_WaitReady();
        if (err)
        {
            return -1;
        }

        phy_pos += cur_len;
        buf     += cur_len;
        remain_len -= cur_len;

        cur_len = (remain_len < 256) ? remain_len : 256;
    }
    return len;
}

/**
 * 函数名称： W25Q64_Erase
 * 功能描述： W25Q64擦除函数
 * 输入参数： offset - 擦除哪个地址(4096对齐)
 *            len    - 擦除多少字节(4096对齐)
 * 输出参数： 无
 * 返 回 值： 非负数 - 擦除了多少字节的数据, (-1) - 失败
**/
static int W25Q64_EraseInternal(uint32_t offset, uint32_t len)
{
    unsigned char tmpbuf[4];
    uint32_t phy_pos = offset;
    int err;

    if ((offset & (4096-1)) || (len & (4096-1)))
        return -1;

    for (int sector = 0; sector < len/4096; sector++)
    {
        /* 写使能 */
        err = W25Q64_WriteEnable();
        if (err)
        {
            return -1;
        }
        
        tmpbuf[0] = 0x20; /* 擦除 */
        tmpbuf[1] = (phy_pos >> 16) & 0xff;
        tmpbuf[2] = (phy_pos >> 8) & 0xff;
        tmpbuf[3] = (phy_pos >> 0) & 0xff;
        
        W25Q64_Select();
        err = W25Q64_Tx(tmpbuf, 4, W25Q64_TIMEOUT);
        if (err)
        {
            W25Q64_Deselect();
            return -1;
        }
        W25Q64_Deselect();
        
        phy_pos += 4096;

        /* 读状态 */
        err = W25Q64_WaitReady();
        if (err)
        {
            return -1;
        }
    }

    return len;
}


/**
 * 函数名称： W25Q64_Test
 * 功能描述： W25Q64测试程序
 * 输入参数： 无
 * 输出参数： 无
 *            无
 * 返 回 值： 无
**/

//void W25Q64_Test(void)
//{
//    int sector;
//    int len;
//    uint8_t buf[4];
//    int i;
//    uint32_t val1, val2;
//    
//    W25Q64_Init();

//    while (1)
//    {
//        LCD_PrintString(0, 0, "W25Q64 Test: ");

//        for (sector = 0; sector < 2048; sector++)
//        {
//            /* 擦除测试 */
//            LCD_ClearLine(0, 2);
//            len = LCD_PrintString(0, 2, "Erase ");
//            LCD_PrintSignedVal(len, 2, sector);
//            W25Q64_Erase(sector * 4096, 4096);            
//            W25Q64_Read(sector * 4096, buf, 4);
//            LCD_ClearLine(0, 4);
//            for (i = 0; i < 4; i++)
//            {
//                LCD_PrintHex(i*3, 4, buf[i], 0);
//            }
//            mdelay(1000);

//            /* 写入测试 */
//            LCD_ClearLine(0, 2);
//            len = LCD_PrintString(0, 2, "Write ");
//            LCD_PrintSignedVal(len, 2, sector);

//            val1 = system_get_ns();
//            W25Q64_Write(sector * 4096, (uint8_t *)&val1, 4);
//            LCD_ClearLine(0, 4);
//            LCD_PrintHex(0, 4, val1, 1);
//            mdelay(1000);

//            /* 读出测试 */
//            LCD_ClearLine(0, 2);
//            len = LCD_PrintString(0, 2, "Read ");
//            LCD_PrintSignedVal(len, 2, sector);

//            W25Q64_Read(sector * 4096, (uint8_t *)&val2, 4);
//            LCD_ClearLine(0, 4);
//            if (val1 == val2)
//                LCD_PrintString(0, 4, "Test ok");
//            else
//            {
//                LCD_PrintHex(0, 4, val2, 1);
//                LCD_ClearLine(0, 6);
//                LCD_PrintString(0, 6, "Test Failed");
//            }
//            mdelay(1000);
//            
//        }
//    }
//}


/* ================================================================
 *  标定数据 Flash 持久化 (Calib_Save / Calib_Load)
 *  写入 W25Q64 最后一个 4KB 扇区 (CALIB_FLASH_ADDR)。
 * ================================================================ */

/* 前向声明 */
static void calib_set_defaults(CalibrationData_t *calib);

/*
 * 软件 CRC32 (Ethernet polynomial 0x04C11DB7, 与 STM32 硬件 CRC 一致)
 */
static uint32_t calib_crc32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFF;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320;
            else
                crc >>= 1;
        }
    }
    return ~crc;
}

/*
 * 将标定数据写入 Flash。
 * 流程: 计算 CRC → 擦除扇区 → 写入
 * 返回: 0 成功, -1 失败
 */
int Calib_Save(const CalibrationData_t *calib)
{
    CalibrationData_t buf;
    int ret;

    /* 复制并填入 CRC */
    memcpy(&buf, calib, sizeof(CalibrationData_t));
    buf.magic = CALIB_MAGIC;
    buf.crc32 = calib_crc32((const uint8_t *)&buf,
                            offsetof(CalibrationData_t, crc32));

    /* 擦除扇区 (4KB 对齐) */
    ret = W25Q64_Erase(CALIB_FLASH_ADDR, CALIB_FLASH_SIZE);
    if (ret < 0) return -1;

    /* 写入 */
    ret = W25Q64_Write(CALIB_FLASH_ADDR, (uint8_t *)&buf, sizeof(buf));
    if (ret < 0) return -1;

    /* 诊断：写后读回比对 */
    {
        CalibrationData_t verify;
        if (W25Q64_Read(CALIB_FLASH_ADDR, (uint8_t *)&verify, sizeof(verify)) >= 0) {
            if (memcmp(&buf, &verify, sizeof(buf)) != 0) {
                PrintDebug("[CALIB] WRITE VERIFY FAILED!\r\n");
                uint8_t *a = (uint8_t *)&buf, *b = (uint8_t *)&verify;
                for (int i = 0; i < (int)sizeof(buf); i++) {
                    if (a[i] != b[i])
                        PrintDebug("  off=%d: wrote 0x%02X, read 0x%02X\r\n", i, a[i], b[i]);
                }
            } else {
                PrintDebug("[CALIB] Write verify OK.\r\n");
            }
        }
    }

    return 0;
}

/*
 * 从 Flash 加载标定数据。
 * 若 magic 不匹配或 CRC 校验失败，填充默认值。
 * 返回: 0 加载成功 (含默认值), -1 Flash 读失败
 */
int Calib_Load(CalibrationData_t *calib)
{
    int ret;

    ret = W25Q64_Read(CALIB_FLASH_ADDR, (uint8_t *)calib,
                      sizeof(CalibrationData_t));
    if (ret < 0) {
        calib_set_defaults(calib);
        return -1;
    }

    /* 校验 magic */
    if (calib->magic != CALIB_MAGIC) {
        PrintDebug("[CALIB] Magic mismatch: 0x%08lX\r\n", (unsigned long)calib->magic);
        calib_set_defaults(calib);
        return 0;
    }

    /* 校验 CRC32 */
    uint32_t expected = calib->crc32;
    uint32_t computed = calib_crc32((const uint8_t *)calib,
                                    offsetof(CalibrationData_t, crc32));
    if (expected != computed) {
        PrintDebug("[CALIB] CRC mismatch: exp=0x%08lX got=0x%08lX\r\n",
                   (unsigned long)expected, (unsigned long)computed);
        calib_set_defaults(calib);
        return 0;
    }

    return 0;
}

/*
 * 将标定数据填充为安全默认值:
 *   坐标全部为 0 (需手动标定)
 *   Z 角度使用代码中的硬编码默认值
 *   摄像头比例使用预估值
 */
static void calib_set_defaults(CalibrationData_t *calib)
{
    memset(calib, 0, sizeof(CalibrationData_t));
    calib->z_safe_angle  = CALIB_DEFAULT_Z_SAFE;
    calib->z_pick_angle  = CALIB_DEFAULT_Z_PICK;
    calib->z_place_angle = CALIB_DEFAULT_Z_PLACE;
		
}
int W25Q64_Read(uint32_t offset, uint8_t *buf, uint32_t len)
{
    int ret;
    flash_lock();
    ret = W25Q64_ReadInternal(offset, buf, len);
    flash_unlock();
    return ret;
}

int W25Q64_Write(uint32_t offset, uint8_t *buf, uint32_t len)
{
    int ret;
    flash_lock();
    ret = W25Q64_WriteInternal(offset, buf, len);
    flash_unlock();
    return ret;
}

int W25Q64_Erase(uint32_t offset, uint32_t len)
{
    int ret;
    flash_lock();
    ret = W25Q64_EraseInternal(offset, len);
    flash_unlock();
    return ret;
}
