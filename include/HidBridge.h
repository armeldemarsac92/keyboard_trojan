#ifndef HIDBRIDGE_H
#define HIDBRIDGE_H

#include <TeensyThreads.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace HidBridgeProtocol {
inline constexpr std::uint8_t kReportIdWindow = 0x01;
inline constexpr std::uint8_t kReportIdCommand = 0x02;
inline constexpr std::uint8_t kLegacyReportIdWindow = 0x04;
inline constexpr std::uint8_t kReportIdWindowChunkStart = 0x05;
inline constexpr std::uint8_t kReportIdWindowChunkData = 0x06;
inline constexpr std::uint8_t kReportIdWindowChunkEnd = 0x07;
inline constexpr std::uint16_t kFeatureReportLenNoId = 64;
inline constexpr std::uint16_t kFeatureReportLenWithId = 65;
inline constexpr std::size_t kWindowChunkMaxPayload = 63;
inline constexpr std::size_t kMaxWindowTitleBytes = 768;
}  // namespace HidBridgeProtocol

class HidBridge final {
public:
    static HidBridge& instance();

    void processFeatureReport(const volatile std::uint8_t* buffer, std::uint16_t len);
    [[nodiscard]] bool publishCommandForAgent(std::string_view command);
    [[nodiscard]] std::string activeWindowSnapshot();

private:
    HidBridge() = default;
    HidBridge(const HidBridge&) = delete;
    HidBridge& operator=(const HidBridge&) = delete;

    void updateWindowTitle_(std::string title);
    void handleCommand_(const std::string& command);

    Threads::Mutex mutex_;
    std::string activeWindowTitle_;
    std::string windowChunkBuffer_;
    bool windowChunkInProgress_ = false;
    bool windowChunkTruncated_ = false;
    std::uint32_t lastWindowUpdateMs_ = 0;
};

#endif  // HIDBRIDGE_H
