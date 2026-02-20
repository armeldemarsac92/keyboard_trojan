#include <Arduino.h>
#include <USBHost_t36.h>
#include <TeensyThreads.h>
#include <cstdint>
#include <usb_keyboard.h>

#include "Globals.h"
#include "DatabaseManager.h"
#include "HostKeyboard.h"
#include "HidBridge.h"
#include "InputHandler.h"
#include "KeyHandlers.h"
#include "Logger.h"
#include "NlpManager.h"
#include "RakManager.h"

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

namespace {
void databaseWriterThread(void*) {
  while (true) {
    DatabaseManager::getInstance().processQueue();
    threads.delay(5);
  }
}
}  // namespace

void setup() {
  Logger::instance().begin(115200);
  // while (!Serial);
  Logger::instance().println("\n\nUSB Keyboard Forwarder (Modular)");

  DatabaseManager::getInstance();

  pinMode(13, OUTPUT);


  myusb.begin();
  NlpManager::getInstance().begin();

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
  threads.addThread(databaseWriterThread, nullptr, 8192);
  RakManager::getInstance().begin();
}

void loop() {
  if (custom_feature_data_ready) {
    digitalWrite(13, HIGH);
    const std::uint16_t receivedLen = custom_feature_len_received;
    custom_feature_data_ready = 0;
    HidBridge::instance().processFeatureReport(custom_feature_buffer, receivedLen);
    delay(1);
    digitalWrite(13, LOW);
  }

  myusb.Task();
  HostKeyboard::instance().tick();
  threads.yield();
}
