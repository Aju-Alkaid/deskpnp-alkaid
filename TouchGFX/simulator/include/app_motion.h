#ifndef TOUCHGFX_SIM_APP_MOTION_H
#define TOUCHGFX_SIM_APP_MOTION_H

#ifdef SIMULATOR

#include <stdint.h>
#include <stdbool.h>
#include "touchgfx_sim_os_shim.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef R_AXIS_ADDR
#define R_AXIS_ADDR 0x04
#endif

typedef struct {
    int32_t x;
    int32_t y;
    float   r;
    float   z;
    bool    homed;
    bool    valid;
} MachineCoord_t;

static inline void Coord_Init(void) {}
static inline MachineCoord_t Coord_Get(void) { MachineCoord_t c = {0,0,0.0f,0.0f,false,false}; return c; }
static inline void Coord_SetHome(void) {}
static inline void Coord_UpdateXY(int32_t x, int32_t y) { (void)x; (void)y; }
static inline void Coord_UpdateR(float angle) { (void)angle; }
static inline void Coord_UpdateZ(float angle) { (void)angle; }
static inline void Coord_Invalidate(void) {}

typedef enum {
    MOTION_CMD_MOVE_TO,
    MOTION_CMD_HOME,
    MOTION_CMD_STOP,
    MOTION_CMD_DISABLE,
    MOTION_CMD_Z_DOWN,
    MOTION_CMD_Z_UP,
    MOTION_CMD_PICK,
    MOTION_CMD_PLACE,
    MOTION_CMD_R_ROTATE,
    MOTION_CMD_WAIT
} MotionCmdType_t;

#ifndef EVENT_X1_DONE
#define EVENT_X1_DONE    (1 << 0)
#endif
#ifndef EVENT_X2_DONE
#define EVENT_X2_DONE    (1 << 1)
#endif
#ifndef EVENT_Y_DONE
#define EVENT_Y_DONE     (1 << 2)
#endif
#ifndef EVENT_ALL_AXES
#define EVENT_ALL_AXES   (EVENT_X1_DONE | EVENT_X2_DONE | EVENT_Y_DONE)
#endif
#ifndef EVENT_ANY_ERROR
#define EVENT_ANY_ERROR  (1 << 3)
#endif

typedef struct {
    MotionCmdType_t cmd_type;
    int32_t target_x;
    int32_t target_y;
    int32_t target_r;
    int32_t param2;
    uint16_t speed;
    uint8_t  acc;
} MotionCmd_t;

typedef enum {
    MOTOR_OK = 0,
    MOTOR_ERR_TIMEOUT,
    MOTOR_ERR_LIMIT
} MotorError_t;

extern osMessageQueueId_t motion_cmd_queue;
extern osMessageQueueId_t motor_event_queue;
extern volatile bool g_motor_error;
extern volatile MotorError_t g_motor_error_detail;
extern volatile bool s_cmd_interrupted;

static inline void Event_Init(void) {}
static inline void nozzle_on(void) {}
static inline void nozzle_off(void) {}
static inline void z_down(void) {}
static inline void z_up(void) {}
static inline void z_safe(void) {}
static inline void z_pick(void) {}
static inline void z_place(void) {}
static inline bool pick_component(void) { return false; }
static inline void place_component(void) {}
static inline bool vacuum_ok(void) { return false; }
static inline void r_axis_set_zero(void) {}
static inline int  r_axis_rotate(float angle, float speed_rpm) { (void)angle; (void)speed_rpm; return 0; }
static inline int  safe_move_to(int32_t target_x, int32_t target_y, uint16_t speed, uint8_t acc) { (void)target_x; (void)target_y; (void)speed; (void)acc; return 0; }
static inline void axis_stop(int32_t addr) { (void)addr; }
static inline void disable_sync_stop(void) {}

#ifdef __cplusplus
}
#endif

#endif /* SIMULATOR */
#endif /* TOUCHGFX_SIM_APP_MOTION_H */
