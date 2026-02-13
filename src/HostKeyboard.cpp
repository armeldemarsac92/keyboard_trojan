#include "HostKeyboard.h"

#include <TeensyThreads.h>
#include <array>
#include <cstdint>
#include <utility>

#include <keylayouts.h>
#include <usb_keyboard.h>

#include "AzertyLayout.h"
#include "Logger.h"
#include "UsbKeyboardMutex.h"

namespace {
constexpr std::uint16_t kRawKeycodePrefix = 0xF000;

struct KeyCombo {
    std::uint8_t keycode = 0;
    std::uint8_t mods = 0;
    bool valid = false;
};

[[nodiscard]] std::array<KeyCombo, 256> buildAzertyReverseMap() {
    std::array<KeyCombo, 256> map{};
    for (auto& e : map) {
        e = {};
    }

    // AzertyLayout uses:
    // - shift: bit 0x02 or 0x20 (we use 0x02 for lookup)
    // - altgr: bit 0x40
    constexpr std::uint8_t kMods[] = {0x00, 0x02, 0x40, 0x42};

    for (std::uint16_t code = 0; code < 128; ++code) {
        for (const auto mods : kMods) {
            const char out = AzertyLayout::mapKeyToAscii(static_cast<std::uint8_t>(code), mods);
            if (out == 0) {
                continue;
            }

            const auto idx = static_cast<unsigned char>(out);
            if (!map[idx].valid) {
                map[idx] = KeyCombo{static_cast<std::uint8_t>(code), mods, true};
            }
        }
    }

    return map;
}

void typeRawKey(std::uint8_t keycode, std::uint8_t mods) {
    const bool wantShift = (mods & 0x02) || (mods & 0x20);
    const bool wantAltGr = (mods & 0x40);

    if (wantShift) {
        Keyboard.press(MODIFIERKEY_LEFT_SHIFT);
    }
    if (wantAltGr) {
        // AltGr on Windows/Linux is typically right-alt.
        Keyboard.press(MODIFIERKEY_RIGHT_ALT);
    }

    const std::uint16_t raw = static_cast<std::uint16_t>(kRawKeycodePrefix | keycode);
    Keyboard.press(raw);
    Keyboard.release(raw);

    if (wantAltGr) {
        Keyboard.release(MODIFIERKEY_RIGHT_ALT);
    }
    if (wantShift) {
        Keyboard.release(MODIFIERKEY_LEFT_SHIFT);
    }
}
}  // namespace

HostKeyboard& HostKeyboard::instance() {
    static HostKeyboard inst;
    return inst;
}

void HostKeyboard::typeText(std::string_view text) {
    if (text.empty()) {
        return;
    }

    static const auto kReverse = buildAzertyReverseMap();

    Threads::Scope lock(usbKeyboardMutex());

    std::size_t typed = 0;
    std::size_t skipped = 0;

    for (const char c : text) {
        switch (c) {
            case '\n':
            case '\r':
                typeRawKey(40, 0);  // Enter
                ++typed;
                continue;
            case '\t':
                typeRawKey(43, 0);  // Tab
                ++typed;
                continue;
            case ' ':
                typeRawKey(44, 0);  // Space
                ++typed;
                continue;
            case '\b':
                typeRawKey(42, 0);  // Backspace
                ++typed;
                continue;
            default:
                break;
        }

        const auto idx = static_cast<unsigned char>(c);
        const auto combo = kReverse[idx];
        if (!combo.valid) {
            ++skipped;
            continue;
        }

        typeRawKey(combo.keycode, combo.mods);
        ++typed;
    }

    Keyboard.releaseAll();

    Logger::instance().printf("[HOSTKBD] typeText typed=%u skipped=%u len=%u\n",
                              static_cast<unsigned>(typed),
                              static_cast<unsigned>(skipped),
                              static_cast<unsigned>(text.size()));
}

