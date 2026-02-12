#include "RakManager.h"
#include "DatabaseManager.h"
#include "NlpManager.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

bool RakManager::not_yet_connected = true;

namespace {
constexpr std::size_t kMaxEnrollmentMessageLen = 160;
constexpr char kEnrollmentSeparator = ':';
constexpr std::string_view kEnrollmentSecretPlaceholder{"CHANGE_ME_TO_A_LONG_RANDOM_SECRET"};

bool constantTimeEquals(const std::string_view lhs, const std::string_view rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }

    std::uint8_t diff = 0;
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        diff |= static_cast<std::uint8_t>(lhs[i] ^ rhs[i]);
    }

    return diff == 0;
}

bool isEnrollmentSecretConfigured() {
    const auto secret = KeyboardConfig::Security::MasterEnrollmentSecret;
    return secret.size() >= KeyboardConfig::Security::MinMasterEnrollmentSecretLength &&
           secret != kEnrollmentSecretPlaceholder;
}

bool tryExtractEnrollmentSecret(const char* message, std::string& outSecret) {
    if (message == nullptr) {
        return false;
    }

    const std::size_t messageLen = strnlen(message, kMaxEnrollmentMessageLen + 1);
    if (messageLen == 0 || messageLen > kMaxEnrollmentMessageLen) {
        return false;
    }

    const std::string_view messageView{message, messageLen};
    const std::size_t separatorPos = messageView.find(kEnrollmentSeparator);

    if (separatorPos == std::string_view::npos || separatorPos == 0 || separatorPos + 1 >= messageView.size()) {
        return false;
    }

    if (messageView.substr(0, separatorPos) != KeyboardConfig::Security::MasterEnrollmentCommand) {
        return false;
    }

    outSecret.assign(messageView.substr(separatorPos + 1));
    return true;
}
}

RakManager& RakManager::getInstance() {
    static RakManager instance;
    return instance;
}

RakManager::RakManager() {
    loadSettingsFromDb();
}

void RakManager::loadSettingsFromDb() {
    this->mastersAddresses = DatabaseManager::getInstance().getRadioNodes();

    Serial.printf("Loaded %u nodes successfully.\n", static_cast<unsigned>(this->mastersAddresses.size()));
}

void RakManager::handleAiCompletion(String topic, float confidence) {

    auto& instance = RakManager::getInstance();

    for (const auto& node : instance.mastersAddresses) {

        String message = "Topic: ";
        message += topic;
        message += ", confidence: ";
        message += String(confidence); // 2 decimal places
        message += "%.";
        mt_send_text(message.c_str(), node.address, 0);
        threads.yield();
    }
}

void RakManager::connected_callback(mt_node_t *node, mt_nr_progress_t progress) {
    (void)node;
    (void)progress;

    if (not_yet_connected)
        Serial.println("Connected to Meshtastic device!");
    not_yet_connected = false;
}

const char* RakManager::meshtastic_portnum_to_string(meshtastic_PortNum port) {
  switch (port) {
      case meshtastic_PortNum_UNKNOWN_APP: return "UNKNOWN_APP";
      case meshtastic_PortNum_TEXT_MESSAGE_APP: return "TEXT_MESSAGE_APP";
      case meshtastic_PortNum_REMOTE_HARDWARE_APP: return "REMOTE_HARDWARE_APP";
      case meshtastic_PortNum_POSITION_APP: return "POSITION_APP";
      case meshtastic_PortNum_NODEINFO_APP: return "NODEINFO_APP";
      case meshtastic_PortNum_ROUTING_APP: return "ROUTING_APP";
      case meshtastic_PortNum_ADMIN_APP: return "ADMIN_APP";
      case meshtastic_PortNum_TEXT_MESSAGE_COMPRESSED_APP: return "TEXT_MESSAGE_COMPRESSED_APP";
      case meshtastic_PortNum_WAYPOINT_APP: return "WAYPOINT_APP";
      case meshtastic_PortNum_AUDIO_APP: return "AUDIO_APP";
      case meshtastic_PortNum_DETECTION_SENSOR_APP: return "DETECTION_SENSOR_APP";
      case meshtastic_PortNum_REPLY_APP: return "REPLY_APP";
      case meshtastic_PortNum_IP_TUNNEL_APP: return "IP_TUNNEL_APP";
      case meshtastic_PortNum_PAXCOUNTER_APP: return "PAXCOUNTER_APP";
      case meshtastic_PortNum_SERIAL_APP: return "SERIAL_APP";
      case meshtastic_PortNum_STORE_FORWARD_APP: return "STORE_FORWARD_APP";
      case meshtastic_PortNum_RANGE_TEST_APP: return "RANGE_TEST_APP";
      case meshtastic_PortNum_TELEMETRY_APP: return "TELEMETRY_APP";
      case meshtastic_PortNum_ZPS_APP: return "ZPS_APP";
      case meshtastic_PortNum_SIMULATOR_APP: return "SIMULATOR_APP";
      case meshtastic_PortNum_TRACEROUTE_APP: return "TRACEROUTE_APP";
      case meshtastic_PortNum_NEIGHBORINFO_APP: return "NEIGHBORINFO_APP";
      case meshtastic_PortNum_ATAK_PLUGIN: return "ATAK_PLUGIN";
      case meshtastic_PortNum_MAP_REPORT_APP: return "MAP_REPORT_APP";
      case meshtastic_PortNum_POWERSTRESS_APP: return "POWERSTRESS_APP";
      case meshtastic_PortNum_PRIVATE_APP: return "PRIVATE_APP";
      case meshtastic_PortNum_ATAK_FORWARDER: return "ATAK_FORWARDER";
      case meshtastic_PortNum_MAX: return "MAX";
      default: return "UNKNOWN_PORTNUM";
  }
}

