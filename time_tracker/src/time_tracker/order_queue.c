#include "time_tracker/order_queue.h"

#include <string.h>

bool order_queue_init(order_queue_t* queue) {
    if (!queue) {
        return false;
    }

    memset(queue, 0, sizeof(*queue));
    InitializeCriticalSection(&queue->lock);
    queue->items_available = CreateSemaphoreA(NULL, 0, ORDER_QUEUE_CAPACITY, NULL);
    if (!queue->items_available) {
        DeleteCriticalSection(&queue->lock);
        return false;
    }

    return true;
}

void order_queue_destroy(order_queue_t* queue) {
    if (!queue) {
        return;
    }

    if (queue->items_available) {
        CloseHandle(queue->items_available);
        queue->items_available = NULL;
    }
    DeleteCriticalSection(&queue->lock);
}

bool order_queue_push(order_queue_t* queue, work_item_kind_t kind, const char* payload, size_t* out_count_after_push) {
    if (!queue || !payload) {
        return false;
    }

    bool pushed = false;
    size_t count_after_push = 0;

    EnterCriticalSection(&queue->lock);
    if (queue->count < ORDER_QUEUE_CAPACITY) {
        const size_t copy_len = strnlen(payload, ORDER_MAX_LEN);
        queue->items[queue->tail].kind = kind;
        if (copy_len > 0) {
            memcpy(queue->items[queue->tail].payload, payload, copy_len);
        }
        queue->items[queue->tail].payload[copy_len] = '\0';

        queue->tail = (queue->tail + 1) % ORDER_QUEUE_CAPACITY;
        queue->count++;
        count_after_push = queue->count;
        pushed = true;
    }
    LeaveCriticalSection(&queue->lock);

    if (!pushed) {
        return false;
    }

    if (!ReleaseSemaphore(queue->items_available, 1, NULL)) {
        EnterCriticalSection(&queue->lock);
        queue->tail = (queue->tail + ORDER_QUEUE_CAPACITY - 1) % ORDER_QUEUE_CAPACITY;
        queue->items[queue->tail].payload[0] = '\0';
        if (queue->count > 0) {
            queue->count--;
        }
        LeaveCriticalSection(&queue->lock);
        return false;
    }

    if (out_count_after_push) {
        *out_count_after_push = count_after_push;
    }

    return true;
}

bool order_queue_pop_wait(order_queue_t* queue, work_item_t* out_item, DWORD timeout_ms) {
    if (!queue || !out_item) {
        return false;
    }

    const DWORD wait_rc = WaitForSingleObject(queue->items_available, timeout_ms);
    if (wait_rc != WAIT_OBJECT_0) {
        return false;
    }

    EnterCriticalSection(&queue->lock);
    if (queue->count == 0) {
        LeaveCriticalSection(&queue->lock);
        return false;
    }

    *out_item = queue->items[queue->head];
    queue->items[queue->head].payload[0] = '\0';
    queue->head = (queue->head + 1) % ORDER_QUEUE_CAPACITY;
    queue->count--;
    LeaveCriticalSection(&queue->lock);
    return true;
}
