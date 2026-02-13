#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>
#include <TeensyThreads.h>

#include <cstdint>
#include <utility>

// Build-time toggle:
// -D KEYBOARD_ENABLE_LOGGING=1 (default) or =0 to compile out all logger calls.
#ifndef KEYBOARD_ENABLE_LOGGING
#define KEYBOARD_ENABLE_LOGGING 1
#endif

class Logger final {
public:
    static Logger& instance() {
        static Logger inst;
        return inst;
    }

    static constexpr bool enabled() {
        return KEYBOARD_ENABLE_LOGGING != 0;
    }

    void begin(std::uint32_t baud) {
        if constexpr (enabled()) {
            Serial.begin(baud);
        } else {
            (void)baud;
        }
    }

    void println() {
        if constexpr (enabled()) {
            Threads::Scope lock(mutex_);
            Serial.println();
        }
    }

    template <typename T>
    void print(T&& value) {
        if constexpr (enabled()) {
            Threads::Scope lock(mutex_);
            Serial.print(std::forward<T>(value));
        }
    }

    template <typename T>
    void println(T&& value) {
        if constexpr (enabled()) {
            Threads::Scope lock(mutex_);
            Serial.println(std::forward<T>(value));
        }
    }

    template <typename T>
    void print(T&& value, int format) {
        if constexpr (enabled()) {
            Threads::Scope lock(mutex_);
            Serial.print(std::forward<T>(value), format);
        } else {
            (void)value;
            (void)format;
        }
    }

    template <typename T>
    void println(T&& value, int format) {
        if constexpr (enabled()) {
            Threads::Scope lock(mutex_);
            Serial.println(std::forward<T>(value), format);
        } else {
            (void)value;
            (void)format;
        }
    }

    template <typename... Args>
    void printf(const char* fmt, Args&&... args) {
        if constexpr (enabled()) {
            Threads::Scope lock(mutex_);
            Serial.printf(fmt, std::forward<Args>(args)...);
        } else {
            (void)fmt;
            ((void)args, ...);
        }
    }

private:
    Logger() = default;

    Threads::Mutex mutex_{};
};

#endif
