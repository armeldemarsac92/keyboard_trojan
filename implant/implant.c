#include <windows.h>
#include <stdio.h>
#include <hidapi.h>
#include <stdlib.h>
#include <string.h>

#include "hidapi.h"

#define MAX_STR 255

// Helper to log detailed Windows system errors to your log file
void log_win_error(const char* context) {
    DWORD error = GetLastError();
    LPVOID lpMsgBuf;
    FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)&lpMsgBuf, 0, NULL
    );
    printf("  [!] ERROR: %s (Code: %lu): %s\n", context, error, (char*)lpMsgBuf);
    LocalFree(lpMsgBuf);
}

int main(int argc, char* argv[]) {
    // FORCE AUTO-FLUSH: Ensures printf data is written to the log file immediately
    setvbuf(stdout, NULL, _IONBF, 0);

    hid_device *handle = NULL;
    struct hid_device_info *devs, *cur_dev;
    unsigned char buf[65];
    wchar_t window_title_w[MAX_STR];
    char last_title[MAX_STR] = "";

    if (hid_init()) {
        printf("FATAL: Failed to initialize HIDAPI.\n");
        return -1;
    }

    printf("--- HID Enumeration Started ---\n");

    // Scan for all PJRC/Teensy devices (VID 0x16C0)
    devs = hid_enumerate(0x16C0, 0x0000);
    cur_dev = devs;

    if (!cur_dev) {
        printf("No Teensy devices found. Ensure USB Passthrough is active in VirtualBox.\n");
    }

    // Print all found interfaces to help you identify the correct MI index
    while (cur_dev) {
        printf("\n[DEVICE IDENTIFIED]\n");
        printf("  Product:      %ls\n", cur_dev->product_string);
        printf("  VID/PID:      %04x:%04x\n", cur_dev->vendor_id, cur_dev->product_id);
        printf("  Interface #:  %d\n", cur_dev->interface_number);
        printf("  Usage Page:   0x%04x\n", cur_dev->usage_page);
        printf("  Usage:        0x%04x\n", cur_dev->usage);
        printf("  Path:         %s\n", cur_dev->path);

        // Target Interface 2 (MI_02) which we modified in the Teensy usb.c
        if (cur_dev->interface_number == 2 && handle == NULL) {
            printf("  >>> MATCH FOUND (MI_02). Attempting to open handle...\n");
            handle = hid_open_path(cur_dev->path);
            if (handle) {
                printf("  >>> SUCCESS: Connection established to Secret Channel.\n");
            } else {
                log_win_error("hid_open_path");
            }
        }
        cur_dev = cur_dev->next;
    }
    hid_free_enumeration(devs);

    if (!handle) {
        printf("\nFAILED: Could not connect to Teensy MI_02. Check Admin rights or PID.\n");
        hid_exit();
        return -1;
    }

    printf("\n--- Monitoring Foreground Windows ---\n");

    while (1) {
        // Get the handle of the window currently in focus
        HWND hwnd = GetForegroundWindow();
        if (hwnd) {
            // Retrieve the window title in Wide (UTF-16) format
            if (GetWindowTextW(hwnd, window_title_w, MAX_STR) > 0) {

                // Convert to standard C-string for comparison and transmission
                char current_title[MAX_STR];
                wcstombs(current_title, window_title_w, MAX_STR);

                // Only send data if the title has changed (prevents flooding)
                if (strcmp(current_title, last_title) != 0) {
                    strncpy(last_title, current_title, MAX_STR);

                    // Prepare the 65-byte HID buffer (1 byte Report ID + 64 bytes Payload)
                    memset(buf, 0, sizeof(buf));
                    buf[0] = 0x01; // Report ID 1 (matches 0x0301 in Teensy stack)

                    // Copy title starting at byte 1
                    strncpy((char*)&buf[1], current_title, 64);

                    printf("Sending Title: [%s] ... ", current_title);

                    // Send the Feature Report to the Teensy Keyboard interface
                    int res = hid_send_feature_report(handle, buf, 65);

                    if (res < 0) {
                        printf("FAILED: %ls\n", hid_error(handle));
                    } else {
                        printf("OK (%d bytes)\n", res);
                    }
                }
            }
        }
        // Poll every 500ms to keep CPU usage low
        Sleep(500);
    }

    hid_close(handle);
    hid_exit();
    return 0;
}