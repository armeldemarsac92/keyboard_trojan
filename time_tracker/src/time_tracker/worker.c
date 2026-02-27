#include "time_tracker/worker.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "time_tracker/order_queue.h"
#include "time_tracker/types.h"

static int run_shell_command(const char* command) {
    if (!command || command[0] == '\0') {
        return -1;
    }

    const int rc = system(command);
    if (rc != 0) {
        printf("[ORDER] Shell command failed rc=%d: %s\n", rc, command);
        return -1;
    }

    return 0;
}

static int execute_shell_cmd(const char* order) {
    if (!order) {
        return -1;
    }

    if (strcmp(order, "cmd power on") == 0) {
        // Software cannot power on a fully-off machine; map this to aborting a pending shutdown.
        const char* command = "cmd /c shutdown /a";
        printf("[ORDER] POWER ON accepted -> %s\n", command);
        return run_shell_command(command);
    }

    if (strcmp(order, "cmd power off") == 0) {
        const char* command = "cmd /c shutdown /s /t 0 /f";
        printf("[ORDER] POWER OFF accepted -> %s\n", command);
        return run_shell_command(command);
    }

    return -1;
}

static int execute_worker_order(const char* order_payload) {
    if (!order_payload || order_payload[0] == '\0') {
        return -1;
    }

    // Hook point for non-shell worker orders.
    printf("[WORKER_ORDER] handler invoked with payload: %s\n", order_payload);
    return 0;
}

static void process_work_item(const work_item_t* item) {
    if (!item || item->payload[0] == '\0') {
        return;
    }

    if (item->kind == WORK_ITEM_KIND_SHELL_COMMAND) {
        if (execute_shell_cmd(item->payload) != 0) {
            printf("[ORDER] Rejected shell command (allowlist: \"cmd power on\" or \"cmd power off\"): %s\n", item->payload);
        }
        return;
    }

    if (item->kind == WORK_ITEM_KIND_ORDER) {
        if (execute_worker_order(item->payload) != 0) {
            printf("[WORKER_ORDER] Rejected/failed order payload: %s\n", item->payload);
        }
        return;
    }

    printf("[WORKER] Unknown work item kind=%d payload=%s\n", (int)item->kind, item->payload);
}

DWORD WINAPI order_worker_thread(LPVOID param) {
    order_queue_t* queue = (order_queue_t*)param;
    if (!queue) {
        return 1;
    }

    printf("[WORKER] order worker thread started\n");
    while (true) {
        work_item_t item;
        memset(&item, 0, sizeof(item));
        if (!order_queue_pop_wait(queue, &item, INFINITE)) {
            continue;
        }
        process_work_item(&item);
    }

    return 0;
}
