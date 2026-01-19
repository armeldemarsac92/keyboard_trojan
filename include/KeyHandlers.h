#ifndef KEYHANDLERS_H
#define KEYHANDLERS_H

#include <USBHost_t36.h>

// --- Function Declarations ---

// Raw Press Handlers
void OnRawPress1(uint8_t keycode);
void OnRawPress2(uint8_t keycode);
void OnRawPress(KeyboardController &kbd, uint8_t keycode);

// Raw Release Handlers
void OnRawRelease1(uint8_t keycode);
void OnRawRelease2(uint8_t keycode);
void OnRawRelease(KeyboardController &kbd, uint8_t keycode);

// Media/Extra Key Handlers
void OnHIDExtrasPress1(uint32_t top, uint16_t key);
void OnHIDExtrasPress2(uint32_t top, uint16_t key);
void OnHIDExtrasPress(KeyboardController &kbd, uint32_t top, uint16_t key);
void OnHIDExtrasRelease1(uint32_t top, uint16_t key);
void OnHIDExtrasRelease2(uint32_t top, uint16_t key);
void OnHIDExtrasRelease(KeyboardController &kbd, uint32_t top, uint16_t key);

#endif