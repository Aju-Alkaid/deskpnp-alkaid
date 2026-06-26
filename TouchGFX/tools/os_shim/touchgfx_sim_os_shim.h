#ifndef TOUCHGFX_SIM_OS_SHIM_H
#define TOUCHGFX_SIM_OS_SHIM_H

#ifdef SIMULATOR

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void* osMessageQueueId_t;

#ifndef osOK
#define osOK 0
#endif

osMessageQueueId_t osMessageQueueNew(uint32_t msg_count, uint32_t msg_size, const void *attr);
int32_t osMessageQueuePut(osMessageQueueId_t mq_id, const void *msg_ptr, uint8_t msg_prio, uint32_t timeout);
int32_t osMessageQueueGet(osMessageQueueId_t mq_id, void *msg_ptr, uint8_t *msg_prio, uint32_t timeout);
uint32_t osMessageQueueGetCount(osMessageQueueId_t mq_id);
uint32_t osMessageQueueGetSpace(osMessageQueueId_t mq_id);

#ifdef __cplusplus
}
#endif

#endif /* SIMULATOR */
#endif /* TOUCHGFX_SIM_OS_SHIM_H */
