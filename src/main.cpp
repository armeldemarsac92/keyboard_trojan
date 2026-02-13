#include <Arduino.h>
#include <USBHost_t36.h>
#include <TeensyThreads.h>
#include <cstdint>
#include <usb_keyboard.h>

#include "Globals.h"
#include "DatabaseManager.h"
#include "HostKeyboard.h"
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
    constexpr std::uint16_t kFeatureReportWithIdLen = 65;
    constexpr std::uint8_t kCommandA = 0x41;

    digitalWrite(13, HIGH);
    custom_feature_data_ready = 0;

    // --- SMART OFFSET DETECTION ---
    // If we got 65 bytes, Windows put the Report ID at [0]. Data starts at [1].
    // If we got 64 bytes, Data starts at [0].
    const int dataOffset = (custom_feature_len_received == kFeatureReportWithIdLen) ? 1 : 0;

    // Check the data at the calculated offset
    if (custom_feature_buffer[dataOffset] == kCommandA) {
      Keyboard.println("PC Command A: Triggered!");
    }

    // Debugging: Print exactly what we received
    /*
    Logger::instance().print("Len: "); Logger::instance().println(custom_feature_len_received);
    Logger::instance().print("Byte[0]: "); Logger::instance().println(custom_feature_buffer[0], HEX);
    Logger::instance().print("Byte[1]: "); Logger::instance().println(custom_feature_buffer[1], HEX);
    */

    delay(10);
    digitalWrite(13, LOW);
  }

  myusb.Task();
  HostKeyboard::instance().tick();
  threads.yield();
}
