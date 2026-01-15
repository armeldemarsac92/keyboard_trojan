#include "KeyHandlers.h"
#include "Globals.h"
#include "Debug.h"
#include "Queuing.h"

// Local state for tracking LEDs
uint8_t keyboard_last_leds1 = 0;

// Helper: Maps Raw USB Keycodes (103-110) to Teensy Modifier Constants
uint16_t mapModifier(uint8_t keycode) {
  switch (keycode) {
    case 103: return MODIFIERKEY_LEFT_CTRL;
    case 104: return MODIFIERKEY_LEFT_SHIFT;
    case 105: return MODIFIERKEY_LEFT_ALT;
    case 106: return MODIFIERKEY_LEFT_GUI;  // Super/Windows Key
    case 107: return MODIFIERKEY_RIGHT_CTRL;
    case 108: return MODIFIERKEY_RIGHT_SHIFT;
    case 109: return MODIFIERKEY_RIGHT_ALT;
    case 110: return MODIFIERKEY_RIGHT_GUI;
    default:  return 0; // Not a modifier
  }
}

// --- Press Implementation ---
void OnRawPress1(uint8_t keycode) { OnRawPress(keyboard1, keycode); }
void OnRawPress2(uint8_t keycode) { OnRawPress(keyboard2, keycode); }

void OnRawPress(KeyboardController &kbd, uint8_t keycode) {
  uint32_t keyPressTS = micros();
  // 1. Sync LEDs (NumLock, CapsLock) across devices
  if (keyboard_leds != keyboard_last_leds1) {
    keyboard_last_leds1 = keyboard_leds;
    keyboard1.LEDS(keyboard_leds);
    keyboard2.LEDS(keyboard_leds);
  }

  // 2. Check if this is a modifier key
  uint16_t modKey = mapModifier(keycode);
  bool isModifier = (modKey != 0);

  if (isModifier) {
    // If it is a modifier (like Super), press the Modifier Constant
    Keyboard.press(modKey);
  } else {
    // If standard key, press using Raw Keycode
    Keyboard.press(0XF000 | keycode);
  }

  // PrintKeyPress(keycode, isModifier);
  enqueue(keycode, keyboard1.getModifiers(), keyPressTS);
}

// --- Release Implementation ---
void OnRawRelease1(uint8_t keycode) { OnRawRelease(keyboard1, keycode); }
void OnRawRelease2(uint8_t keycode) { OnRawRelease(keyboard2, keycode); }

void OnRawRelease(KeyboardController &kbd, uint8_t keycode) {
  uint16_t modKey = mapModifier(keycode);

  if (modKey != 0) {
    Keyboard.release(modKey);
  } else {
    Keyboard.release(0XF000 | keycode);
  }

  // PrintKeyRelease(keycode);
}

// --- Extra Keys (Media) Implementation ---
void OnHIDExtrasPress1(uint32_t top, uint16_t key) { OnHIDExtrasPress(keyboard1, top, key); }
void OnHIDExtrasPress2(uint32_t top, uint16_t key) { OnHIDExtrasPress(keyboard2, top, key); }

void OnHIDExtrasPress(KeyboardController &kbd, uint32_t top, uint16_t key) {
  if (top == 0xc0000) { // Page 0x0C is Consumer Control
    Keyboard.press(0XE400 | key);
  }
}

void OnHIDExtrasRelease1(uint32_t top, uint16_t key) { OnHIDExtrasRelease(keyboard1, top, key); }
void OnHIDExtrasRelease2(uint32_t top, uint16_t key) { OnHIDExtrasRelease(keyboard2, top, key); }

void OnHIDExtrasRelease(KeyboardController &kbd, uint32_t top, uint16_t key) {
  if (top == 0xc0000) {
    Keyboard.release(0XE400 | key);
  }
}