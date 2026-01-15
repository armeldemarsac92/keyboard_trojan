#ifndef DEBUG_H
#define DEBUG_H

#include <cstdint>
#include <USBHost_t36.h>

// --- Configuration ---
// Comment this line out to disable all Serial printing
#define SHOW_KEYBOARD_DATA

// --- Function Declarations ---
void ShowUpdatedDeviceListInfo();
void PrintKeyPress(uint8_t keycode, bool mapped);
void PrintKeyRelease(uint8_t keycode);

#endif