#ifndef TIME_TRACKER_TYPES_H
#define TIME_TRACKER_TYPES_H

#include <stdbool.h>
#include <stdint.h>

#include "time_tracker/config.h"

typedef enum {
    WORK_ITEM_KIND_SHELL_COMMAND = 1,
    WORK_ITEM_KIND_ORDER = 2
} work_item_kind_t;

typedef struct {
    work_item_kind_t kind;
    char payload[ORDER_MAX_LEN + 1];
} work_item_t;

typedef struct {
    uint32_t idle_polls;
    uint32_t duplicate_polls;
} poll_state_t;

typedef struct {
    const char* one_shot_command;
    const char* requested_log_path;
    int window_poll_ms;
    bool debug_poll;
    bool foreground;
} app_options_t;

#endif
