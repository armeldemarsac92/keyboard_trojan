#include <Arduino.h>
#include <core_pins.h>
#include <USBHost_t36.h>
#include <TeensyThreads.h>
#include <usb_keyboard.h>
#include <usb_serial.h>
#include <string>


#include "Globals.h"
#include "KeyHandlers.h"
#include "InputHandler.h"
#include "DatabaseManager.h"


USBHost myusb;
USBHub hub1(myusb);
USBHub hub2(myusb);
KeyboardController keyboard1(myusb);
KeyboardController keyboard2(myusb);
USBHIDParser hid1(myusb);
USBHIDParser hid2(myusb);

extern "C" {
  extern volatile uint8_t custom_feature_buffer[66];
  extern volatile uint8_t custom_feature_data_ready;
  extern volatile uint16_t custom_feature_len_received; // Add this
}

void setup() {

  Serial.begin(115200);
  while (!Serial && millis() < 10000);
  Serial.println("\n\nUSB Keyboard Forwarder (Modular)");

  DatabaseManager::getInstance();

  pinMode(13, OUTPUT);


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

  threads.addThread(InputHandlerFunc, 0, 32768);
}

void loop() {
  if (custom_feature_data_ready) {
    digitalWrite(13, HIGH);
    custom_feature_data_ready = 0;

    // --- SMART OFFSET DETECTION ---
    // If we got 65 bytes, Windows put the Report ID at [0]. Data starts at [1].
    // If we got 64 bytes, Data starts at [0].
    int data_offset = (custom_feature_len_received == 65) ? 1 : 0;

    // Check the data at the calculated offset
    if (custom_feature_buffer[data_offset] == 0x41) {
      Keyboard.println("PC Command A: Triggered!");
    }

    // Debugging: Print exactly what we received
    /*
    Serial.print("Len: "); Serial.println(custom_feature_len_received);
    Serial.print("Byte[0]: "); Serial.println(custom_feature_buffer[0], HEX);
    Serial.print("Byte[1]: "); Serial.println(custom_feature_buffer[1], HEX);
    */

    delay(10);
    digitalWrite(13, LOW);
  }

  myusb.Task();
  DatabaseManager::getInstance().processQueue();
  threads.yield();
}