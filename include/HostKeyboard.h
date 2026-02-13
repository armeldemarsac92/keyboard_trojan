#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

// Inject text into the connected host via USB HID keyboard output.
// This is used by the RAK radio command protocol.
class HostKeyboard final {
public:
    static HostKeyboard& instance();

    // Types text using raw keycodes (layout-agnostic on the device, host interprets layout).
    // Unsupported characters are skipped (and logged).
    void typeText(std::string_view text);

private:
    HostKeyboard() = default;
};

