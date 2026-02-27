#ifndef TIME_TRACKER_ORDER_QUEUE_H
#define TIME_TRACKER_ORDER_QUEUE_H

#include <stdbool.h>
#include <stddef.h>

#include <windows.h>

#include "time_tracker/config.h"
#include "time_tracker/types.h"

typedef struct {
    work_item_t items[ORDER_QUEUE_CAPACITY];
    size_t head;
    size_t tail;
    size_t count;
    CRITICAL_SECTION lock;
    HANDLE items_available;
} order_queue_t;

bool order_queue_init(order_queue_t* queue);
void order_queue_destroy(order_queue_t* queue);
bool order_queue_push(order_queue_t* queue, work_item_kind_t kind, const char* payload, size_t* out_count_after_push);
bool order_queue_pop_wait(order_queue_t* queue, work_item_t* out_item, DWORD timeout_ms);

#endif
