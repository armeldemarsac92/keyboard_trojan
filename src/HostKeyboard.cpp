#include "HostKeyboard.h"

#include <algorithm>
#include <array>
#include <cstdint>

#include <keylayouts.h>
#include <usb_keyboard.h>

#include "Logger.h"

namespace {
constexpr std::uint16_t kRawKeycodePrefix = 0xF000;

struct KeyCombo {
    std::uint8_t keycode = 0;
    std::uint8_t mods = 0;
    bool valid = false;
};

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

KeyCombo mapFrenchAzertyLetter(char lower) {
    // USB HID usage codes correspond to key positions. A French AZERTY OS layout maps those
    // usages to characters. For injection we must use the correct usage codes for AZERTY.
    //
    // Only a few letters differ from the "US QWERTY identity mapping":
    // - a <-> q (usage 20 <-> 4)
    // - z <-> w (usage 26 <-> 29)
    // - m is on usage 51 (US ';' key). Usage 16 is ',' on FR.
    switch (lower) {
        case 'a': return KeyCombo{20, 0, true};  // US 'q' key
        case 'q': return KeyCombo{4, 0, true};   // US 'a' key
        case 'z': return KeyCombo{26, 0, true};  // US 'w' key
        case 'w': return KeyCombo{29, 0, true};  // US 'z' key
        case 'm': return KeyCombo{51, 0, true};  // US ';' key
        default:
            if (lower >= 'a' && lower <= 'z') {
                const std::uint8_t code = static_cast<std::uint8_t>(4 + (lower - 'a'));
                return KeyCombo{code, 0, true};
            }
            return KeyCombo{};
    }
}

KeyCombo mapFrenchAzertyCodepoint(std::uint32_t cp) {
    // Controls
    switch (cp) {
        case '\n':
        case '\r':
            return KeyCombo{40, 0, true};  // Enter
        case '\t':
            return KeyCombo{43, 0, true};  // Tab
        case ' ':
            return KeyCombo{44, 0, true};  // Space
        case '\b':
            return KeyCombo{42, 0, true};  // Backspace
        default:
            break;
    }

    // ASCII letters
    if (cp >= 'a' && cp <= 'z') {
        return mapFrenchAzertyLetter(static_cast<char>(cp));
    }
    if (cp >= 'A' && cp <= 'Z') {
        auto combo = mapFrenchAzertyLetter(static_cast<char>(cp - 'A' + 'a'));
        combo.mods = static_cast<std::uint8_t>(combo.mods | 0x02);  // shift
        return combo;
    }

    // Digits (French AZERTY: digits are shifted on the top row)
    if (cp >= '1' && cp <= '9') {
        const std::uint8_t code = static_cast<std::uint8_t>(30 + (cp - '1'));
        return KeyCombo{code, 0x02, true};  // shift
    }
    if (cp == '0') {
        return KeyCombo{39, 0x02, true};  // shift + '0' key
    }

    // Minimal punctuation (common in chat / phone keyboards)
    switch (cp) {
        case '\'':
            return KeyCombo{33, 0, true};  // top row 4 key on FR yields apostrophe
        case ',':
            return KeyCombo{16, 0, true};  // FR: ',' on US 'm' key
        case '.':
            return KeyCombo{55, 0, true};  // common placement; may vary by OS settings
        case '-':
            return KeyCombo{35, 0, true};  // '-' on FR top row (no shift)
        default:
            break;
    }

    // Common French characters (UTF-8 codepoints).
    switch (cp) {
        case 0x00E9: return KeyCombo{31, 0, true};  // é
        case 0x00E8: return KeyCombo{36, 0, true};  // è
        case 0x00E0: return KeyCombo{39, 0, true};  // à
        case 0x00E7: return KeyCombo{38, 0, true};  // ç
        case 0x00F9: return KeyCombo{52, 0, true};  // ù
        case 0x00A0: return KeyCombo{44, 0, true};  // non-breaking space -> space
        case 0x2019: return KeyCombo{33, 0, true};  // ’ -> '
        case 0x2018: return KeyCombo{33, 0, true};  // ‘ -> '
        default:
            break;
    }

    return KeyCombo{};
}

bool typeCodepoint(std::uint32_t cp, std::size_t& typed, std::size_t& skipped) {
    const auto combo = mapFrenchAzertyCodepoint(cp);
    if (!combo.valid) {
        ++skipped;
        return false;
    }

    typeRawKey(combo.keycode, combo.mods);
    ++typed;
    return true;
}

bool decodeUtf8One(const char* bytes, std::size_t len, std::uint32_t& outCp, std::size_t& outConsumed) {
    if (bytes == nullptr || len == 0) {
        return false;
    }

    const auto b0 = static_cast<std::uint8_t>(bytes[0]);
    if (b0 < 0x80) {
        outCp = b0;
        outConsumed = 1;
        return true;
    }

    auto isCont = [](std::uint8_t b) { return (b & 0xC0u) == 0x80u; };

    if ((b0 & 0xE0u) == 0xC0u) {
        if (len < 2) return false;
        const auto b1 = static_cast<std::uint8_t>(bytes[1]);
        if (!isCont(b1)) return false;
        const std::uint32_t cp = ((b0 & 0x1Fu) << 6) | (b1 & 0x3Fu);
        if (cp < 0x80u) return false;  // overlong
        outCp = cp;
        outConsumed = 2;
        return true;
    }

    if ((b0 & 0xF0u) == 0xE0u) {
        if (len < 3) return false;
        const auto b1 = static_cast<std::uint8_t>(bytes[1]);
        const auto b2 = static_cast<std::uint8_t>(bytes[2]);
        if (!isCont(b1) || !isCont(b2)) return false;
        const std::uint32_t cp = ((b0 & 0x0Fu) << 12) | ((b1 & 0x3Fu) << 6) | (b2 & 0x3Fu);
        if (cp < 0x800u) return false;  // overlong
        if (cp >= 0xD800u && cp <= 0xDFFFu) return false;  // surrogate range
        outCp = cp;
        outConsumed = 3;
        return true;
    }

    if ((b0 & 0xF8u) == 0xF0u) {
        if (len < 4) return false;
        const auto b1 = static_cast<std::uint8_t>(bytes[1]);
        const auto b2 = static_cast<std::uint8_t>(bytes[2]);
        const auto b3 = static_cast<std::uint8_t>(bytes[3]);
        if (!isCont(b1) || !isCont(b2) || !isCont(b3)) return false;
        const std::uint32_t cp =
            ((b0 & 0x07u) << 18) | ((b1 & 0x3Fu) << 12) | ((b2 & 0x3Fu) << 6) | (b3 & 0x3Fu);
        if (cp < 0x10000u || cp > 0x10FFFFu) return false;
        outCp = cp;
        outConsumed = 4;
        return true;
    }

    return false;
}

std::size_t utf8TruncateToWholeCodepoints(std::string_view text, std::size_t maxBytes) {
    std::size_t i = 0;
    const std::size_t limit = std::min<std::size_t>(text.size(), maxBytes);
    while (i < limit) {
        std::uint32_t cp = 0;
        std::size_t consumed = 0;
        const std::size_t remaining = limit - i;
        if (!decodeUtf8One(text.data() + i, remaining, cp, consumed)) {
            // Keep invalid bytes as-is (they'll be skipped during typing); just avoid infinite loops.
            ++i;
            continue;
        }
        if (i + consumed > limit) {
            break;
        }
        i += consumed;
    }
    return i;
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
    if (!active_) {
        startJobLocked_(text);
        return true;
    }

    if (queueCount_ >= kQueueSize) {
        return false;
    }

    auto& slot = queue_[queueTail_];
    slot.len = utf8TruncateToWholeCodepoints(text, kMaxTypeChars);
    for (std::size_t i = 0; i < slot.len; ++i) {
        slot.text[i] = text[i];
    }
    slot.text[slot.len] = '\0';

    queueTail_ = (queueTail_ + 1) % kQueueSize;
    ++queueCount_;
    return true;
}

bool HostKeyboard::isBusy() const {
    Threads::Scope lock(mutex_);
    return active_ || (queueCount_ > 0);
}

void HostKeyboard::cancelAll() {
    Threads::Scope lock(mutex_);
    active_ = false;
    len_ = 0;
    index_ = 0;
    typed_ = 0;
    skipped_ = 0;
    queueHead_ = 0;
    queueTail_ = 0;
    queueCount_ = 0;
}

void HostKeyboard::tick() {
    typeNextChunk_(kCharsPerTick);
}

void HostKeyboard::typeNextChunk_(std::size_t maxChars) {
    Threads::Scope lock(mutex_);
    if (!active_ && queueCount_ > 0) {
        const auto& slot = queue_[queueHead_];
        startJobLocked_(std::string_view(slot.text.data(), slot.len));

        queueHead_ = (queueHead_ + 1) % kQueueSize;
        --queueCount_;
    }

    if (!active_) {
        return;
    }

    const std::size_t remaining = (index_ < len_) ? (len_ - index_) : 0;
    const std::size_t todo = std::min<std::size_t>(remaining, maxChars);
    std::size_t codepoints = 0;
    while (codepoints < todo && index_ < len_) {
        std::uint32_t cp = 0;
        std::size_t consumed = 0;
        const std::size_t bytesRemaining = len_ - index_;
        if (!decodeUtf8One(text_.data() + index_, bytesRemaining, cp, consumed)) {
            ++skipped_;
            ++index_;
            continue;
        }
        index_ += consumed;
        (void)typeCodepoint(cp, typed_, skipped_);
        ++codepoints;
    }

    if (index_ >= len_) {
        Logger::instance().printf("[HOSTKBD] typeText layout=AZERTY typed=%u skipped=%u len=%u\n",
                                  static_cast<unsigned>(typed_),
                                  static_cast<unsigned>(skipped_),
                                  static_cast<unsigned>(len_));
        active_ = false;
        len_ = 0;
        index_ = 0;
        typed_ = 0;
        skipped_ = 0;
    }
}

void HostKeyboard::startJobLocked_(std::string_view text) {
    len_ = utf8TruncateToWholeCodepoints(text, kMaxTypeChars);
    for (std::size_t i = 0; i < len_; ++i) {
        text_[i] = text[i];
    }
    text_[len_] = '\0';
    index_ = 0;
    typed_ = 0;
    skipped_ = 0;
    active_ = true;
}
