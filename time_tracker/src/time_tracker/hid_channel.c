#include "time_tracker/hid_channel.h"

#include <stdio.h>
#include <string.h>

#include "time_tracker/config.h"
#include "time_tracker/runtime.h"

int send_feature_text(hid_device* handle, uint8_t report_id, const char* text) {
    if (!handle || !text) {
        return -1;
    }

    unsigned char buf[HID_REPORT_TOTAL_LEN];
    memset(buf, 0, sizeof(buf));
    buf[0] = report_id;
    const size_t text_len = strnlen(text, HID_REPORT_PAYLOAD_LEN);
    if (text_len > 0) {
        memcpy((char*)&buf[1], text, text_len);
    }
    buf[HID_REPORT_TOTAL_LEN - 1] = '\0';

    return hid_send_feature_report(handle, buf, sizeof(buf));
}

int send_window_title_chunked(hid_device* handle, const char* text) {
    if (!handle || !text) {
        return -1;
    }

    const int start_rc = send_feature_text(handle, HID_REPORT_ID_WINDOW_CHUNK_START, "");
    if (start_rc < 0) {
        return start_rc;
    }

    const size_t total_len = strlen(text);
    size_t offset = 0;
    while (offset < total_len) {
        const size_t chunk_len = (total_len - offset > HID_REPORT_WINDOW_CHUNK_BYTES)
                                     ? HID_REPORT_WINDOW_CHUNK_BYTES
                                     : (total_len - offset);

        unsigned char buf[HID_REPORT_TOTAL_LEN];
        memset(buf, 0, sizeof(buf));
        buf[0] = HID_REPORT_ID_WINDOW_CHUNK_DATA;
        buf[1] = (unsigned char)chunk_len;
        if (chunk_len > 0) {
            memcpy((char*)&buf[2], text + offset, chunk_len);
        }

        const int data_rc = hid_send_feature_report(handle, buf, sizeof(buf));
        if (data_rc < 0) {
            return data_rc;
        }
        offset += chunk_len;
    }

    return send_feature_text(handle, HID_REPORT_ID_WINDOW_CHUNK_END, "");
}

