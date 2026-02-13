#pragma once

#include <TeensyThreads.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

// Inject text into the connected host via USB HID keyboard output.
// This is used by the RAK radio command protocol.
class HostKeyboard final {
public:
    static HostKeyboard& instance();

    // Request that text is typed on the host.
    // Thread-safe. Returns false if a job is already queued or in progress.
    bool enqueueTypeText(std::string_view text);

    // True if a job is queued or being typed.
    bool isBusy() const;

    // Call frequently from the main Arduino loop context.
    // Types a small chunk per call to avoid blocking USBHost processing.
    void tick();

private:
    HostKeyboard() = default;

    void typeNextChunk_(std::size_t maxChars);

    static constexpr std::size_t kMaxTypeChars = 220;
    static constexpr std::size_t kCharsPerTick = 8;

    mutable Threads::Mutex mutex_{};
    std::array<char, kMaxTypeChars + 1> text_{};
    std::size_t len_ = 0;
    std::size_t index_ = 0;
    std::size_t typed_ = 0;
    std::size_t skipped_ = 0;
    bool active_ = false;
};
