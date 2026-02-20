#include <windows.h>
#include <hidapi.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hidapi.h"

#define MAX_STR 255
#define HID_REPORT_TOTAL_LEN 65
#define HID_REPORT_PAYLOAD_LEN 64
#define HID_REPORT_ID_WINDOW 0x01
#define HID_REPORT_ID_COMMAND 0x02
#define WINDOW_POLL_MS_DEFAULT 500
#define WINDOW_POLL_MS_MIN 50
#define AGENT_POLL_IDLE_LOG_EVERY 20
#define AGENT_POLL_DUP_LOG_EVERY 20
#define AGENT_POLL_RAW_PREVIEW 12
#define LOG_PATH_MAX (MAX_PATH * 2)

static void hide_console_window(void) {
    HWND hwnd = GetConsoleWindow();
    if (hwnd != NULL) {
        ShowWindow(hwnd, SW_HIDE);
    }
}

static void derive_default_log_path(char* out, size_t out_len) {
    if (!out || out_len == 0) {
        return;
    }

    out[0] = '\0';

    char exe_path[MAX_PATH];
    const DWORD exe_len = GetModuleFileNameA(NULL, exe_path, MAX_PATH);
    if (exe_len > 0 && exe_len < MAX_PATH) {
        for (int i = (int)exe_len - 1; i >= 0; --i) {
            if (exe_path[i] == '\\' || exe_path[i] == '/') {
                exe_path[i] = '\0';
                break;
            }
        }
        snprintf(out, out_len, "%s\\implant.log", exe_path);
        return;
    }

    char temp_path[MAX_PATH];
    const DWORD temp_len = GetTempPathA(MAX_PATH, temp_path);
    if (temp_len > 0 && temp_len < MAX_PATH) {
        snprintf(out, out_len, "%simplant.log", temp_path);
        return;
    }

    snprintf(out, out_len, "implant.log");
}

static bool redirect_output_to_log(const char* log_path) {
    if (!log_path || log_path[0] == '\0') {
        return false;
    }

    FILE* stdout_file = freopen(log_path, "a", stdout);
    if (!stdout_file) {
        return false;
    }

    FILE* stderr_file = freopen(log_path, "a", stderr);
    if (!stderr_file) {
        return false;
    }

    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    return true;
}

static void log_win_error(const char* context) {
    DWORD error = GetLastError();
    LPVOID lpMsgBuf = NULL;
    FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        error,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR)&lpMsgBuf,
        0,
        NULL
    );

    printf("  [!] ERROR: %s (Code: %lu): %s\n", context, (unsigned long)error, lpMsgBuf ? (char*)lpMsgBuf : "<none>");
    if (lpMsgBuf) {
        LocalFree(lpMsgBuf);
    }
}

static int utf16_to_utf8(const wchar_t* src, char* dst, int dst_len) {
    if (!src || !dst || dst_len <= 0) {
        return 0;
    }

    const int rc = WideCharToMultiByte(CP_UTF8, 0, src, -1, dst, dst_len, NULL, NULL);
    if (rc <= 0) {
        dst[0] = '\0';
        return 0;
    }

    return rc - 1;
}