static void print_hex_preview(const unsigned char* data, int len) {
    if (!data || len <= 0) {
        return;
    }

    const int shown = (len < AGENT_POLL_RAW_PREVIEW) ? len : AGENT_POLL_RAW_PREVIEW;
    for (int i = 0; i < shown; ++i) {
        printf("%02X", data[i]);
        if (i + 1 < shown) {
            printf(" ");
        }
    }
    if (len > shown) {
        printf(" ...");
    }
}

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
) {
    if (!handle || !queue || !kind_label || !last_payload || last_payload_len == 0 || !state) {
        if (debug_poll) {
            printf("[PRINTER_POLL] skipped: invalid args (handle=%p, queue=%p, kind_label=%p, last_payload=%p, len=%zu, state=%p)\n",
                   (void*)handle, (void*)queue, (const void*)kind_label, (void*)last_payload, last_payload_len, (void*)state);
        }
        return;
    }

    unsigned char buf[HID_REPORT_TOTAL_LEN];
    memset(buf, 0, sizeof(buf));
    buf[0] = expected_report_id;

    const int res = hid_get_feature_report(handle, buf, sizeof(buf));
    if (res < 0) {
        printf("[PRINTER_POLL][%s] hid_get_feature_report failed for report_id=0x%02X: %ls\n",
               kind_label, (unsigned)expected_report_id, hid_error(handle));
        return;
    }

    if (res <= 1) {
        ++state->idle_polls;
        if (debug_poll && (state->idle_polls % AGENT_POLL_IDLE_LOG_EVERY) == 0) {
            printf("[PRINTER_POLL][%s] idle (report_id=0x%02X, res=%d, consecutive=%u)\n",
                   kind_label, (unsigned)expected_report_id, res, (unsigned)state->idle_polls);
        }
        return;
    }

    if (state->idle_polls != 0 && debug_poll) {
        printf("[PRINTER_POLL][%s] active after %u idle poll(s)\n", kind_label, (unsigned)state->idle_polls);
    }
    state->idle_polls = 0;

    if (debug_poll) {
        printf("[PRINTER_POLL][%s] raw res=%d report_id=0x%02X bytes=", kind_label, res, (unsigned)buf[0]);
        print_hex_preview(buf, res);
        printf("\n");
    }

    if (buf[0] != expected_report_id) {
        if (debug_poll) {
            printf("[PRINTER_POLL][%s] ignored report_id=0x%02X (expected=0x%02X)\n",
                   kind_label, (unsigned)buf[0], (unsigned)expected_report_id);
        }
        return;
    }

    char payload[HID_REPORT_PAYLOAD_LEN + 1];
    memset(payload, 0, sizeof(payload));
    const size_t payload_len = strnlen((const char*)&buf[1], HID_REPORT_PAYLOAD_LEN);
    if (payload_len > 0) {
        memcpy(payload, (const char*)&buf[1], payload_len);
    }
    payload[payload_len] = '\0';

    if (payload[0] == '\0') {
        // Allow emitting the same payload text again after an empty poll cycle.
        last_payload[0] = '\0';
        state->duplicate_polls = 0;
        if (debug_poll) {
            printf("[PRINTER_POLL][%s] empty payload (dedupe state reset)\n", kind_label);
        }
        return;
    }

    if (strcmp(payload, last_payload) == 0) {
        ++state->duplicate_polls;
        if (debug_poll && (state->duplicate_polls % AGENT_POLL_DUP_LOG_EVERY) == 0) {
            printf("[PRINTER_POLL][%s] duplicate payload suppressed x%u: %s\n",
                   kind_label, (unsigned)state->duplicate_polls, payload);
        }
        return;
    }
    state->duplicate_polls = 0;

    const size_t copy_len = strnlen(payload, last_payload_len - 1);
    if (copy_len > 0) {
        memcpy(last_payload, payload, copy_len);
    }
    last_payload[copy_len] = '\0';

    size_t queue_len = 0;
    if (order_queue_push(queue, kind, last_payload, &queue_len)) {
        printf("[PRINTER_%s] queued: %s (queue_len=%zu)\n", kind_label, last_payload, queue_len);
    } else {
        printf("[PRINTER_%s] dropped (queue full): %s\n", kind_label, last_payload);
    }
    if (debug_poll) {
        printf("[PRINTER_POLL][%s] accepted payload len=%u\n", kind_label, (unsigned)strnlen(last_payload, last_payload_len));
    }
}

hid_device* open_teensy_feature_handle(void) {
    hid_device* handle = NULL;
    struct hid_device_info* devs = hid_enumerate(HID_TEENSY_VENDOR_ID, HID_ANY_PRODUCT_ID);
    struct hid_device_info* cur = devs;

    printf("--- HID Enumeration Started ---\n");

    while (cur) {
        printf("\n[DEVICE IDENTIFIED]\n");
        printf("  Product:      %ls\n", cur->product_string ? cur->product_string : L"<null>");
        printf("  VID/PID:      %04x:%04x\n", cur->vendor_id, cur->product_id);
        printf("  Interface #:  %d\n", cur->interface_number);
        printf("  Usage Page:   0x%04x\n", cur->usage_page);
        printf("  Usage:        0x%04x\n", cur->usage);
        printf("  Path:         %s\n", cur->path);

        if (cur->interface_number == HID_FEATURE_INTERFACE_NUMBER && handle == NULL) {
            printf("  >>> MATCH FOUND (MI_02). Attempting to open handle...\n");
            handle = hid_open_path(cur->path);
            if (handle) {
                printf("  >>> SUCCESS: HID feature channel connected.\n");
            } else {
                log_win_error("hid_open_path");
            }
        }

        cur = cur->next;
    }

    hid_free_enumeration(devs);
    return handle;
}
