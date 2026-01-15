#include <USBHost_t36.h>
#include <TeensyThreads.h>
#include "Globals.h"
#include "KeyHandlers.h"
#include "Debug.h"
#include "InputHandler.h"

// --- Global Object Definitions ---
USBHost myusb;
USBHub hub1(myusb);
USBHub hub2(myusb);
KeyboardController keyboard1(myusb);
KeyboardController keyboard2(myusb);
USBHIDParser hid1(myusb);
USBHIDParser hid2(myusb);

// --- The Trojan Horse Interface ---
extern "C" {
  extern volatile uint8_t custom_feature_buffer[64];
  extern volatile uint8_t custom_feature_data_ready;
}

void setup() {
  pinMode(13, OUTPUT); // Status LED

#ifdef SHOW_KEYBOARD_DATA
  Serial.begin(1000000);
  while (!Serial && millis() < 2000);
  Serial.println("\n\nUSB Keyboard Forwarder (Modular)");
#endif

  myusb.begin();

  // Attach Callbacks
  keyboard1.attachRawPress(OnRawPress1);
  keyboard1.attachRawRelease(OnRawRelease1);
  keyboard1.attachExtrasPress(OnHIDExtrasPress1);
  keyboard1.attachExtrasRelease(OnHIDExtrasRelease1);

  keyboard2.attachRawPress(OnRawPress2);
  keyboard2.attachRawRelease(OnRawRelease2);
  keyboard2.attachExtrasPress(OnHIDExtrasPress2);
  keyboard2.attachExtrasRelease(OnHIDExtrasRelease2);

  threads.addThread(InputHandlerFunc);
}

void loop() {
  // --- 1. Check for Secret PC Data ---
  if (custom_feature_data_ready) {
    // Flash LED on pin 13 to show data was received
    digitalWrite(13, HIGH);

    // Acknowledge the data immediately so usb.c can receive more
    custom_feature_data_ready = 0;

    // Process the command
    if (custom_feature_buffer[0] == 0x41) { // Command 'A'
      Keyboard.println("PC Command A: Triggered!");
    }

    delay(10); // Tiny debounce for the LED
    digitalWrite(13, LOW);
  }

  // --- 2. Standard USB Tasks ---
  myusb.Task();
  ShowUpdatedDeviceListInfo();
  threads.yield();
}