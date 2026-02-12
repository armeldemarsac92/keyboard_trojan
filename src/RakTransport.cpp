#include "RakTransport.h"

#include <Arduino.h>

#include "Logger.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <utility>

// Meshtastic-arduino internal symbols (not part of the public API).
extern bool _mt_send_toRadio(meshtastic_ToRadio toRadio);
extern size_t pb_size;  // receive FIFO size inside mt_protocol.cpp

namespace {
constexpr std::uint8_t kMagic0 = 0x52;  // 'R'
constexpr std::uint8_t kMagic1 = 0x4B;  // 'K'
constexpr std::uint8_t kVersion = 1;

enum class FrameType : std::uint8_t {
    Data = 1,
    Ack = 2,
};

struct FrameHeader {
    FrameType type = FrameType::Data;
    std::uint32_t msgId = 0;
    std::uint16_t chunkIndex = 0;
    std::uint16_t chunkCount = 0;
    std::uint32_t totalLen = 0;
};

constexpr std::size_t kMaxDecodedPayloadBytes = sizeof(((meshtastic_Data_payload_t*)nullptr)->bytes);
constexpr std::size_t kHeaderLen = 2 + 1 + 1 + 4 + 2 + 2 + 4;
static_assert(kHeaderLen <= kMaxDecodedPayloadBytes);
constexpr std::size_t kMaxChunkDataBytes = kMaxDecodedPayloadBytes - kHeaderLen;
static_assert(kMaxChunkDataBytes > 0);

void writeLe16(std::uint8_t* out, std::uint16_t v) {
    out[0] = static_cast<std::uint8_t>(v & 0xFF);
    out[1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
}

void writeLe32(std::uint8_t* out, std::uint32_t v) {
    out[0] = static_cast<std::uint8_t>(v & 0xFF);
    out[1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
    out[2] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
    out[3] = static_cast<std::uint8_t>((v >> 24) & 0xFF);
}

std::uint16_t readLe16(const std::uint8_t* in) {
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(in[0]) |
                                      static_cast<std::uint16_t>(static_cast<std::uint16_t>(in[1]) << 8));
}

std::uint32_t readLe32(const std::uint8_t* in) {
    return static_cast<std::uint32_t>(static_cast<std::uint32_t>(in[0]) |
                                      (static_cast<std::uint32_t>(in[1]) << 8) |
                                      (static_cast<std::uint32_t>(in[2]) << 16) |
                                      (static_cast<std::uint32_t>(in[3]) << 24));
}

bool encodeHeader(const FrameHeader& h, std::uint8_t* out, std::size_t outLen) {
    if (out == nullptr || outLen < kHeaderLen) {
        return false;
    }

    out[0] = kMagic0;
    out[1] = kMagic1;
    out[2] = kVersion;
    out[3] = static_cast<std::uint8_t>(h.type);
    writeLe32(out + 4, h.msgId);
    writeLe16(out + 8, h.chunkIndex);
    writeLe16(out + 10, h.chunkCount);
    writeLe32(out + 12, h.totalLen);
    return true;
}

bool tryParseHeader(const std::uint8_t* in, std::size_t inLen, FrameHeader& out) {
    if (in == nullptr || inLen < kHeaderLen) {
        return false;
    }

    if (in[0] != kMagic0 || in[1] != kMagic1 || in[2] != kVersion) {
        return false;
    }

    const auto type = static_cast<FrameType>(in[3]);
    if (type != FrameType::Data && type != FrameType::Ack) {
        return false;
    }

    out.type = type;
    out.msgId = readLe32(in + 4);
    out.chunkIndex = readLe16(in + 8);
    out.chunkCount = readLe16(in + 10);
	    out.totalLen = readLe32(in + 12);
	    return true;
	}

	std::vector<std::uint8_t> buildAckFrame(std::uint32_t msgId, std::uint16_t chunkIndex, std::uint16_t chunkCount,
	                                       std::uint32_t totalLen) {
	    std::vector<std::uint8_t> frame(kHeaderLen);
    FrameHeader h;
    h.type = FrameType::Ack;
    h.msgId = msgId;
    h.chunkIndex = chunkIndex;
    h.chunkCount = chunkCount;
    h.totalLen = totalLen;
    (void)encodeHeader(h, frame.data(), frame.size());
    return frame;
}

std::size_t writeDataFrame(std::uint32_t msgId, std::uint16_t chunkIndex, std::uint16_t chunkCount,
                           std::uint32_t totalLen, const std::uint8_t* data, std::size_t dataLen, std::uint8_t* out,
                           std::size_t outLen) {
    if (out == nullptr) {
        return 0;
    }

    if (data == nullptr && dataLen != 0) {
        return 0;
    }

    if (outLen < kHeaderLen + dataLen) {
        return 0;
    }

    FrameHeader h;
    h.type = FrameType::Data;
    h.msgId = msgId;
    h.chunkIndex = chunkIndex;
    h.chunkCount = chunkCount;
    h.totalLen = totalLen;
    if (!encodeHeader(h, out, outLen)) {
        return 0;
    }

    if (dataLen > 0) {
        std::memcpy(out + kHeaderLen, data, dataLen);
    }

    return kHeaderLen + dataLen;
}

bool canSendNow() {
    // Meshtastic-arduino uses a shared protobuf buffer for RX+TX and clears pb_size on send.
    // If pb_size != 0, a send would discard partially-received bytes already consumed from the stream.
    return pb_size == 0;
}

bool sendDecodedPacket(meshtastic_PortNum portNum, uint32_t dest, uint8_t channel, const std::uint8_t* bytes,
                       std::size_t len) {
    if (!canSendNow()) {
        return false;
    }

    if (bytes == nullptr && len != 0) {
        return false;
    }

    if (len > kMaxDecodedPayloadBytes) {
        return false;
    }

    meshtastic_MeshPacket meshPacket = meshtastic_MeshPacket_init_default;
    meshPacket.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    meshPacket.id = random(0x7FFFFFFF);
    meshPacket.decoded.portnum = portNum;
    meshPacket.to = dest;
    meshPacket.channel = channel;
    meshPacket.want_ack = true;
    meshPacket.decoded.payload.size = static_cast<decltype(meshPacket.decoded.payload.size)>(len);
    if (len > 0) {
        std::memcpy(meshPacket.decoded.payload.bytes, bytes, len);
    }

    meshtastic_ToRadio toRadio = meshtastic_ToRadio_init_default;
    toRadio.which_payload_variant = meshtastic_ToRadio_packet_tag;
    toRadio.packet = meshPacket;
    return _mt_send_toRadio(toRadio);
}

std::uint32_t generateMsgId() {
    // random() can't handle a full uint32 range on this platform; keep it consistent with mt_send_text().
    auto id = static_cast<std::uint32_t>(random(0x7FFFFFFF));
    if (id == 0U) {
        id = 1U;
    }
    return id;
}

std::uint16_t computeChunkCount(std::size_t totalLen) {
    if (totalLen == 0) {
        return 0;
    }

    const std::size_t chunks = (totalLen + (kMaxChunkDataBytes - 1)) / kMaxChunkDataBytes;
    if (chunks > 0xFFFFu) {
        return 0xFFFFu;
    }

    return static_cast<std::uint16_t>(chunks);
}
}  // namespace

void RakTransport::setPayloadCompleteCallback(PayloadCompleteCallback cb) {
    payloadCompleteCb_ = cb;
}

void RakTransport::enqueueText(uint32_t dest, uint8_t channel, std::string text) {
    Threads::Scope scope(mutex_);
    pendingText_.push_back(OutboundText{dest, channel, std::move(text)});
}

void RakTransport::enqueueReliable(uint32_t dest, uint8_t channel, std::vector<std::uint8_t> payload) {
    enqueueReliable(dest, channel, std::move(payload), defaults_);
}

void RakTransport::enqueueReliable(uint32_t dest, uint8_t channel, std::vector<std::uint8_t> payload,
                                   ReliableOptions options) {
    if (payload.empty()) {
        return;
    }

    Threads::Scope scope(mutex_);
    OutboundTransfer t;
    t.dest = dest;
    t.channel = channel;
    t.msgId = generateMsgId();
    t.payload = std::move(payload);
    t.chunkCount = computeChunkCount(t.payload.size());
    t.options = options;
    pendingTransfers_.push_back(std::move(t));
}

void RakTransport::onPortnumPacket(uint32_t from, uint32_t to, uint8_t channel, meshtastic_PortNum port,
                                  const meshtastic_Data_payload_t* payload) {
    if (port != meshtastic_PortNum_PRIVATE_APP) {
        return;
    }

    if (payload == nullptr) {
        return;
    }

    InboundFrame frame;
    frame.from = from;
    frame.to = to;
    frame.channel = channel;
    frame.bytes.assign(payload->bytes, payload->bytes + payload->size);

    Threads::Scope scope(mutex_);
    inboundFrames_.push_back(std::move(frame));
}

void RakTransport::tick(std::uint32_t nowMs) {
    drainInboundFrames(nowMs);
    cleanupInboundTransfers(nowMs);
    serviceOutbound(nowMs);
}

void RakTransport::drainInboundFrames(std::uint32_t nowMs) {
    std::deque<InboundFrame> localFrames;
    {
        Threads::Scope scope(mutex_);
        localFrames = std::move(inboundFrames_);
        inboundFrames_.clear();
    }

    for (const auto& frame : localFrames) {
        FrameHeader h;
        if (!tryParseHeader(frame.bytes.data(), frame.bytes.size(), h)) {
            continue;
        }

        if (h.type == FrameType::Ack) {
            // Stop-and-wait ACK: (msgId, chunkIndex) for the active transfer only.
            if (!hasActiveTransfer_) {
                continue;
            }

            if (frame.from != activeTransfer_.dest) {
                continue;
            }

            if (h.msgId != activeTransfer_.msgId) {
                continue;
            }

            if (!activeTransfer_.awaitingAck) {
                continue;
            }

            if (h.chunkIndex != activeTransfer_.nextChunkIndex) {
                continue;
            }

            activeTransfer_.awaitingAck = false;
            activeTransfer_.retriesForCurrentChunk = 0;
            activeTransfer_.nextChunkIndex = static_cast<std::uint16_t>(activeTransfer_.nextChunkIndex + 1);

            if (activeTransfer_.nextChunkIndex >= activeTransfer_.chunkCount) {
                hasActiveTransfer_ = false;
            }

            continue;
        }

        // DATA frame.
        if (h.chunkCount == 0 || h.chunkIndex >= h.chunkCount || h.totalLen == 0) {
            continue;
        }

        // Best-effort protection against memory abuse.
        if (h.totalLen > defaults_.maxInboundPayloadBytes) {
            continue;
        }

        const auto expectedChunkCount = computeChunkCount(h.totalLen);
        if (expectedChunkCount == 0 || h.chunkCount != expectedChunkCount) {
            continue;
        }

        const std::size_t chunkDataLen = frame.bytes.size() - kHeaderLen;
        if (chunkDataLen == 0 || chunkDataLen > kMaxChunkDataBytes) {
            continue;
        }

        const std::size_t offset = static_cast<std::size_t>(h.chunkIndex) * kMaxChunkDataBytes;
        if (offset + chunkDataLen > h.totalLen) {
            continue;
        }

        auto it = std::find_if(inboundTransfers_.begin(), inboundTransfers_.end(), [&](const InboundTransfer& t) {
            return t.from == frame.from && t.channel == frame.channel && t.msgId == h.msgId;
        });

        if (it == inboundTransfers_.end()) {
            InboundTransfer t;
            t.from = frame.from;
            t.channel = frame.channel;
            t.msgId = h.msgId;
            t.createdMs = nowMs;
            t.lastUpdateMs = nowMs;
            t.totalLen = h.totalLen;
            t.chunkCount = h.chunkCount;
            t.buffer.assign(static_cast<std::size_t>(h.totalLen), 0);
            t.received.assign(static_cast<std::size_t>(h.chunkCount), 0);
            inboundTransfers_.push_back(std::move(t));
            it = std::prev(inboundTransfers_.end());
        }

        if (it->totalLen != h.totalLen || it->chunkCount != h.chunkCount) {
            continue;
        }

        if (it->received[h.chunkIndex] == 0) {
            std::memcpy(it->buffer.data() + offset, frame.bytes.data() + kHeaderLen, chunkDataLen);
            it->received[h.chunkIndex] = 1;
        }
        it->lastUpdateMs = nowMs;

        // Queue an app-layer ACK. Never send from within callbacks.
        {
            Threads::Scope scope(mutex_);
            OutboundFrame ack;
            ack.port = meshtastic_PortNum_PRIVATE_APP;
            ack.dest = frame.from;
            ack.channel = frame.channel;
            ack.bytes = buildAckFrame(h.msgId, h.chunkIndex, h.chunkCount, h.totalLen);
            immediateFrames_.push_back(std::move(ack));
        }

        const bool complete = std::all_of(it->received.begin(), it->received.end(), [](std::uint8_t v) {
            return v != 0;
        });

        if (!complete) {
            continue;
        }

        if (payloadCompleteCb_) {
            payloadCompleteCb_(it->from, it->channel, it->buffer.data(), it->buffer.size());
        } else {
            Logger::instance().printf("[RAK] PRIVATE_APP payload complete: from=%u len=%u\n", it->from,
                                      static_cast<unsigned>(it->buffer.size()));
        }

        inboundTransfers_.erase(it);
    }
}

void RakTransport::cleanupInboundTransfers(std::uint32_t nowMs) {
    // Remove stalled assemblies (best-effort; the protocol is stop-and-wait so this should be rare).
    const auto timeoutMs = defaults_.inboundAssemblyTimeoutMs;

    inboundTransfers_.erase(
        std::remove_if(inboundTransfers_.begin(), inboundTransfers_.end(), [&](const InboundTransfer& t) {
            return (nowMs - t.lastUpdateMs) > timeoutMs;
        }),
        inboundTransfers_.end());
}

void RakTransport::serviceOutbound(std::uint32_t nowMs) {
    // 1) High priority frames (ACKs).
    if (canSendNow()) {
        OutboundFrame immediate;
        bool haveImmediate = false;
        {
            Threads::Scope scope(mutex_);
            if (!immediateFrames_.empty()) {
                immediate = std::move(immediateFrames_.front());
                immediateFrames_.pop_front();
                haveImmediate = true;
            }
        }

        if (haveImmediate) {
            if (!sendDecodedPacket(immediate.port, immediate.dest, immediate.channel, immediate.bytes.data(),
                                   immediate.bytes.size())) {
                // Best-effort: if the send fails, requeue. The sender will retry after timeout anyway.
                Threads::Scope scope(mutex_);
                immediateFrames_.push_front(std::move(immediate));
            }
            return;  // at most one send per tick
        }
    }

    // 2) Active reliable transfer (stop-and-wait per chunk).
    if (hasActiveTransfer_) {
        if (activeTransfer_.awaitingAck) {
            const auto elapsed = nowMs - activeTransfer_.lastSendMs;
            if (elapsed < activeTransfer_.options.ackTimeoutMs) {
                return;
            }

            if (activeTransfer_.retriesForCurrentChunk >= activeTransfer_.options.maxRetriesPerChunk) {
                Logger::instance().printf("[RAK] Reliable send aborted: dest=%u msgId=%u chunk=%u/%u\n",
                                          activeTransfer_.dest, activeTransfer_.msgId, activeTransfer_.nextChunkIndex,
                                          static_cast<unsigned>(activeTransfer_.chunkCount));
                hasActiveTransfer_ = false;
                return;
            }

            // Timeout: resend current chunk.
            const std::size_t offset = static_cast<std::size_t>(activeTransfer_.nextChunkIndex) * kMaxChunkDataBytes;
            const std::size_t remaining = activeTransfer_.payload.size() - offset;
            const std::size_t dataLen = std::min<std::size_t>(remaining, kMaxChunkDataBytes);

            std::array<std::uint8_t, kMaxDecodedPayloadBytes> frame{};
            const std::size_t frameLen =
                writeDataFrame(activeTransfer_.msgId, activeTransfer_.nextChunkIndex, activeTransfer_.chunkCount,
                               static_cast<std::uint32_t>(activeTransfer_.payload.size()),
                               activeTransfer_.payload.data() + offset, dataLen, frame.data(), frame.size());
            if (frameLen == 0) {
                return;
            }

            if (sendDecodedPacket(meshtastic_PortNum_PRIVATE_APP, activeTransfer_.dest, activeTransfer_.channel,
                                  frame.data(), frameLen)) {
                activeTransfer_.lastSendMs = nowMs;
                activeTransfer_.retriesForCurrentChunk =
                    static_cast<std::uint8_t>(activeTransfer_.retriesForCurrentChunk + 1);
            }
            return;
        }

        if (activeTransfer_.nextChunkIndex >= activeTransfer_.chunkCount) {
            hasActiveTransfer_ = false;
            return;
        }

        const std::size_t offset = static_cast<std::size_t>(activeTransfer_.nextChunkIndex) * kMaxChunkDataBytes;
        const std::size_t remaining = activeTransfer_.payload.size() - offset;
        const std::size_t dataLen = std::min<std::size_t>(remaining, kMaxChunkDataBytes);

        std::array<std::uint8_t, kMaxDecodedPayloadBytes> frame{};
        const std::size_t frameLen =
            writeDataFrame(activeTransfer_.msgId, activeTransfer_.nextChunkIndex, activeTransfer_.chunkCount,
                           static_cast<std::uint32_t>(activeTransfer_.payload.size()),
                           activeTransfer_.payload.data() + offset, dataLen, frame.data(), frame.size());
        if (frameLen == 0) {
            return;
        }

        if (sendDecodedPacket(meshtastic_PortNum_PRIVATE_APP, activeTransfer_.dest, activeTransfer_.channel,
                              frame.data(), frameLen)) {
            activeTransfer_.awaitingAck = true;
            activeTransfer_.lastSendMs = nowMs;
        }
        return;
    }

    // 3) Promote pending transfers to active.
    {
        Threads::Scope scope(mutex_);
        if (!pendingTransfers_.empty()) {
            activeTransfer_ = std::move(pendingTransfers_.front());
            pendingTransfers_.pop_front();
            hasActiveTransfer_ = true;
            activeTransfer_.awaitingAck = false;
            activeTransfer_.nextChunkIndex = 0;
            activeTransfer_.retriesForCurrentChunk = 0;
            return;
        }
    }

    // 4) Best-effort text messages.
    OutboundText text;
    {
        Threads::Scope scope(mutex_);
        if (pendingText_.empty()) {
            return;
        }
        text = std::move(pendingText_.front());
        pendingText_.pop_front();
    }

    const auto* bytes = reinterpret_cast<const std::uint8_t*>(text.text.data());
    const std::size_t len = text.text.size();
    const std::size_t safeLen = std::min<std::size_t>(len, kMaxDecodedPayloadBytes);
    if (safeLen != len) {
        Logger::instance().printf("[RAK] Truncating TEXT_MESSAGE_APP from %u to %u bytes\n",
                                  static_cast<unsigned>(len), static_cast<unsigned>(safeLen));
    }

    (void)sendDecodedPacket(meshtastic_PortNum_TEXT_MESSAGE_APP, text.dest, text.channel, bytes, safeLen);
}
