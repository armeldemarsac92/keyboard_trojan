#include <Arduino.h>
#include <cstdint>
#include <core_pins.h>
#include <keylayouts.h>
#include <usb_keyboard.h>

#include "KeyHandlers.h"
#include "Globals.h"
#include "Queuing.h"
#include "UsbKeyboardMutex.h"

namespace {
constexpr std::uint16_t kRawKeycodePrefix = 0xF000;
constexpr std::uint16_t kMediaKeyPrefix = 0xE400;
constexpr std::uint32_t kConsumerControlUsagePage = 0x0c0000;
std::uint8_t keyboardLastLeds = 0;

[[nodiscard]] std::uint16_t mapModifier(const std::uint8_t keycode) {
    switch (keycode) {
        case 103: return MODIFIERKEY_LEFT_CTRL;
        case 104: return MODIFIERKEY_LEFT_SHIFT;
        case 105: return MODIFIERKEY_LEFT_ALT;
        case 106: return MODIFIERKEY_LEFT_GUI;
        case 107: return MODIFIERKEY_RIGHT_CTRL;
        case 108: return MODIFIERKEY_RIGHT_SHIFT;
        case 109: return MODIFIERKEY_RIGHT_ALT;
        case 110: return MODIFIERKEY_RIGHT_GUI;
        default:  return 0;
    }
}
}

// --- Press Implementation ---
void OnRawPress1(uint8_t keycode) { OnRawPress(keyboard1, keycode); }
void OnRawPress2(uint8_t keycode) { OnRawPress(keyboard2, keycode); }

void OnRawPress(KeyboardController &kbd, uint8_t keycode) {
  const std::uint32_t keyPressTS = micros();

  // 1. Sync LEDs (NumLock, CapsLock) across devices
  if (keyboard_leds != keyboardLastLeds) {
    keyboardLastLeds = keyboard_leds;
    keyboard1.LEDS(keyboard_leds);
    keyboard2.LEDS(keyboard_leds);
  }

  // 2. Check if this is a modifier key
  const std::uint16_t modKey = mapModifier(keycode);
  const bool isModifier = (modKey != 0);

  {
    Threads::Scope lock(usbKeyboardMutex());
    if (isModifier) {
      // If it is a modifier (like Super), press the Modifier Constant
      Keyboard.press(modKey);
    } else {
      // If standard key, press using Raw Keycode
      Keyboard.press(kRawKeycodePrefix | keycode);
    }
  }

  // PrintKeyPress(keycode, isModifier);
  // Preserve the modifier state from the originating keyboard controller.
  enqueue(keycode, kbd.getModifiers(), keyPressTS);
}

// --- Release Implementation ---
void OnRawRelease1(uint8_t keycode) { OnRawRelease(keyboard1, keycode); }
void OnRawRelease2(uint8_t keycode) { OnRawRelease(keyboard2, keycode); }

void OnRawRelease(KeyboardController &kbd, uint8_t keycode) {
  (void)kbd;
  const std::uint16_t modKey = mapModifier(keycode);

  {
    Threads::Scope lock(usbKeyboardMutex());
    if (modKey != 0) {
      Keyboard.release(modKey);
    } else {
      Keyboard.release(kRawKeycodePrefix | keycode);
    }
  }

  // PrintKeyRelease(keycode);
}

// --- Extra Keys (Media) Implementation ---
void OnHIDExtrasPress1(uint32_t top, uint16_t key) { OnHIDExtrasPress(keyboard1, top, key); }
void OnHIDExtrasPress2(uint32_t top, uint16_t key) { OnHIDExtrasPress(keyboard2, top, key); }

void OnHIDExtrasPress(KeyboardController &kbd, uint32_t top, uint16_t key) {
  (void)kbd;
  if (top == kConsumerControlUsagePage) { // Page 0x0C is Consumer Control
    Threads::Scope lock(usbKeyboardMutex());
    Keyboard.press(kMediaKeyPrefix | key);
  }
}

void OnHIDExtrasRelease1(uint32_t top, uint16_t key) { OnHIDExtrasRelease(keyboard1, top, key); }
void OnHIDExtrasRelease2(uint32_t top, uint16_t key) { OnHIDExtrasRelease(keyboard2, top, key); }

void OnHIDExtrasRelease(KeyboardController &kbd, uint32_t top, uint16_t key) {
  (void)kbd;
  if (top == kConsumerControlUsagePage) {
    Threads::Scope lock(usbKeyboardMutex());
    Keyboard.release(kMediaKeyPrefix | key);
  }
}