void RakManager::displayPubKey(meshtastic_MeshPacket_public_key_t pubKey, char *hex_str) {
      for (std::size_t i = 0; i < 32; ++i) {
          std::snprintf(&hex_str[i * 2], 3, "%02x", static_cast<unsigned char>(pubKey.bytes[i]));
      }

      hex_str[64] = '\0';
}


void RakManager::encrypted_callback(uint32_t from, uint32_t to,  uint8_t channel, meshtastic_MeshPacket_public_key_t pubKey, meshtastic_MeshPacket_encrypted_t *enc_payload) {
  (void)channel;
  (void)pubKey;
  (void)enc_payload;

  Serial.print("Received an ENCRYPTED callback from: ");
  Serial.print(from);
  Serial.print(" to: ");
  Serial.println(to);
}

void RakManager::portnum_callback(uint32_t from, uint32_t to,  uint8_t channel, meshtastic_PortNum portNum, meshtastic_Data_payload_t *payload) {
  (void)from;
  (void)to;
  (void)channel;
  (void)payload;

  Serial.print("Received a callback for PortNum ");
  Serial.println(meshtastic_portnum_to_string(portNum));
}

void RakManager::onTextMessage(uint32_t from, uint32_t to,  uint8_t channel, const char* text) {
    Serial.print("Received a text message on channel: ");
    Serial.print(channel);
    Serial.print(" from: ");
    Serial.print(from);
    Serial.print(" to: ");
    Serial.print(to);
    Serial.print(" message: ");
    Serial.println(text ? text : "<null>");

    if (text == nullptr) {
        return;
    }

    auto& instance = RakManager::getInstance();

    const bool isDirectMessageToThisNode = (my_node_num != 0U) && (to == my_node_num);
    if (isDirectMessageToThisNode) {
        std::string suppliedSecret;
        if (tryExtractEnrollmentSecret(text, suppliedSecret)) {
            if (!isEnrollmentSecretConfigured()) {
                Serial.println("[RAK] Enrollment disabled: set a strong MasterEnrollmentSecret in KeyboardConfig.");
                return;
            }

            if (!constantTimeEquals(suppliedSecret, KeyboardConfig::Security::MasterEnrollmentSecret)) {
                Serial.printf("[RAK] Enrollment rejected for node %u: invalid credentials.\n", from);
                return;
            }

            const std::uint64_t fromAddress = static_cast<std::uint64_t>(from);
            const bool alreadyEnrolled = std::any_of(
                instance.mastersAddresses.cbegin(),
                instance.mastersAddresses.cend(),
                [fromAddress](const KeyboardConfig::NodeInfo& node) {
                    return node.address == fromAddress;
                });

            if (alreadyEnrolled) {
                Serial.printf("[RAK] Node %u is already enrolled as master.\n", from);
            } else {
                instance.mastersAddresses.push_back({0U, fromAddress});
                DatabaseManager::getInstance().saveData({std::to_string(from)}, KeyboardConfig::Tables::RadioMasters);
                Serial.printf("[RAK] Added new master node: %u\n", from);
            }
            return;
        }
    }

    if (std::strlen(text) > 1) {
        NlpManager::getInstance().analyzeSentence(text);
    }
}


void RakManager::begin() {
    constexpr std::uint32_t kBootDelayMs = 5'000U;
    constexpr std::uint32_t kRakSerialBaudRate = 921'600U;

    pinMode(0, INPUT);
    pinMode(1, INPUT);

    Serial.println("[RAK] Manager Init: Waiting 3 seconds for module to boot...");
    delay(kBootDelayMs);
    mt_serial_init(0, 1, kRakSerialBaudRate);

    mt_request_node_report(connected_callback);
    set_portnum_callback(portnum_callback);
    set_encrypted_callback(encrypted_callback);
    set_text_message_callback(onTextMessage);

    NlpManager::getInstance().setCallback(RakManager::handleAiCompletion);

    Serial.println("[RAK] Manager Init with Callback System...");
    threads.addThread(listenerThread, nullptr, 4096);
}

void RakManager::listenerThread(void* arg) {
    (void)arg;

    std::uint32_t lastActionTime = millis();
    constexpr std::uint32_t kHeartbeatIntervalMs = 120'000U;
    constexpr const char* kHeartbeatText = "Heartbeat";
    auto& instance = RakManager::getInstance();

    while (true) {
        mt_loop(millis());

        if (millis() - lastActionTime >= kHeartbeatIntervalMs) {
            lastActionTime = millis();

            Serial.println("Sending rak heartbeat to known masters");

            for (const auto& node : instance.mastersAddresses) {
                Serial.println(node.address);
                mt_send_text(kHeartbeatText, node.address, 0);
            }
        }
        threads.yield();
    }
}
