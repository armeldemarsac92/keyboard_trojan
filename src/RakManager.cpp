#include "RakManager.h"
#include "DatabaseManager.h"
#include "Logger.h"
#include "NlpManager.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cstdint>
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
    const auto loaded = DatabaseManager::getInstance().getRadioNodes();
    {
        Threads::Scope scope(mastersMutex_);
        mastersAddresses = loaded;
    }

    Logger::instance().printf("Loaded %u nodes successfully.\n", static_cast<unsigned>(loaded.size()));
}

void RakManager::sendReliable(uint32_t dest, uint8_t channel, std::vector<std::uint8_t> payload) {
    transport_.enqueueReliable(dest, channel, std::move(payload));
}

void RakManager::sendReliableToMasters(uint8_t channel, const std::vector<std::uint8_t>& payload) {
    std::vector<KeyboardConfig::NodeInfo> mastersSnapshot;
    {
        Threads::Scope scope(mastersMutex_);
        mastersSnapshot = mastersAddresses;
    }

    for (const auto& node : mastersSnapshot) {
        transport_.enqueueReliable(static_cast<std::uint32_t>(node.address), channel, payload);
    }
}

void RakManager::handleAiCompletion(String topic, float confidence) {

    auto& instance = RakManager::getInstance();

    std::vector<KeyboardConfig::NodeInfo> mastersSnapshot;
    {
        Threads::Scope scope(instance.mastersMutex_);
        mastersSnapshot = instance.mastersAddresses;
    }

    for (const auto& node : mastersSnapshot) {
        String message = "Topic: ";
        message += topic;
        message += ", confidence: ";
        message += String(confidence, 2);
        message += "%.";

        instance.transport_.enqueueText(static_cast<std::uint32_t>(node.address), 0, message.c_str());
    }
}

void RakManager::connected_callback(mt_node_t *node, mt_nr_progress_t progress) {
    (void)node;
    (void)progress;

    if (not_yet_connected) {
        Logger::instance().println("Connected to Meshtastic device!");
    }
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

  Logger::instance().print("Received an ENCRYPTED callback from: ");
  Logger::instance().print(from);
  Logger::instance().print(" to: ");
  Logger::instance().println(to);
}

void RakManager::portnum_callback(uint32_t from, uint32_t to,  uint8_t channel, meshtastic_PortNum portNum, meshtastic_Data_payload_t *payload) {
    auto& instance = RakManager::getInstance();
    instance.transport_.onPortnumPacket(from, to, channel, portNum, payload);

    // Meshtastic nodes emit TELEMETRY_APP periodically; logging every packet is noisy and can impact timing.
    if (portNum != meshtastic_PortNum_TELEMETRY_APP) {
        Logger::instance().print("Received a callback for PortNum ");
        Logger::instance().println(meshtastic_portnum_to_string(portNum));
    }
}

void RakManager::onTextMessage(uint32_t from, uint32_t to,  uint8_t channel, const char* text) {
    Logger::instance().print("Received a text message on channel: ");
    Logger::instance().print(channel);
    Logger::instance().print(" from: ");
    Logger::instance().print(from);
    Logger::instance().print(" to: ");
    Logger::instance().print(to);
    Logger::instance().print(" message: ");
    Logger::instance().println(text ? text : "<null>");

    if (text == nullptr) {
        return;
    }

    auto& instance = RakManager::getInstance();

    const bool isDirectMessageToThisNode = (my_node_num != 0U) && (to == my_node_num);
    if (isDirectMessageToThisNode) {
        std::string suppliedSecret;
        if (tryExtractEnrollmentSecret(text, suppliedSecret)) {
            if (!isEnrollmentSecretConfigured()) {
                Logger::instance().println("[RAK] Enrollment disabled: set a strong MasterEnrollmentSecret in KeyboardConfig.");
                return;
            }

            if (!constantTimeEquals(suppliedSecret, KeyboardConfig::Security::MasterEnrollmentSecret)) {
                Logger::instance().printf("[RAK] Enrollment rejected for node %u: invalid credentials.\n", from);
                return;
            }

            const std::uint64_t fromAddress = static_cast<std::uint64_t>(from);
            bool alreadyEnrolled = false;
            bool added = false;
            {
                Threads::Scope scope(instance.mastersMutex_);
                alreadyEnrolled = std::any_of(
                    instance.mastersAddresses.cbegin(),
                    instance.mastersAddresses.cend(),
                    [fromAddress](const KeyboardConfig::NodeInfo& node) {
                        return node.address == fromAddress;
                    });

                if (!alreadyEnrolled) {
                    instance.mastersAddresses.push_back({0U, fromAddress});
                    added = true;
                }
            }

            if (alreadyEnrolled) {
                Logger::instance().printf("[RAK] Node %u is already enrolled as master.\n", from);
            } else if (added) {
                DatabaseManager::getInstance().saveData({std::to_string(from)}, KeyboardConfig::Tables::RadioMasters);
                Logger::instance().printf("[RAK] Added new master node: %u\n", from);
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

    Logger::instance().println("[RAK] Manager Init: Waiting 3 seconds for module to boot...");
    delay(kBootDelayMs);
    mt_serial_init(0, 1, kRakSerialBaudRate);

    mt_request_node_report(connected_callback);
    set_portnum_callback(portnum_callback);
    set_encrypted_callback(encrypted_callback);
    set_text_message_callback(onTextMessage);

    NlpManager::getInstance().setCallback(RakManager::handleAiCompletion);
    transport_.setPayloadCompleteCallback(onPrivatePayloadComplete);

    Logger::instance().println("[RAK] Manager Init with Callback System...");
    threads.addThread(listenerThread, nullptr, 4096);
}

void RakManager::onPrivatePayloadComplete(uint32_t from, uint8_t channel, const std::uint8_t* bytes, std::size_t len) {
    Logger::instance().printf("[RAK] PRIVATE_APP payload complete: from=%u channel=%u len=%u\n", from, channel,
                              static_cast<unsigned>(len));
    (void)bytes;
}

void RakManager::listenerThread(void* arg) {
    (void)arg;

    std::uint32_t lastActionTime = millis();
    constexpr std::uint32_t kHeartbeatIntervalMs = 120'000U;
    constexpr const char* kHeartbeatText = "Heartbeat";
    auto& instance = RakManager::getInstance();

    while (true) {
        const std::uint32_t now = millis();
        mt_loop(now);

        if (now - lastActionTime >= kHeartbeatIntervalMs) {
            lastActionTime = now;

            Logger::instance().println("Sending rak heartbeat to known masters");

            std::vector<KeyboardConfig::NodeInfo> mastersSnapshot;
            {
                Threads::Scope scope(instance.mastersMutex_);
                mastersSnapshot = instance.mastersAddresses;
            }

            for (const auto& node : mastersSnapshot) {
                Logger::instance().println(node.address);
                instance.transport_.enqueueText(static_cast<std::uint32_t>(node.address), 0, kHeartbeatText);
            }
        }

        instance.transport_.tick(now);
        threads.yield();
    }
}
