#ifndef __APP_VISION_H
#define __APP_VISION_H

#include <stdint.h>
#include <stdbool.h>

/* Mark point count (for app_host.c) */
#define P2_MARK_COUNT  3

/* P1 batch result cap: keep in sync with camera max_det; 32 covers dense feeder views */
#define P1_MAX_TARGETS 32

/*
 * Vision module external states (MaixCAM 2026 v2 protocol)
 */

/* External states */
typedef enum {
    VISION_IDLE = 0,
    VISION_BUSY,
    VISION_RDY,          /* Cam responded rdy, host should send go */
    VISION_GOT_STOP,
    VISION_GOT_POS,
    VISION_DONE,
    VISION_ERROR,
    VISION_GOT_ERR_RETRY,
    VISION_GOT_CATEGORY_QUERY,/* P1: Cam asked for category, host must call Vision_ClsReply() */
    VISION_GOT_MOVE,          /* P1: cam requests host to move to next view */
} VisionState_t;

/* Command types */
typedef enum {
    VCMD_P1,
    VCMD_P2,
    VCMD_P3,
    VCMD_P4,             /* P4: 下相机圆形标定对位（吸嘴中心） */
} VisionCmd_t;

/* P1 component class mapping (MaixCAM2 YOLO 3 classes: ccap/cled/cres, 2026-08-02) */
#define P1_CLASS_CCAP    0
#define P1_CLASS_CLED    1
#define P1_CLASS_CRES    2

/* Single visual target result */
typedef struct {
    int32_t dx;
    int32_t dy;
    int32_t angle_x100;
    bool    angle_valid;
    int32_t class_id;
} VisionTarget_t;

/* Single Process result data */
typedef struct {
    VisionTarget_t targets[P1_MAX_TARGETS];
    int32_t  target_count;         /* P1 batch: number of targets reported by cam */
    bool     p1_batch_mode;        /* P1 single-shot batch: do not iterate with go */
    int32_t dx, dy;
    int32_t angle_x100;
    bool    angle_valid;
    int32_t class_id;
    char    class_name[8];
    int32_t mark_index;
    int32_t mark_count;
} VisionResult_t;

/* ---- New protocol API ---- */

void Vision_Init(void);

/* P0 startup handshake: send p0, wait for rdy. Returns false on timeout. */
bool Vision_Handshake(uint32_t timeout_ms);

/* Start Process (non-blocking), state becomes VISION_BUSY */
/* class_id: component class for P1 (0~2), ignored for P2/P3 */
void Vision_Start(VisionCmd_t cmd, int class_id);

/* Send "go" (after host stops/moves), state becomes VISION_BUSY */
void Vision_Go(void);

/* P1 scan-start "go": after category/mv, cam starts Phase0 but still waits stp */
void Vision_GoScan(void);

/* P1 category reply: send cls + N:{id} + end (task context only) */
void Vision_ClsReply(void);

/* Query current state */
VisionState_t Vision_GetState(void);

/* Get result (valid when VISION_DONE / VISION_GOT_POS) */
const VisionResult_t* Vision_GetResult(void);

/* Get error code string (valid when VISION_ERROR) */
const char* Vision_GetError(void);

/* Convert P1 class ID to string */
const char* Vision_ClassName(int class_id);

/* UART callback: feed bytes (ISR path, no blocking ops) */
void CamUart_RecvCallback(uint8_t *data, int len);

/* Timeout protection (30s default) */
bool Vision_IsTimedOut(void);
void Vision_ResetTimeout(void);
void Vision_ForceIdle(void);
void Vision_SendEnd(void);

/* Transition back to search mode without sending commands (pos-detect timeout recovery) */
void Vision_BackToSearch(void);
uint32_t Vision_GetAlignRxCount(void);
int Vision_GetGotPosFromISR(void);
uint32_t Vision_GetP2TotalRxCount(void);
uint32_t Vision_GetP2StpIgnoredCount(void);
uint32_t Vision_GetFrameCrcErrors(void);

#endif
