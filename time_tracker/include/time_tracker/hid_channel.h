#ifndef TIME_TRACKER_HID_CHANNEL_H
#define TIME_TRACKER_HID_CHANNEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <hidapi.h>

#include "time_tracker/order_queue.h"
#include "time_tracker/types.h"

int send_feature_text(hid_device* handle, uint8_t report_id, const char* text);
int send_window_title_chunked(hid_device* handle, const char* text);
void poll_agent_message(
    hid_device* handle,
    order_queue_t* queue,
    uint8_t expected_report_id,
    work_item_kind_t kind,
    const char* kind_label,
    char* last_payload,
    size_t last_payload_len,
    poll_state_t* state,
    bool debug_poll
);
hid_device* open_teensy_feature_handle(void);

#endif
