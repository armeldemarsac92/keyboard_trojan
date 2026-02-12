#ifndef RAK_TRANSPORT_H
#define RAK_TRANSPORT_H

#include <Meshtastic.h>
#include <TeensyThreads.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

class RakTransport {
public:
    using PayloadCompleteCallback =
        void (*)(uint32_t from, uint8_t channel, const std::uint8_t* bytes, std::size_t len);

    struct ReliableOptions {
        std::uint32_t ackTimeoutMs = 3'000U;
        std::uint8_t maxRetriesPerChunk = 5U;
        std::uint32_t inboundAssemblyTimeoutMs = 30'000U;
        std::size_t maxInboundPayloadBytes = 8U * 1024U;
    };

    RakTransport() = default;

    void setPayloadCompleteCallback(PayloadCompleteCallback cb);

    void enqueueText(uint32_t dest, uint8_t channel, std::string text);
    void enqueueReliable(uint32_t dest, uint8_t channel, std::vector<std::uint8_t> payload);
    void enqueueReliable(uint32_t dest, uint8_t channel, std::vector<std::uint8_t> payload, ReliableOptions options);

    void onPortnumPacket(uint32_t from, uint32_t to, uint8_t channel, meshtastic_PortNum port,
                         const meshtastic_Data_payload_t* payload);

    // Must be called from the single thread that owns Meshtastic send/loop operations.
    void tick(std::uint32_t nowMs);

private:
    struct OutboundText {
        uint32_t dest = BROADCAST_ADDR;
        uint8_t channel = 0;
        std::string text;
    };

    struct OutboundTransfer {
        uint32_t dest = BROADCAST_ADDR;
        uint8_t channel = 0;
        std::uint32_t msgId = 0;
        std::vector<std::uint8_t> payload;
        std::uint16_t chunkCount = 0;
        std::uint16_t nextChunkIndex = 0;
        std::uint8_t retriesForCurrentChunk = 0;
        std::uint32_t lastSendMs = 0;
        ReliableOptions options;
        bool awaitingAck = false;
    };

    struct InboundFrame {
        uint32_t from = 0;
        uint32_t to = 0;
        uint8_t channel = 0;
        std::vector<std::uint8_t> bytes;
    };

    struct InboundTransfer {
        uint32_t from = 0;
        uint8_t channel = 0;
        std::uint32_t msgId = 0;
        std::uint32_t createdMs = 0;
        std::uint32_t lastUpdateMs = 0;
        std::uint32_t totalLen = 0;
        std::uint16_t chunkCount = 0;
        std::vector<std::uint8_t> buffer;
        std::vector<std::uint8_t> received;
    };

    struct OutboundFrame {
        meshtastic_PortNum port = meshtastic_PortNum_UNKNOWN_APP;
        uint32_t dest = BROADCAST_ADDR;
        uint8_t channel = 0;
        std::vector<std::uint8_t> bytes;
    };

    void drainInboundFrames(std::uint32_t nowMs);
    void serviceOutbound(std::uint32_t nowMs);
    void cleanupInboundTransfers(std::uint32_t nowMs);

    PayloadCompleteCallback payloadCompleteCb_ = nullptr;
    ReliableOptions defaults_{};

    Threads::Mutex mutex_;
    std::deque<OutboundText> pendingText_;
    std::deque<OutboundTransfer> pendingTransfers_;
    std::deque<InboundFrame> inboundFrames_;

    std::deque<OutboundFrame> immediateFrames_;
    std::vector<InboundTransfer> inboundTransfers_;

    bool hasActiveTransfer_ = false;
    OutboundTransfer activeTransfer_{};
};

#endif
