#pragma once

#include <TeensyThreads.h>

// Shared mutex to serialize all USB keyboard output operations (Keyboard.press/print/etc).
// We use it from both the physical key forwarder callbacks and the radio command injector.
Threads::Mutex& usbKeyboardMutex();

