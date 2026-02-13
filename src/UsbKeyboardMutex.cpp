#include "UsbKeyboardMutex.h"

Threads::Mutex& usbKeyboardMutex() {
    static Threads::Mutex m{};
    return m;
}

