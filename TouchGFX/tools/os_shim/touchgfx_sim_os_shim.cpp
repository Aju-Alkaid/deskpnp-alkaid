#ifdef SIMULATOR

#include "touchgfx_sim_os_shim.h"
#include <cstdlib>
#include <cstring>
#include <vector>

struct SimQueue {
    std::vector<uint8_t> storage;
    std::vector<uint8_t> used;
    uint32_t capacity = 0;
    uint32_t itemSize = 0;
    uint32_t head = 0;
    uint32_t tail = 0;
    uint32_t count = 0;
};

static SimQueue* to_queue(osMessageQueueId_t id) {
    return reinterpret_cast<SimQueue*>(id);
}

extern "C" {

osMessageQueueId_t osMessageQueueNew(uint32_t msg_count, uint32_t msg_size, const void* /*attr*/) {
    if (msg_count == 0 || msg_size == 0) {
        return NULL;
    }
    auto* q = new(std::nothrow) SimQueue();
    if (q == NULL) {
        return NULL;
    }
    q->capacity = msg_count;
    q->itemSize = msg_size;
    q->storage.resize(static_cast<size_t>(msg_count) * msg_size, 0);
    q->used.resize(msg_count, 0);
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    return reinterpret_cast<osMessageQueueId_t>(q);
}

int32_t osMessageQueuePut(osMessageQueueId_t mq_id, const void* msg_ptr, uint8_t /*msg_prio*/, uint32_t /*timeout*/) {
    if (mq_id == NULL || msg_ptr == NULL) {
        return -1;
    }
    SimQueue* q = to_queue(mq_id);
    if (q->count >= q->capacity) {
        return -2;
    }
    std::memcpy(q->storage.data() + static_cast<size_t>(q->head) * q->itemSize, msg_ptr, q->itemSize);
    q->used[q->head] = 1;
    q->head = (q->head + 1) % q->capacity;
    ++q->count;
    return 0;
}

int32_t osMessageQueueGet(osMessageQueueId_t mq_id, void* msg_ptr, uint8_t* /*msg_prio*/, uint32_t /*timeout*/) {
    if (mq_id == NULL || msg_ptr == NULL) {
        return -1;
    }
    SimQueue* q = to_queue(mq_id);
    if (q->count == 0 || q->used[q->tail] == 0) {
        return -2;
    }
    std::memcpy(msg_ptr, q->storage.data() + static_cast<size_t>(q->tail) * q->itemSize, q->itemSize);
    q->used[q->tail] = 0;
    q->tail = (q->tail + 1) % q->capacity;
    --q->count;
    return 0;
}

uint32_t osMessageQueueGetCount(osMessageQueueId_t mq_id) {
    if (mq_id == NULL) {
        return 0;
    }
    SimQueue* q = to_queue(mq_id);
    return q->count;
}

uint32_t osMessageQueueGetSpace(osMessageQueueId_t mq_id) {
    if (mq_id == NULL) {
        return 0;
    }
    SimQueue* q = to_queue(mq_id);
    return q->capacity - q->count;
}

} // extern "C"

#endif /* SIMULATOR */