static int send_feature_text(hid_device* handle, uint8_t report_id, const char* text) {
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

static void poll_agent_command(hid_device* handle, char* last_agent_cmd, size_t last_agent_cmd_len, bool debug_poll) {
    static uint32_t idle_polls = 0;
    static uint32_t duplicate_polls = 0;

    if (!handle || !last_agent_cmd || last_agent_cmd_len == 0) {
        if (debug_poll) {
            printf("[AGENT_POLL] skipped: invalid args (handle=%p, last_agent_cmd=%p, len=%zu)\n",
                   (void*)handle, (void*)last_agent_cmd, last_agent_cmd_len);
        }
        return;
    }

    unsigned char buf[HID_REPORT_TOTAL_LEN];
    memset(buf, 0, sizeof(buf));
    buf[0] = HID_REPORT_ID_COMMAND;

    const int res = hid_get_feature_report(handle, buf, sizeof(buf));
    if (res < 0) {
        printf("[AGENT_POLL] hid_get_feature_report failed: %ls\n", hid_error(handle));
        return;
    }

    if (res <= 1) {
        ++idle_polls;
        if (debug_poll && (idle_polls % AGENT_POLL_IDLE_LOG_EVERY) == 0) {
            printf("[AGENT_POLL] idle (res=%d, consecutive=%u)\n", res, (unsigned)idle_polls);
        }
        return;
    }

    if (idle_polls != 0 && debug_poll) {
        printf("[AGENT_POLL] active after %u idle poll(s)\n", (unsigned)idle_polls);
    }
    idle_polls = 0;

    if (debug_poll) {
        printf("[AGENT_POLL] raw res=%d report_id=0x%02X bytes=", res, (unsigned)buf[0]);
        print_hex_preview(buf, res);
        printf("\n");
    }

    if (buf[0] != HID_REPORT_ID_COMMAND) {
        if (debug_poll) {
            printf("[AGENT_POLL] ignored non-command report_id=0x%02X\n", (unsigned)buf[0]);
        }
        return;
    }

    char cmd[HID_REPORT_PAYLOAD_LEN + 1];
    memset(cmd, 0, sizeof(cmd));
    const size_t cmd_len = strnlen((const char*)&buf[1], HID_REPORT_PAYLOAD_LEN);
    if (cmd_len > 0) {
        memcpy(cmd, (const char*)&buf[1], cmd_len);
    }
    cmd[cmd_len] = '\0';

    if (cmd[0] == '\0') {
        // Allow emitting the same command text again after an empty poll cycle.
        last_agent_cmd[0] = '\0';
        duplicate_polls = 0;
        if (debug_poll) {
            printf("[AGENT_POLL] empty command payload (dedupe state reset)\n");
        }
        return;
    }

    if (strcmp(cmd, last_agent_cmd) == 0) {
        ++duplicate_polls;
        if (debug_poll && (duplicate_polls % AGENT_POLL_DUP_LOG_EVERY) == 0) {
            printf("[AGENT_POLL] duplicate command suppressed x%u: %s\n", (unsigned)duplicate_polls, cmd);
        }
        return;
    }
    duplicate_polls = 0;

    const size_t copy_len = strnlen(cmd, last_agent_cmd_len - 1);
    if (copy_len > 0) {
        memcpy(last_agent_cmd, cmd, copy_len);
    }
    last_agent_cmd[copy_len] = '\0';
    printf("[AGENT_CMD] %s\n", last_agent_cmd);
    if (debug_poll) {
        printf("[AGENT_POLL] accepted command len=%u\n", (unsigned)strnlen(last_agent_cmd, last_agent_cmd_len));
    }
}

static hid_device* open_teensy_feature_handle(void) {
    hid_device* handle = NULL;
    struct hid_device_info* devs = hid_enumerate(0x16C0, 0x0000);
    struct hid_device_info* cur = devs;

    printf("--- HID Enumeration Started ---\n");

    while (cur) {
        printf("\n[DEVICE IDENTIFIED]\n");
        printf("  Product:      %ls\n", cur->product_string);
        printf("  VID/PID:      %04x:%04x\n", cur->vendor_id, cur->product_id);
        printf("  Interface #:  %d\n", cur->interface_number);
        printf("  Usage Page:   0x%04x\n", cur->usage_page);
        printf("  Usage:        0x%04x\n", cur->usage);
        printf("  Path:         %s\n", cur->path);

        if (cur->interface_number == 2 && handle == NULL) {
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

int main(int argc, char* argv[]) {
    const char* one_shot_command = NULL;
    const char* requested_log_path = NULL;
    int window_poll_ms = WINDOW_POLL_MS_DEFAULT;
    bool debug_poll = false;
    bool foreground = false;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--cmd") == 0) {
            if (i + 1 >= argc) {
                printf("Usage error: --cmd requires a value.\n");
                return -1;
            }
            one_shot_command = argv[++i];
            continue;
        }

        if (strcmp(argv[i], "--interval-ms") == 0) {
            if (i + 1 >= argc) {
                printf("Usage error: --interval-ms requires a value.\n");
                return -1;
            }
            window_poll_ms = atoi(argv[++i]);
            if (window_poll_ms < WINDOW_POLL_MS_MIN) {
                window_poll_ms = WINDOW_POLL_MS_MIN;
            }
            continue;
        }

        if (strcmp(argv[i], "--debug-poll") == 0) {
            debug_poll = true;
            continue;
        }

        if (strcmp(argv[i], "--log-file") == 0) {
            if (i + 1 >= argc) {
                printf("Usage error: --log-file requires a value.\n");
                return -1;
            }
            requested_log_path = argv[++i];
            continue;
        }

        if (strcmp(argv[i], "--foreground") == 0) {
            foreground = true;
            continue;
        }

        printf("Unknown argument: %s\n", argv[i]);
        printf("Supported args: --cmd \"text\" --interval-ms <n> --debug-poll --log-file \"path\" --foreground\n");
        return -1;
    }

    if (!foreground) {
        hide_console_window();
    }

    char resolved_log_path[LOG_PATH_MAX];
    resolved_log_path[0] = '\0';

    if (requested_log_path != NULL && requested_log_path[0] != '\0') {
        snprintf(resolved_log_path, sizeof(resolved_log_path), "%s", requested_log_path);
    } else {
        derive_default_log_path(resolved_log_path, sizeof(resolved_log_path));
    }

    if (!redirect_output_to_log(resolved_log_path)) {
        // Last fallback to the temp directory if the chosen path is unavailable.
        char fallback_log_path[LOG_PATH_MAX];
        fallback_log_path[0] = '\0';
        char temp_path[MAX_PATH];
        const DWORD temp_len = GetTempPathA(MAX_PATH, temp_path);
        if (temp_len > 0 && temp_len < MAX_PATH) {
            snprintf(fallback_log_path, sizeof(fallback_log_path), "%simplant.log", temp_path);
            if (redirect_output_to_log(fallback_log_path)) {
                snprintf(resolved_log_path, sizeof(resolved_log_path), "%s", fallback_log_path);
            } else {
                return -1;
            }
        } else {
            return -1;
        }
    }

    printf("[START] implant.exe started (headless=%s, log=%s)\n",
           foreground ? "no" : "yes",
           resolved_log_path);

    if (hid_init()) {
        printf("FATAL: Failed to initialize HIDAPI.\n");
        return -1;
    }

    hid_device* handle = open_teensy_feature_handle();
    if (!handle) {
        printf("\nFAILED: Could not connect to Teensy MI_02.\n");
        hid_exit();
        return -1;
    }

    if (one_shot_command != NULL) {
        printf("[CMD] Sending command on report_id=0x%02X: %s\n", HID_REPORT_ID_COMMAND, one_shot_command);
        const int res = send_feature_text(handle, HID_REPORT_ID_COMMAND, one_shot_command);
        if (res < 0) {
            printf("[CMD] FAILED: %ls\n", hid_error(handle));
            hid_close(handle);
            hid_exit();
            return -1;
        }
        printf("[CMD] OK (%d bytes)\n", res);
        hid_close(handle);
        hid_exit();
        return 0;
    }

    printf("\n--- Monitoring Foreground Windows (report_id=0x%02X) ---\n", HID_REPORT_ID_WINDOW);
    if (debug_poll) {
        printf("[AGENT_POLL] debug enabled (idle log every %u polls, duplicate log every %u polls)\n",
               (unsigned)AGENT_POLL_IDLE_LOG_EVERY,
               (unsigned)AGENT_POLL_DUP_LOG_EVERY);
    }

    wchar_t window_title_w[MAX_STR];
    char current_title_utf8[MAX_STR * 3];
    char last_title_utf8[MAX_STR * 3];
    char last_agent_command[HID_REPORT_PAYLOAD_LEN + 1];
    last_title_utf8[0] = '\0';
    last_agent_command[0] = '\0';

    while (true) {
        poll_agent_command(handle, last_agent_command, sizeof(last_agent_command), debug_poll);

        HWND hwnd = GetForegroundWindow();
        if (hwnd != NULL && GetWindowTextW(hwnd, window_title_w, MAX_STR) > 0) {
            utf16_to_utf8(window_title_w, current_title_utf8, (int)sizeof(current_title_utf8));

            if (current_title_utf8[0] != '\0' && strcmp(current_title_utf8, last_title_utf8) != 0) {
                strncpy(last_title_utf8, current_title_utf8, sizeof(last_title_utf8));
                last_title_utf8[sizeof(last_title_utf8) - 1] = '\0';

                printf("Sending window title: [%s] ... ", current_title_utf8);
                const int res = send_feature_text(handle, HID_REPORT_ID_WINDOW, current_title_utf8);
                if (res < 0) {
                    printf("FAILED: %ls\n", hid_error(handle));
                } else {
                    printf("OK (%d bytes)\n", res);
                }
            }
        }

        Sleep((DWORD)window_poll_ms);
    }

    hid_close(handle);
    hid_exit();
    return 0;
}
