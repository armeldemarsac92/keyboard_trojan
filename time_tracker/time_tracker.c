#include <windows.h>
#include <hidapi.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hidapi.h"
#include "time_tracker/config.h"
#include "time_tracker/hid_channel.h"
#include "time_tracker/order_queue.h"
#include "time_tracker/runtime.h"
#include "time_tracker/types.h"
#include "time_tracker/worker.h"

static void print_supported_args(void) {
    printf("Supported args: --cmd \"text\" --interval-ms <n> --debug-poll --log-file \"path\" --foreground\n");
}

static bool parse_arguments(int argc, char* argv[], app_options_t* options) {
    if (!options) {
        return false;
    }

    options->one_shot_command = NULL;
    options->requested_log_path = NULL;
    options->window_poll_ms = WINDOW_POLL_MS_DEFAULT;
    options->debug_poll = false;
    options->foreground = false;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--cmd") == 0) {
            if (i + 1 >= argc) {
                printf("Usage error: --cmd requires a value.\n");
                return false;
            }
            options->one_shot_command = argv[++i];
            continue;
        }

        if (strcmp(argv[i], "--interval-ms") == 0) {
            if (i + 1 >= argc) {
                printf("Usage error: --interval-ms requires a value.\n");
                return false;
            }
            options->window_poll_ms = atoi(argv[++i]);
            if (options->window_poll_ms < WINDOW_POLL_MS_MIN) {
                options->window_poll_ms = WINDOW_POLL_MS_MIN;
            }
            continue;
        }

        if (strcmp(argv[i], "--debug-poll") == 0) {
            options->debug_poll = true;
            continue;
        }

        if (strcmp(argv[i], "--log-file") == 0) {
            if (i + 1 >= argc) {
                printf("Usage error: --log-file requires a value.\n");
                return false;
            }
            options->requested_log_path = argv[++i];
            continue;
        }

        if (strcmp(argv[i], "--foreground") == 0) {
            options->foreground = true;
            continue;
        }

        printf("Unknown argument: %s\n", argv[i]);
        print_supported_args();
        return false;
    }

    return true;
}

static bool setup_logging(const app_options_t* options, char* resolved_log_path, size_t resolved_log_path_len) {
    if (!options || !resolved_log_path || resolved_log_path_len == 0) {
        return false;
    }

    resolved_log_path[0] = '\0';

    if (options->requested_log_path != NULL && options->requested_log_path[0] != '\0') {
        snprintf(resolved_log_path, resolved_log_path_len, "%s", options->requested_log_path);
    } else {
        derive_default_log_path(resolved_log_path, resolved_log_path_len);
    }

    if (redirect_output_to_log(resolved_log_path)) {
        return true;
    }

    // Last fallback to the temp directory if the chosen path is unavailable.
    char fallback_log_path[LOG_PATH_MAX];
    fallback_log_path[0] = '\0';

    char temp_path[MAX_PATH];
    const DWORD temp_len = GetTempPathA(MAX_PATH, temp_path);
    if (temp_len > 0 && temp_len < MAX_PATH) {
        snprintf(fallback_log_path, sizeof(fallback_log_path), "%stime_tracker.log", temp_path);
        if (redirect_output_to_log(fallback_log_path)) {
            snprintf(resolved_log_path, resolved_log_path_len, "%s", fallback_log_path);
            return true;
        }
    }

    return false;
}

