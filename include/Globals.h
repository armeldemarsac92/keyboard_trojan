#ifndef GLOBALS_H
#define GLOBALS_H

#include <USBHost_t36.h>
#include <TeensyThreads.h>

// --- Global Object Declarations ---
// These allow all files to access the USB devices
extern USBHost myusb;
extern USBHub hub1;
extern USBHub hub2;
extern KeyboardController keyboard1;
extern KeyboardController keyboard2;
extern USBHIDParser hid1;
extern USBHIDParser hid2;

#endif