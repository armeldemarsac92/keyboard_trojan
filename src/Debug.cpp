#include <Arduino.h>
#include <array>
#include <core_pins.h>
#include <usb_serial.h>

#include "Debug.h"
#include "Globals.h"

namespace {
std::array<USBDriver*, 4> drivers{&hub1, &hub2, &hid1, &hid2};
constexpr std::array<const char*, 4> driverNames{"Hub1", "Hub2", "HID1", "HID2"};
std::array<bool, 4> driverActive{};

std::array<USBHIDInput*, 2> hidDrivers{&keyboard1, &keyboard2};
constexpr std::array<const char*, 2> hidDriverNames{"Keyboard1", "Keyboard2"};
std::array<bool, 2> hidDriverActive{};
}


void ShowUpdatedDeviceListInfo() {
#ifdef SHOW_KEYBOARD_DATA
  // Generic Devices
  for (std::size_t i = 0; i < drivers.size(); ++i) {
    if (*drivers[i] != driverActive[i]) {
      if (driverActive[i]) {
        Serial.printf("*** Device %s - disconnected ***\n", driverNames[i]);
        driverActive[i] = false;
      } else {
        Serial.printf("*** Device %s %x:%x - connected ***\n", driverNames[i], drivers[i]->idVendor(), drivers[i]->idProduct());
        driverActive[i] = true;
      }
    }
  }

  // HID Devices (Visual Feedback on LED Pin 13)
  for (std::size_t i = 0; i < hidDrivers.size(); ++i) {
    if (*hidDrivers[i] != hidDriverActive[i]) {
      if (hidDriverActive[i]) {
        Serial.printf("*** HID Device %s - disconnected ***\n", hidDriverNames[i]);
        hidDriverActive[i] = false;
        digitalWrite(13, LOW);
      } else {
        Serial.printf("*** HID Device %s %x:%x - connected ***\n", hidDriverNames[i], hidDrivers[i]->idVendor(), hidDrivers[i]->idProduct());
        hidDriverActive[i] = true;
        digitalWrite(13, HIGH);
      }
    }
  }
#endif
}

void PrintKeyPress(uint8_t keycode, bool mapped) {
#ifdef SHOW_KEYBOARD_DATA
  Serial.print("Press: ");
  Serial.print(keycode, HEX);
  if (mapped) Serial.print(" [Mapped Modifier]");
  Serial.println();
#endif
}

void PrintKeyRelease(uint8_t keycode) {
#ifdef SHOW_KEYBOARD_DATA
  Serial.print("Release: ");
  Serial.println(keycode, HEX);
#endif
}