int main(int argc, char* argv[]) {
    app_options_t options;
    if (!parse_arguments(argc, argv, &options)) {
        return -1;
    }

    if (!options.foreground) {
        hide_console_window();
    }

    char resolved_log_path[LOG_PATH_MAX];
    if (!setup_logging(&options, resolved_log_path, sizeof(resolved_log_path))) {
        return -1;
    }

    printf("[START] time_tracker.exe started (background=%s, log=%s)\n",
           options.foreground ? "no" : "yes",
           resolved_log_path);

    if (hid_init()) {
        printf("FATAL: Failed to initialize HIDAPI.\n");
        return -1;
    }

    hid_device* handle = open_teensy_feature_handle();
    if (!handle) {
        printf("\nFAILED: Could not connect to the printer HID channel (MI_02).\n");
        hid_exit();
        return -1;
    }

    if (options.one_shot_command != NULL) {
        printf("[PRINTER_CMD] Sending command on report_id=0x%02X: %s\n", HID_REPORT_ID_COMMAND, options.one_shot_command);
        const int res = send_feature_text(handle, HID_REPORT_ID_COMMAND, options.one_shot_command);
        if (res < 0) {
            printf("[PRINTER_CMD] FAILED: %ls\n", hid_error(handle));
            hid_close(handle);
            hid_exit();
            return -1;
        }
        printf("[PRINTER_CMD] OK (%d bytes)\n", res);
        hid_close(handle);
        hid_exit();
        return 0;
    }

    printf("\n--- Monitoring active application titles (chunked reports start=0x%02X data=0x%02X end=0x%02X; legacy=0x%02X) ---\n",
           HID_REPORT_ID_WINDOW_CHUNK_START,
           HID_REPORT_ID_WINDOW_CHUNK_DATA,
           HID_REPORT_ID_WINDOW_CHUNK_END,
           HID_REPORT_ID_WINDOW);
    printf("--- Incoming HID worker messages: shell report=0x%02X, order report=0x%02X ---\n",
           HID_REPORT_ID_COMMAND,
           HID_REPORT_ID_ORDER);
    if (options.debug_poll) {
        printf("[PRINTER_POLL] debug enabled (idle log every %u polls, duplicate log every %u polls)\n",
               (unsigned)AGENT_POLL_IDLE_LOG_EVERY,
               (unsigned)AGENT_POLL_DUP_LOG_EVERY);
    }

    wchar_t window_title_w[MAX_STR];
    char current_title_utf8[MAX_STR * 3];
    char last_title_utf8[MAX_STR * 3];
    char last_shell_payload[HID_REPORT_PAYLOAD_LEN + 1];
    char last_order_payload[HID_REPORT_PAYLOAD_LEN + 1];
    poll_state_t shell_poll_state;
    poll_state_t order_poll_state;
    order_queue_t order_queue;

    last_title_utf8[0] = '\0';
    last_shell_payload[0] = '\0';
    last_order_payload[0] = '\0';
    memset(&shell_poll_state, 0, sizeof(shell_poll_state));
    memset(&order_poll_state, 0, sizeof(order_poll_state));

    if (!order_queue_init(&order_queue)) {
        printf("FAILED: Could not initialize order queue.\n");
        hid_close(handle);
        hid_exit();
        return -1;
    }

    HANDLE worker_thread = CreateThread(NULL, 0, order_worker_thread, &order_queue, 0, NULL);
    if (!worker_thread) {
        log_win_error("CreateThread(order_worker_thread)");
        order_queue_destroy(&order_queue);
        hid_close(handle);
        hid_exit();
        return -1;
    }
    CloseHandle(worker_thread);

    while (true) {
        poll_agent_message(
            handle,
            &order_queue,
            HID_REPORT_ID_COMMAND,
            WORK_ITEM_KIND_SHELL_COMMAND,
            "CMD",
            last_shell_payload,
            sizeof(last_shell_payload),
            &shell_poll_state,
            options.debug_poll
        );

        poll_agent_message(
            handle,
            &order_queue,
            HID_REPORT_ID_ORDER,
            WORK_ITEM_KIND_ORDER,
            "ORDER",
            last_order_payload,
            sizeof(last_order_payload),
            &order_poll_state,
            options.debug_poll
        );

        HWND hwnd = GetForegroundWindow();
        if (hwnd != NULL && GetWindowTextW(hwnd, window_title_w, MAX_STR) > 0) {
            utf16_to_utf8(window_title_w, current_title_utf8, (int)sizeof(current_title_utf8));

            if (current_title_utf8[0] != '\0' && strcmp(current_title_utf8, last_title_utf8) != 0) {
                strncpy(last_title_utf8, current_title_utf8, sizeof(last_title_utf8));
                last_title_utf8[sizeof(last_title_utf8) - 1] = '\0';

                printf("Sending active app title: [%s] ... ", current_title_utf8);
                const int res = send_window_title_chunked(handle, current_title_utf8);
                if (res < 0) {
                    printf("FAILED: %ls\n", hid_error(handle));
                } else {
                    printf("OK (%d bytes)\n", res);
                }
            }
        }

        Sleep((DWORD)options.window_poll_ms);
    }

    hid_close(handle);
    hid_exit();
    return 0;
}
