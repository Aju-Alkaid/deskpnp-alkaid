#ifndef TOUCHGFX_SIM_DRIVER_MOTOR_H
#define TOUCHGFX_SIM_DRIVER_MOTOR_H

#ifdef SIMULATOR

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CR_OPEN
#define CR_OPEN   0x00
#endif
#ifndef CR_CLOSE
#define CR_CLOSE  0x01
#endif
#ifndef CR_vFOC
#define CR_vFOC   0x02
#endif
#ifndef SR_OPEN
#define SR_OPEN   0x03
#endif
#ifndef SR_CLOSE
#define SR_CLOSE  0x04
#endif
#ifndef SR_vFOC
#define SR_vFOC   0x05
#endif

#ifndef MOTOR_X1_ID
#define MOTOR_X1_ID  1
#endif
#ifndef MOTOR_X2_ID
#define MOTOR_X2_ID  2
#endif
#ifndef MOTOR_Y_ID
#define MOTOR_Y_ID   3
#endif

typedef enum {
    MOTOR_STATE_IDLE = 0,
    MOTOR_STATE_SENDING = 1,
    MOTOR_STATE_WAITING = 2,
    MOTOR_STATE_COMPLETE = 3,
    MOTOR_STATE_ERROR = 4
} MotorState_t;

static inline void readRealTimeSpeed(uint16_t slaveAddr, uint16_t ID) { (void)slaveAddr; (void)ID; }
static inline uint8_t waitingForACK(uint32_t delayTime, uint8_t expectFunc, uint8_t expectStatus) { (void)delayTime; (void)expectFunc; (void)expectStatus; return 0; }
static inline void runFail(void) {}
static inline void runOK(void) {}
static inline void NVIC_INIT(void) {}
static inline void speedModeRun(uint8_t slaveAddr, uint8_t dir, uint16_t speed, uint8_t acc) { (void)slaveAddr; (void)dir; (void)speed; (void)acc; }
static inline void readRealTimeLocation(uint8_t slaveAddr) { (void)slaveAddr; }
static inline void setWorkMStep(uint8_t slaveAddr, uint8_t MStep) { (void)slaveAddr; (void)MStep; }
static inline void setIWorkMode(uint8_t slaveAddr, uint16_t Ma) { (void)slaveAddr; (void)Ma; }
static inline void positionMode2Run(uint8_t slaveAddr, uint16_t speed, uint8_t acc, int32_t relAxis) { (void)slaveAddr; (void)speed; (void)acc; (void)relAxis; }
static inline void positionMode3Run(uint8_t slaveAddr, uint16_t speed, uint16_t acc, int32_t absAxis) { (void)slaveAddr; (void)speed; (void)acc; (void)absAxis; }
static inline void motorEnable(uint8_t slaveAddr, uint8_t enable) { (void)slaveAddr; (void)enable; }
static inline void motorSetArrivalThreshold(uint8_t slaveAddr) { (void)slaveAddr; }
static inline void motorSyncEnable(uint8_t enable) { (void)enable; }
static inline void motorSetZero(uint8_t slaveAddr) { (void)slaveAddr; }
static inline void setWorkMode(uint8_t slaveAddr, uint8_t Mode) { (void)slaveAddr; (void)Mode; }
static inline void Motor_Init(void) {}
static inline void motorSyncTrigger(uint8_t slaveAddr) { (void)slaveAddr; }

#ifdef __cplusplus
}
#endif

#endif /* SIMULATOR */
#endif /* TOUCHGFX_SIM_DRIVER_MOTOR_H */
