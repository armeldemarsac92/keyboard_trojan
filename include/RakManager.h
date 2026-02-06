#ifndef RAK_MANAGER_H
#define RAK_MANAGER_H

#include <Arduino.h>
#include <Meshtastic.h>
#include <TeensyThreads.h>

#include "../config/KeyboardConfig.h"

class RakManager {
public:
    static RakManager& getInstance();
    void begin();

    // Callbacks MUST be static to be passed to the Meshtastic library
    static void onTextMessage(uint32_t from, uint32_t to, uint8_t channel, const char* text);
    static void portnum_callback(uint32_t from, uint32_t to, uint8_t channel, meshtastic_PortNum portNum, meshtastic_Data_payload_t *payload);
    static void encrypted_callback(uint32_t from, uint32_t to, uint8_t channel, meshtastic_MeshPacket_public_key_t pubKey, meshtastic_MeshPacket_encrypted_t *enc_payload);
    static void connected_callback(mt_node_t *node, mt_nr_progress_t progress);
    static void handleAiCompletion(String topic, float confidence);
private:
    RakManager();
    std::vector<KeyboardConfig::NodeInfo> mastersAddresses;
    void loadSettingsFromDb();
    static void listenerThread(void* arg);

    // Helper for strings (can be static or instance)
    static const char* meshtastic_portnum_to_string(meshtastic_PortNum port);
    static void displayPubKey(meshtastic_MeshPacket_public_key_t pubKey, char *hex_str);

    // Connection state
    static bool not_yet_connected;
};

#endif