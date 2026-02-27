#include "HidBridge.h"

#include <Arduino.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include <usb_keyboard.h>

#include "Logger.h"

extern "C" {
extern volatile uint8_t custom_feature_reply_buffer[66];
extern volatile uint8_t custom_feature_reply_data_ready;
extern volatile uint16_t custom_feature_reply_len_received;
}

namespace {
constexpr std::uint8_t kLegacyCommandA = 0x41;
constexpr std::size_t kMaxLogChars = 72;

std::string_view trimAscii(std::string_view text) {
    std::size_t begin = 0;
    std::size_t end = text.size();

    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }

    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }

    return text.substr(begin, end - begin);
}

std::string decodeTextPayload(const std::uint8_t* data, std::size_t len) {
    std::string out;
    out.reserve(len);

    for (std::size_t i = 0; i < len; ++i) {
        const char c = static_cast<char>(data[i]);
        if (c == '\0') {
            break;
        }

        const auto uc = static_cast<unsigned char>(c);
        const bool isControl = (uc < 0x20u) && c != '\t' && c != '\n' && c != '\r';
        if (isControl) {
            continue;
        }

        out.push_back(c);
    }

    const auto trimmed = trimAscii(out);
    return std::string(trimmed);
}

bool startsWithIgnoreCase(std::string_view text, std::string_view prefix) {
    if (prefix.size() > text.size()) {
        return false;
    }

    for (std::size_t i = 0; i < prefix.size(); ++i) {
        const char a = static_cast<char>(std::toupper(static_cast<unsigned char>(text[i])));
        const char b = static_cast<char>(std::toupper(static_cast<unsigned char>(prefix[i])));
        if (a != b) {
            return false;
        }
    }

    return true;
}
}  // namespace

HidBridge& HidBridge::instance() {
    static HidBridge inst;
    return inst;
}

std::string HidBridge::activeWindowSnapshot() {
    Threads::Scope scope(mutex_);
    return activeWindowTitle_;
}

bool HidBridge::publishCommandForAgent(std::string_view command) {
    command = trimAscii(command);
    if (command.empty()) {
        return false;
    }

    std::array<std::uint8_t, 66> local{};
    local[0] = HidBridgeProtocol::kReportIdCommand;
    const std::size_t payloadMax = 64;
    const std::size_t copyLen = std::min(command.size(), payloadMax);
    std::memcpy(local.data() + 1, command.data(), copyLen);

    noInterrupts();
    custom_feature_reply_data_ready = 0;
    for (std::size_t i = 0; i < local.size(); ++i) {
        custom_feature_reply_buffer[i] = local[i];
    }
    custom_feature_reply_len_received = 65;
    custom_feature_reply_data_ready = 1;
    interrupts();

    Logger::instance().printf("[HID] queued agent command len=%u\n", static_cast<unsigned>(copyLen));
    return true;
}

void HidBridge::updateWindowTitle_(std::string title) {
    if (title.empty()) {
        return;
    }

    bool updated = false;
    {
        Threads::Scope scope(mutex_);
        if (activeWindowTitle_ != title) {
            activeWindowTitle_ = std::move(title);
            lastWindowUpdateMs_ = millis();
            updated = true;
        }
    }

    if (updated) {
        const std::string snapshot = activeWindowSnapshot();
        const std::size_t shown = std::min(snapshot.size(), kMaxLogChars);
        Logger::instance().printf("[HID] active_window=\"%.*s%s\"\n",
                                  static_cast<int>(shown),
                                  snapshot.c_str(),
                                  snapshot.size() > shown ? "..." : "");
    }
}

void HidBridge::handleCommand_(const std::string& command) {
    if (command.empty()) {
        return;
    }

    if (startsWithIgnoreCase(command, "PING")) {
        Logger::instance().println("[HID][CMD] PING");
        return;
    }

    if (startsWithIgnoreCase(command, "CLEAR_WINDOW")) {
        Threads::Scope scope(mutex_);
        activeWindowTitle_.clear();
        lastWindowUpdateMs_ = millis();
        Logger::instance().println("[HID][CMD] active window cleared");
        return;
    }

    constexpr std::string_view kSetWindowColon = "SET_WINDOW:";
    constexpr std::string_view kSetWindowSpace = "SET_WINDOW ";
    if (startsWithIgnoreCase(command, kSetWindowColon)) {
        updateWindowTitle_(std::string(trimAscii(command.substr(kSetWindowColon.size()))));
        return;
    }
    if (startsWithIgnoreCase(command, kSetWindowSpace)) {
        updateWindowTitle_(std::string(trimAscii(command.substr(kSetWindowSpace.size()))));
        return;
    }

    if (startsWithIgnoreCase(command, "CMD_A")) {
        Keyboard.println("PC Command A: Triggered!");
        Logger::instance().println("[HID][CMD] CMD_A executed");
        return;
    }

    const std::size_t shown = std::min(command.size(), kMaxLogChars);
    Logger::instance().printf("[HID][CMD] unknown=\"%.*s%s\"\n",
                              static_cast<int>(shown),
                              command.c_str(),
                              command.size() > shown ? "..." : "");
}

