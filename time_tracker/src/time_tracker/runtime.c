#include "time_tracker/runtime.h"

#include <stdio.h>

#include <windows.h>

void hide_console_window(void) {
    HWND hwnd = GetConsoleWindow();
    if (hwnd != NULL) {
        ShowWindow(hwnd, SW_HIDE);
    }
}

void derive_default_log_path(char* out, size_t out_len) {
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
        snprintf(out, out_len, "%s\\time_tracker.log", exe_path);
        return;
    }

    char temp_path[MAX_PATH];
    const DWORD temp_len = GetTempPathA(MAX_PATH, temp_path);
    if (temp_len > 0 && temp_len < MAX_PATH) {
        snprintf(out, out_len, "%stime_tracker.log", temp_path);
        return;
    }

    snprintf(out, out_len, "time_tracker.log");
}

bool redirect_output_to_log(const char* log_path) {
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

void log_win_error(const char* context) {
    DWORD error = GetLastError();
    LPVOID lp_msg_buf = NULL;
    FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        error,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR)&lp_msg_buf,
        0,
        NULL
    );

    printf("  [!] ERROR: %s (Code: %lu): %s\n",
           context,
           (unsigned long)error,
           lp_msg_buf ? (const char*)lp_msg_buf : "<none>");
    if (lp_msg_buf) {
        LocalFree(lp_msg_buf);
    }
}

int utf16_to_utf8(const wchar_t* src, char* dst, int dst_len) {
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
