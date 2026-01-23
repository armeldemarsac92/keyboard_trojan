#pragma once
#include <cstdint>
#include <string>

namespace AzertyLayout {
    char mapKeyToAscii(uint8_t code, uint8_t modifiers);
    std::string getShortcutName(uint8_t code, uint8_t modifiers);
}