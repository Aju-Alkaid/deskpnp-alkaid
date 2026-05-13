#ifndef __DRIVER_TMC2209_H
#define __DRIVER_TMC2209_H

#include "driver_uart.h"
#include <stdint.h>
#include <stdbool.h>
#include "FreeRTOS.h"
#include "semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 用户配置区 ========== */
#define TMC_UART_ID           UART_CH3    // 使用的 UART 通道（huart3）
#define TMC_EN_PORT           GPIOD
#define TMC2_EN_PIN           GPIO_PIN_14
#define TMC1_EN_PIN           GPIO_PIN_15

#define TMC2209_RSENSE_MOHM   110         // 检流电阻 mΩ
#define TMC2209_VSENSE        0           // 0: 低灵敏度(0.325V), 1: 高灵敏度(0.180V)
#define TMC2209_MICROSTEPS    256         // 微步分辨率
#define TMC2209_F_CLK         12000000UL  // 内部时钟 12MHz

/* 电流参数 */
#define TMC2209_MOTOR_HOLD_CURRENT    200   // mA
#define TMC2209_MOTOR_RUN_CURRENT     800   // mA

/* ========== 寄存器地址 ========== */
typedef enum {
    TMC_REG_GCONF         = 0x00,
    TMC_REG_GSTAT         = 0x01,
    TMC_REG_IFCNT         = 0x02,
    TMC_REG_SLAVECONF     = 0x03,
    TMC_REG_OTP_PROG      = 0x04,
    TMC_REG_OTP_READ      = 0x05,
    TMC_REG_IOIN          = 0x06,
    TMC_REG_FACTORY_CONF  = 0x07,
    TMC_REG_IHOLD_IRUN    = 0x10,
    TMC_REG_TPOWERDOWN    = 0x11,
    TMC_REG_TSTEP         = 0x12,
    TMC_REG_TPWMTHRS      = 0x13,
    TMC_REG_VACTUAL       = 0x22,
    TMC_REG_SGTHRS        = 0x40,
    TMC_REG_SG_RESULT     = 0x41,
    TMC_REG_COOLCONF      = 0x42,
    TMC_REG_MSCNT         = 0x6A,
    TMC_REG_MSCURACT      = 0x6B,
    TMC_REG_CHOPCONF      = 0x6C,
    TMC_REG_DRV_STATUS    = 0x6F,
    TMC_REG_PWMCONF       = 0x70,
    TMC_REG_PWM_SCALE     = 0x71,
    TMC_REG_PWM_AUTO      = 0x72
} TMC_RegAddr_t;

/* ========== 位定义 ========== */
/* GCONF */
#define GCONF_I_SCALE_ANALOG   (1<<0)
#define GCONF_INTERNAL_RSENSE  (1<<1)
#define GCONF_EN_SPREADCYCLE   (1<<2)
#define GCONF_SHAFT            (1<<3)
#define GCONF_INDEX_OTPW       (1<<4)
#define GCONF_INDEX_STEP       (1<<5)
#define GCONF_PDN_DISABLE      (1<<6)
#define GCONF_MSTEP_REG_SELECT (1<<7)
#define GCONF_MULTISTEP_FILT   (1<<8)

/* CHOPCONF */
#define CHOPCONF_DISS2VS       (1<<31)
#define CHOPCONF_DISS2G        (1<<30)
#define CHOPCONF_DEDGE         (1<<29)
#define CHOPCONF_INTPOL        (1<<28)
#define CHOPCONF_VSENSE        (1<<17)
#define CHOPCONF_TBL_SHIFT     16
#define CHOPCONF_HEND_SHIFT    7
#define CHOPCONF_HSTRT_SHIFT   4
#define CHOPCONF_TOFF_SHIFT    0

/* 通信常量 */
#define TMC_SYNC_BYTE          0x05
#define TMC_SLAVE_ADDR         0x00        // MS1=0, MS2=0
#define TMC_WRITE_FLAG         0x80
#define TMC_READ_REPLY_ADDR    0xFF
#define TMC_DATAGRAM_LEN       8
#define TMC_READ_REQUEST_LEN   4

/* 超时/重试 */
#define TMC_UART_TIMEOUT       50          // ms
#define TMC_MUTEX_WAIT_TIME    100         // ms

/* ========== 错误码 ========== */
typedef enum {
    TMC_ERR_NONE = 0,
    TMC_ERR_CRC_FAIL,
    TMC_ERR_SYNC_FAIL,
    TMC_ERR_TIMEOUT,
    TMC_ERR_BUSY,
    TMC_ERR_INVALID_PARAM
} TMC_Error_t;

/* ========== API 声明 ========== */
bool            TMC_Init(void);
TMC_Error_t     TMC_WriteReg(uint8_t reg_addr, uint32_t data);
TMC_Error_t     TMC_ReadReg(uint8_t reg_addr, uint32_t *value);
void            TMC_SetEnable(bool enable);
void            TMC_SetCurrent_mA(uint16_t current_mA);
void            TMC_SetCurrent_Raw(uint8_t run_current, uint8_t hold_current, uint8_t hold_delay);
void            TMC_SetMicrosteps(uint16_t steps);
void            TMC_SetChopperMode(bool use_spreadcycle);
void            TMC_SetSpeed(int32_t velocity);
TMC_Error_t     TMC_GetStatus(uint8_t *status);
TMC_Error_t     TMC_GetSGResult(uint16_t *sg_value);

#ifdef __cplusplus
}
#endif

#endif /* __DRIVER_TMC2209_H */