void HidBridge::processFeatureReport(const volatile std::uint8_t* buffer, std::uint16_t len) {
    if (buffer == nullptr) {
        return;
    }

    if (len != HidBridgeProtocol::kFeatureReportLenNoId && len != HidBridgeProtocol::kFeatureReportLenWithId) {
        Logger::instance().printf("[HID] ignored feature report with unexpected len=%u\n",
                                  static_cast<unsigned>(len));
        return;
    }

    std::array<std::uint8_t, HidBridgeProtocol::kFeatureReportLenWithId> local{};
    const std::size_t safeLen = std::min<std::size_t>(len, local.size());
    for (std::size_t i = 0; i < safeLen; ++i) {
        local[i] = buffer[i];
    }

    const bool hasReportId = (len == HidBridgeProtocol::kFeatureReportLenWithId);
    const std::size_t payloadOffset = hasReportId ? 1U : 0U;
    const std::uint8_t reportId = hasReportId ? local[0] : 0U;

    if (safeLen <= payloadOffset) {
        return;
    }

    const auto* payload = local.data() + payloadOffset;
    const std::size_t payloadLen = safeLen - payloadOffset;

    if (!hasReportId && payload[0] == kLegacyCommandA) {
        Keyboard.println("PC Command A: Triggered!");
        Logger::instance().println("[HID] legacy CMD_A executed");
        return;
    }

    if (reportId == HidBridgeProtocol::kReportIdWindowChunkStart) {
        Threads::Scope scope(mutex_);
        windowChunkBuffer_.clear();
        windowChunkInProgress_ = true;
        windowChunkTruncated_ = false;
        return;
    }

    if (reportId == HidBridgeProtocol::kReportIdWindowChunkData) {
        if (payloadLen == 0) {
            return;
        }

        const std::size_t requestedBytes = std::min<std::size_t>(
            static_cast<std::size_t>(payload[0]),
            payloadLen > 0 ? payloadLen - 1 : 0);
        if (requestedBytes == 0) {
            return;
        }

        Threads::Scope scope(mutex_);
        if (!windowChunkInProgress_) {
            windowChunkBuffer_.clear();
            windowChunkInProgress_ = true;
            windowChunkTruncated_ = false;
        }

        const std::size_t roomLeft =
            (windowChunkBuffer_.size() < HidBridgeProtocol::kMaxWindowTitleBytes)
                ? (HidBridgeProtocol::kMaxWindowTitleBytes - windowChunkBuffer_.size())
                : 0U;
        const std::size_t copyLen = std::min(roomLeft, requestedBytes);
        if (copyLen > 0) {
            windowChunkBuffer_.append(reinterpret_cast<const char*>(payload + 1), copyLen);
        }

        if (copyLen < requestedBytes) {
            windowChunkTruncated_ = true;
        }
        return;
    }

    if (reportId == HidBridgeProtocol::kReportIdWindowChunkEnd) {
        std::string assembled;
        bool truncated = false;
        {
            Threads::Scope scope(mutex_);
            if (!windowChunkInProgress_) {
                return;
            }
            assembled = windowChunkBuffer_;
            truncated = windowChunkTruncated_;
            windowChunkBuffer_.clear();
            windowChunkInProgress_ = false;
            windowChunkTruncated_ = false;
        }

        const auto trimmed = trimAscii(assembled);
        if (!trimmed.empty()) {
            updateWindowTitle_(std::string(trimmed));
            if (truncated) {
                Logger::instance().printf("[HID] active window title truncated at %u bytes\n",
                                          static_cast<unsigned>(HidBridgeProtocol::kMaxWindowTitleBytes));
            }
        }
        return;
    }

    const std::string text = decodeTextPayload(payload, payloadLen);
    if (text.empty()) {
        return;
    }

    switch (reportId) {
        case 0U:
        case HidBridgeProtocol::kReportIdWindow:
        case HidBridgeProtocol::kLegacyReportIdWindow:
            updateWindowTitle_(text);
            return;
        case HidBridgeProtocol::kReportIdCommand:
            handleCommand_(text);
            return;
        default:
            Logger::instance().printf("[HID] unknown report_id=%u\n", static_cast<unsigned>(reportId));
            return;
    }
}
