#include "HostKeyboard.h"

#include <algorithm>
#include <array>
#include <cstdint>

#include <keylayouts.h>
#include <usb_keyboard.h>

#include "AzertyLayout.h"
#include "Logger.h"

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

bool typeChar(char c, const std::array<KeyCombo, 256>& reverseMap, std::size_t& typed, std::size_t& skipped) {
    switch (c) {
        case '\n':
        case '\r':
            typeRawKey(40, 0);  // Enter
            ++typed;
            return true;
        case '\t':
            typeRawKey(43, 0);  // Tab
            ++typed;
            return true;
        case ' ':
            typeRawKey(44, 0);  // Space
            ++typed;
            return true;
        case '\b':
            typeRawKey(42, 0);  // Backspace
            ++typed;
            return true;
        default:
            break;
    }

    const auto idx = static_cast<unsigned char>(c);
    const auto combo = reverseMap[idx];
    if (!combo.valid) {
        ++skipped;
        return false;
    }

    typeRawKey(combo.keycode, combo.mods);
    ++typed;
    return true;
}
}  // namespace

HostKeyboard& HostKeyboard::instance() {
    static HostKeyboard inst;
    return inst;
}

bool HostKeyboard::enqueueTypeText(std::string_view text) {
    if (text.empty()) {
        return false;
    }

    Threads::Scope lock(mutex_);
    if (active_) {
        return false;
    }

    len_ = std::min<std::size_t>(text.size(), kMaxTypeChars);
    for (std::size_t i = 0; i < len_; ++i) {
        text_[i] = text[i];
    }
    text_[len_] = '\0';
    index_ = 0;
    typed_ = 0;
    skipped_ = 0;
    active_ = true;
    return true;
}

bool HostKeyboard::isBusy() const {
    Threads::Scope lock(mutex_);
    return active_;
}

void HostKeyboard::tick() {
    typeNextChunk_(kCharsPerTick);
}

void HostKeyboard::typeNextChunk_(std::size_t maxChars) {
    static const auto kReverse = buildAzertyReverseMap();

    Threads::Scope lock(mutex_);
    if (!active_) {
        return;
    }

    const std::size_t remaining = (index_ < len_) ? (len_ - index_) : 0;
    const std::size_t todo = std::min<std::size_t>(remaining, maxChars);
    for (std::size_t i = 0; i < todo; ++i) {
        const char c = text_[index_++];
        (void)typeChar(c, kReverse, typed_, skipped_);
    }

    if (index_ >= len_) {
        Logger::instance().printf("[HOSTKBD] typeText typed=%u skipped=%u len=%u\n",
                                  static_cast<unsigned>(typed_),
                                  static_cast<unsigned>(skipped_),
                                  static_cast<unsigned>(len_));
        active_ = false;
        len_ = 0;
        index_ = 0;
    }
}
