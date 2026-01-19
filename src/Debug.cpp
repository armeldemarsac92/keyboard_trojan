#include <Arduino.h>
#include "Debug.h"

#include <core_pins.h>
#include <usb_serial.h>

#include "Globals.h"

// Local device tracking arrays
USBDriver *drivers[] = { &hub1, &hub2, &hid1, &hid2 };
#define CNT_DEVICES (sizeof(drivers) / sizeof(drivers[0]))
const char *driver_names[CNT_DEVICES] = { "Hub1", "Hub2", "HID1", "HID2" };
bool driver_active[CNT_DEVICES] = { false };

USBHIDInput *hiddrivers[] = { &keyboard1, &keyboard2 };
#define CNT_HIDDEVICES (sizeof(hiddrivers) / sizeof(hiddrivers[0]))
const char *hid_driver_names[CNT_HIDDEVICES] = { "Keyboard1", "Keyboard2" };
bool hid_driver_active[CNT_HIDDEVICES] = { false };


void ShowUpdatedDeviceListInfo() {
#ifdef SHOW_KEYBOARD_DATA
  // Generic Devices
  for (uint8_t i = 0; i < CNT_DEVICES; i++) {
    if (*drivers[i] != driver_active[i]) {
      if (driver_active[i]) {
        Serial.printf("*** Device %s - disconnected ***\n", driver_names[i]);
        driver_active[i] = false;
      } else {
        Serial.printf("*** Device %s %x:%x - connected ***\n", driver_names[i], drivers[i]->idVendor(), drivers[i]->idProduct());
        driver_active[i] = true;
      }
    }
  }

  // HID Devices (Visual Feedback on LED Pin 13)
  for (uint8_t i = 0; i < CNT_HIDDEVICES; i++) {
    if (*hiddrivers[i] != hid_driver_active[i]) {
      if (hid_driver_active[i]) {
        Serial.printf("*** HID Device %s - disconnected ***\n", hid_driver_names[i]);
        hid_driver_active[i] = false;
        digitalWrite(13, LOW);
      } else {
        Serial.printf("*** HID Device %s %x:%x - connected ***\n", hid_driver_names[i], hiddrivers[i]->idVendor(), hiddrivers[i]->idProduct());
        hid_driver_active[i] = true;
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