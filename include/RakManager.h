#ifndef RAK_MANAGER_H
#define RAK_MANAGER_H

#include <Arduino.h>
#include <Meshtastic.h>
#include <TeensyThreads.h>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

#include "RakTransport.h"

#include "../config/KeyboardConfig.h"

class RakManager {
public:
    static RakManager& getInstance();
    void begin();
    void sendReliable(uint32_t dest, uint8_t channel, std::vector<std::uint8_t> payload);
    void sendReliableToMasters(uint8_t channel, const std::vector<std::uint8_t>& payload);

    // Callbacks MUST be static to be passed to the Meshtastic library
    static void onTextMessage(uint32_t from, uint32_t to, uint8_t channel, const char* text);
    static void portnum_callback(uint32_t from, uint32_t to, uint8_t channel, meshtastic_PortNum portNum, meshtastic_Data_payload_t *payload);
    static void encrypted_callback(uint32_t from, uint32_t to, uint8_t channel, meshtastic_MeshPacket_public_key_t pubKey, meshtastic_MeshPacket_encrypted_t *enc_payload);
    static void connected_callback(mt_node_t *node, mt_nr_progress_t progress);
    static void handleAiCompletion(String topic, float confidence);
private:
    RakManager();
    std::vector<KeyboardConfig::NodeInfo> mastersAddresses;
    Threads::Mutex mastersMutex_;
    void loadSettingsFromDb();
    static void listenerThread(void* arg);
    static void onPrivatePayloadComplete(uint32_t from, uint8_t channel, const std::uint8_t* bytes, std::size_t len);
    void processCommands();
    void pollTypingSessionTimeout(std::uint32_t now);
    void pollQuerySessionTimeout(std::uint32_t now);
    void enqueueCommand(uint32_t from, uint32_t to, uint8_t channel, const char* text);

    struct PendingCommand {
        uint32_t from = 0;
        uint32_t to = 0;
        uint8_t channel = 0;
        std::string text;
    };

    Threads::Mutex commandsMutex_;
    std::deque<PendingCommand> pendingCommands_;

    struct TypingSession {
        uint32_t owner = 0;
        uint8_t channel = 0;
        std::uint32_t lastActivityMs = 0;
    };

    std::optional<TypingSession> typingSession_{};

    struct QuerySession {
        uint32_t owner = 0;
        uint8_t channel = 0;
        std::uint32_t lastActivityMs = 0;
        const DBTable* selectedTable = nullptr;
    };

    std::optional<QuerySession> querySession_{};

    // Helper for strings (can be static or instance)
    static const char* meshtastic_portnum_to_string(meshtastic_PortNum port);
    static void displayPubKey(meshtastic_MeshPacket_public_key_t pubKey, char *hex_str);

    // Connection state
    static bool not_yet_connected;

    RakTransport transport_{};
};

#endif
