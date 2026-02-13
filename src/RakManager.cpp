#include "RakManager.h"
#include "DatabaseManager.h"
#include "HostKeyboard.h"
#include "Logger.h"
#include "NlpManager.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <iterator>
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

constexpr char asciiLower(char c) {
    if (c >= 'A' && c <= 'Z') {
        return static_cast<char>(c - 'A' + 'a');
    }
    return c;
}

bool asciiIEquals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }

    for (std::size_t i = 0; i < a.size(); ++i) {
        if (asciiLower(a[i]) != asciiLower(b[i])) {
            return false;
        }
    }

    return true;
}

bool isAsciiWhitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

std::string unescapeRadioText(std::string_view in) {
    // Minimal unescape so users can send "\n" / "\t" in a single-line Meshtastic message.
    std::string out;
    out.reserve(in.size());

    for (std::size_t i = 0; i < in.size(); ++i) {
        const char c = in[i];
        if (c != '\\' || i + 1 >= in.size()) {
            out.push_back(c);
            continue;
        }

        const char n = in[i + 1];
        switch (n) {
            case 'n': out.push_back('\n'); ++i; continue;
            case 'r': out.push_back('\r'); ++i; continue;
            case 't': out.push_back('\t'); ++i; continue;
            case '\\': out.push_back('\\'); ++i; continue;
            default:
                out.push_back(c);
                continue;
        }
    }

    return out;
}

const char* routingErrorToString(meshtastic_Routing_Error err) {
    // Newer Meshtastic protobufs might emit values we don't have in our older generated enum.
    // Avoid putting those integers in a switch() to keep -Wswitch clean.
    const int v = static_cast<int>(err);
    if (v == 38) return "RATE_LIMIT_EXCEEDED";
    if (v == 39) return "PKI_SEND_FAIL_PUBLIC_KEY";

    switch (err) {
        case meshtastic_Routing_Error_NONE: return "NONE";
        case meshtastic_Routing_Error_NO_ROUTE: return "NO_ROUTE";
        case meshtastic_Routing_Error_GOT_NAK: return "GOT_NAK";
        case meshtastic_Routing_Error_TIMEOUT: return "TIMEOUT";
        case meshtastic_Routing_Error_NO_INTERFACE: return "NO_INTERFACE";
        case meshtastic_Routing_Error_MAX_RETRANSMIT: return "MAX_RETRANSMIT";
        case meshtastic_Routing_Error_NO_CHANNEL: return "NO_CHANNEL";
        case meshtastic_Routing_Error_TOO_LARGE: return "TOO_LARGE";
        case meshtastic_Routing_Error_NO_RESPONSE: return "NO_RESPONSE";
        case meshtastic_Routing_Error_DUTY_CYCLE_LIMIT: return "DUTY_CYCLE_LIMIT";
        case meshtastic_Routing_Error_BAD_REQUEST: return "BAD_REQUEST";
        case meshtastic_Routing_Error_NOT_AUTHORIZED: return "NOT_AUTHORIZED";
        case meshtastic_Routing_Error_PKI_FAILED: return "PKI_FAILED";
        case meshtastic_Routing_Error_PKI_UNKNOWN_PUBKEY: return "PKI_UNKNOWN_PUBKEY";
        case meshtastic_Routing_Error_ADMIN_BAD_SESSION_KEY: return "ADMIN_BAD_SESSION_KEY";
        case meshtastic_Routing_Error_ADMIN_PUBLIC_KEY_UNAUTHORIZED: return "ADMIN_PUBLIC_KEY_UNAUTHORIZED";
        default: return "UNKNOWN";
    }
}

const char* priorityToString(meshtastic_MeshPacket_Priority p) {
    switch (p) {
        case meshtastic_MeshPacket_Priority_UNSET: return "UNSET";
        case meshtastic_MeshPacket_Priority_BACKGROUND: return "BACKGROUND";
        case meshtastic_MeshPacket_Priority_DEFAULT: return "DEFAULT";
        case meshtastic_MeshPacket_Priority_RELIABLE: return "RELIABLE";
        case meshtastic_MeshPacket_Priority_RESPONSE: return "RESPONSE";
        case meshtastic_MeshPacket_Priority_HIGH: return "HIGH";
        case meshtastic_MeshPacket_Priority_ALERT: return "ALERT";
        case meshtastic_MeshPacket_Priority_ACK: return "ACK";
        case meshtastic_MeshPacket_Priority_MIN: return "MIN";
        case meshtastic_MeshPacket_Priority_MAX: return "MAX";
        default: return "UNKNOWN";
    }
}

const meshtastic_Data* tryGetDataFromPayload(const meshtastic_Data_payload_t* payload) {
    if (payload == nullptr) {
        return nullptr;
    }

    const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload);
    const auto* data = reinterpret_cast<const meshtastic_Data*>(bytes - offsetof(meshtastic_Data, payload));
    if (&data->payload != payload) {
        return nullptr;
    }

    return data;
}

const meshtastic_MeshPacket* tryGetMeshPacketFromData(const meshtastic_Data* data) {
    if (data == nullptr) {
        return nullptr;
    }

    const auto* bytes = reinterpret_cast<const std::uint8_t*>(data);
    const auto* pkt = reinterpret_cast<const meshtastic_MeshPacket*>(bytes - offsetof(meshtastic_MeshPacket, decoded));
    if (&pkt->decoded != data) {
        return nullptr;
    }

    return pkt;
}

void logRoutingAppPacket(uint32_t from, uint32_t to, uint8_t channel, const meshtastic_Data_payload_t* payload) {
    if (payload == nullptr) {
        Logger::instance().printf("[RAK][ROUTING] from=%u to=%u ch=%u (null payload)\n", from, to, channel);
        return;
    }

    const meshtastic_Data* data = tryGetDataFromPayload(payload);
    const meshtastic_MeshPacket* pkt = tryGetMeshPacketFromData(data);

    const std::size_t len = payload->size;
    if (len == 0) {
        Logger::instance().printf("[RAK][ROUTING] from=%u to=%u ch=%u (empty)\n", from, to, channel);
        return;
    }

    meshtastic_Routing routing = meshtastic_Routing_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(payload->bytes, len);
    if (!pb_decode(&stream, meshtastic_Routing_fields, &routing)) {
        constexpr std::size_t kMaxHex = 12;
        const std::size_t shown = std::min<std::size_t>(len, kMaxHex);

        Logger::instance().printf("[RAK][ROUTING] decode failed: %s (len=%u)\n",
                                  PB_GET_ERROR(&stream),
                                  static_cast<unsigned>(len));

        Logger::instance().print("[RAK][ROUTING] bytes=");
        for (std::size_t i = 0; i < shown; ++i) {
            Logger::instance().printf("%02X", static_cast<unsigned>(payload->bytes[i]));
            if (i + 1 < shown) Logger::instance().print(" ");
        }
        if (len > shown) Logger::instance().print(" ...");
        Logger::instance().println();
        return;
    }

    switch (routing.which_variant) {
        case meshtastic_Routing_route_request_tag: {
            Logger::instance().printf(
                "[RAK][ROUTING] from=%u to=%u ch=%u type=route_request pktId=%u reqId=%u dataDest=%u dataSrc=%u "
                "pri=%s(%u) hop=%u/%u snr=%.2f rssi=%d hops=%u\n",
                                      from,
                                      to,
                                      channel,
                                      pkt ? pkt->id : 0U,
                                      data ? data->request_id : 0U,
                                      data ? data->dest : 0U,
                                      data ? data->source : 0U,
                                      pkt ? priorityToString(pkt->priority) : "UNKNOWN",
                                      pkt ? static_cast<unsigned>(pkt->priority) : 0U,
                                      pkt ? static_cast<unsigned>(pkt->hop_limit) : 0U,
                                      pkt ? static_cast<unsigned>(pkt->hop_start) : 0U,
                                      pkt ? pkt->rx_snr : 0.0f,
                                      pkt ? static_cast<int>(pkt->rx_rssi) : 0,
                                      static_cast<unsigned>(routing.route_request.route_count));

            Logger::instance().print("[RAK][ROUTING] route=");
            for (std::size_t i = 0; i < routing.route_request.route_count; ++i) {
                Logger::instance().print(routing.route_request.route[i]);
                if (i + 1 < routing.route_request.route_count) Logger::instance().print("->");
            }
            Logger::instance().println();
            return;
        }

        case meshtastic_Routing_route_reply_tag: {
            Logger::instance().printf(
                "[RAK][ROUTING] from=%u to=%u ch=%u type=route_reply pktId=%u reqId=%u dataDest=%u dataSrc=%u "
                "pri=%s(%u) hop=%u/%u snr=%.2f rssi=%d hops=%u back_hops=%u\n",
                                      from,
                                      to,
                                      channel,
                                      pkt ? pkt->id : 0U,
                                      data ? data->request_id : 0U,
                                      data ? data->dest : 0U,
                                      data ? data->source : 0U,
                                      pkt ? priorityToString(pkt->priority) : "UNKNOWN",
                                      pkt ? static_cast<unsigned>(pkt->priority) : 0U,
                                      pkt ? static_cast<unsigned>(pkt->hop_limit) : 0U,
                                      pkt ? static_cast<unsigned>(pkt->hop_start) : 0U,
                                      pkt ? pkt->rx_snr : 0.0f,
                                      pkt ? static_cast<int>(pkt->rx_rssi) : 0,
                                      static_cast<unsigned>(routing.route_reply.route_count),
                                      static_cast<unsigned>(routing.route_reply.route_back_count));

            Logger::instance().print("[RAK][ROUTING] route=");
            for (std::size_t i = 0; i < routing.route_reply.route_count; ++i) {
                Logger::instance().print(routing.route_reply.route[i]);
                if (i + 1 < routing.route_reply.route_count) Logger::instance().print("->");
            }
            Logger::instance().println();

            if (routing.route_reply.route_back_count > 0) {
                Logger::instance().print("[RAK][ROUTING] back=");
                for (std::size_t i = 0; i < routing.route_reply.route_back_count; ++i) {
                    Logger::instance().print(routing.route_reply.route_back[i]);
                    if (i + 1 < routing.route_reply.route_back_count) Logger::instance().print("->");
                }
                Logger::instance().println();
            }

            return;
        }

        case meshtastic_Routing_error_reason_tag: {
            const bool isAck = (routing.error_reason == meshtastic_Routing_Error_NONE);
            Logger::instance().printf(
                "[RAK][ROUTING] from=%u to=%u ch=%u type=%s pktId=%u reqId=%u dataDest=%u dataSrc=%u "
                "replyId=%u bitfield=%u/%u pri=%s(%u) hop=%u/%u snr=%.2f rssi=%d reason=%s(%d)\n",
                                      from,
                                      to,
                                      channel,
                                      isAck ? "ack" : "error",
                                      pkt ? pkt->id : 0U,
                                      data ? data->request_id : 0U,
                                      data ? data->dest : 0U,
                                      data ? data->source : 0U,
                                      data ? data->reply_id : 0U,
                                      (data && data->has_bitfield) ? 1U : 0U,
                                      data ? static_cast<unsigned>(data->bitfield) : 0U,
                                      pkt ? priorityToString(pkt->priority) : "UNKNOWN",
                                      pkt ? static_cast<unsigned>(pkt->priority) : 0U,
                                      pkt ? static_cast<unsigned>(pkt->hop_limit) : 0U,
                                      pkt ? static_cast<unsigned>(pkt->hop_start) : 0U,
                                      pkt ? pkt->rx_snr : 0.0f,
                                      pkt ? static_cast<int>(pkt->rx_rssi) : 0,
                                      routingErrorToString(routing.error_reason),
                                      static_cast<int>(routing.error_reason));
            return;
        }

        default: {
            Logger::instance().printf("[RAK][ROUTING] from=%u to=%u ch=%u type=unknown (%u) len=%u\n",
                                      from,
                                      to,
                                      channel,
                                      static_cast<unsigned>(routing.which_variant),
                                      static_cast<unsigned>(len));
            return;
        }
    }
}

std::string_view trimLeft(std::string_view s) {
    while (!s.empty() && isAsciiWhitespace(s.front())) {
        s.remove_prefix(1);
    }
    return s;
}

std::string_view trim(std::string_view s) {
    s = trimLeft(s);
    while (!s.empty() && isAsciiWhitespace(s.back())) {
        s.remove_suffix(1);
    }
    return s;
}

std::string_view nextToken(std::string_view& s) {
    s = trimLeft(s);
    if (s.empty()) {
        return {};
    }

    std::size_t end = 0;
    while (end < s.size() && !isAsciiWhitespace(s[end])) {
        ++end;
    }

    const std::string_view tok = s.substr(0, end);
    s.remove_prefix(end);
    return tok;
}

bool tryExtractCommandPayload(std::string_view msg, std::string_view expectedCmd, std::string_view& outPayload) {
    msg = trim(msg);
    if (msg.size() < 3 || msg.front() != '[') {
        return false;
    }

    const std::size_t close = msg.find(']');
    if (close == std::string_view::npos) {
        return false;
    }

    std::string_view inside = msg.substr(1, close - 1);
    std::string_view after = msg.substr(close + 1);

    inside = trim(inside);
    after = trim(after);

    if (inside.empty()) {
        return false;
    }

    std::string_view tmp = inside;
    const std::string_view name = nextToken(tmp);
    if (!asciiIEquals(name, expectedCmd)) {
        return false;
    }

    tmp = trim(tmp);
    if (!tmp.empty()) {
        outPayload = tmp;
        return true;  // [CMD payload...]
    }

    if (!after.empty()) {
        outPayload = after;
        return true;  // [CMD] payload...
    }

    return false;
}

struct ParsedCommand {
    std::string_view name;
    std::string_view arg0;
    std::string_view arg1;
};

bool tryParseBracketCommand(std::string_view msg, ParsedCommand& out) {
    msg = trim(msg);
    if (msg.size() < 3 || msg.front() != '[') {
        return false;
    }

    const std::size_t close = msg.find(']');
    if (close == std::string_view::npos) {
        return false;
    }

    std::string_view inside = msg.substr(1, close - 1);
    std::string_view after = msg.substr(close + 1);

    out = {};

    out.name = nextToken(inside);
    if (out.name.empty()) {
        return false;
    }

    out.arg0 = nextToken(inside);
    if (out.arg0.empty()) {
        out.arg0 = nextToken(after);
    }

    out.arg1 = nextToken(inside);
    if (out.arg1.empty()) {
        out.arg1 = nextToken(after);
    }

    return true;
}

const DBTable* findKnownTable(std::string_view name) {
    if (asciiIEquals(name, KeyboardConfig::Tables::Inputs.tableName)) {
        return &KeyboardConfig::Tables::Inputs;
    }

    if (asciiIEquals(name, KeyboardConfig::Tables::RadioMasters.tableName)) {
        return &KeyboardConfig::Tables::RadioMasters;
    }

    if (asciiIEquals(name, KeyboardConfig::Tables::Logs.tableName)) {
        return &KeyboardConfig::Tables::Logs;
    }

    return nullptr;
}

std::size_t parseSizeOrDefault(std::string_view s, std::size_t fallback) {
    s = trim(s);
    if (s.empty()) {
        return fallback;
    }

    // strtoul needs a null-terminated string; commands are small so this is fine.
    std::string tmp(s);
    char* end = nullptr;
    errno = 0;
    const unsigned long v = std::strtoul(tmp.c_str(), &end, 10);
    if (errno != 0 || end == nullptr || end == tmp.c_str() || *end != '\0') {
        return fallback;
    }
    return static_cast<std::size_t>(v);
}

bool tryParseLeadingU64(std::string_view s, std::uint64_t& out) {
    out = 0;
    s = trim(s);
    if (s.empty()) {
        return false;
    }

    std::size_t n = 0;
    while (n < s.size() && s[n] >= '0' && s[n] <= '9') {
        ++n;
    }

    if (n == 0) {
        return false;
    }

    std::string tmp(s.substr(0, n));
    char* end = nullptr;
    errno = 0;
    const unsigned long long v = std::strtoull(tmp.c_str(), &end, 10);
    if (errno != 0 || end == nullptr || end == tmp.c_str() || *end != '\0') {
        return false;
    }

    out = static_cast<std::uint64_t>(v);
    return true;
}

const std::array<const DBTable*, 3>& queryTables() {
    static const std::array<const DBTable*, 3> tables = {
        &KeyboardConfig::Tables::Inputs,
        &KeyboardConfig::Tables::RadioMasters,
        &KeyboardConfig::Tables::Logs,
    };
    return tables;
}

const DBTable* resolveQueryTableSelection(std::string_view token) {
    token = trim(token);
    if (token.empty()) {
        return nullptr;
    }

    std::uint64_t idx = 0;
    if (tryParseLeadingU64(token, idx)) {
        const auto& tables = queryTables();
        if (idx >= 1 && idx <= tables.size()) {
            return tables[static_cast<std::size_t>(idx - 1)];
        }
        return nullptr;
    }

    return findKnownTable(token);
}
}  // namespace

RakManager& RakManager::getInstance() {
    static RakManager instance;
    return instance;
}

RakManager::RakManager() {
    loadSettingsFromDb();
}

void RakManager::dbReplyCallback(std::uint32_t dest, std::uint8_t channel, std::string text) {
    RakManager::getInstance().transport_.enqueueTextReliable(dest, channel, std::move(text));
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

    if (portNum == meshtastic_PortNum_ROUTING_APP) {
        logRoutingAppPacket(from, to, channel, payload);
        return;
    }

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
        Logger::instance().printf("[RAK][DM] from=%u channel=%u\n", from, channel);

        std::string suppliedSecret;
        if (tryExtractEnrollmentSecret(text, suppliedSecret)) {
            Logger::instance().printf("[RAK][PAIR] request from=%u\n", from);
            if (!isEnrollmentSecretConfigured()) {
                Logger::instance().println("[RAK] Enrollment disabled: set a strong MasterEnrollmentSecret in KeyboardConfig.");
                instance.transport_.enqueueTextReliable(from, channel,
                                                "[RAK] Enrollment disabled. Set MasterEnrollmentSecret in KeyboardConfig and reflash.");
                return;
            }

            if (!constantTimeEquals(suppliedSecret, KeyboardConfig::Security::MasterEnrollmentSecret)) {
                Logger::instance().printf("[RAK] Enrollment rejected for node %u: invalid credentials.\n", from);
                instance.transport_.enqueueTextReliable(from, channel, "[RAK] Pair rejected: invalid secret.");
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
                instance.transport_.enqueueTextReliable(from, channel, "[RAK] Already paired.");
            } else if (added) {
                DatabaseManager::getInstance().saveData({std::to_string(from)}, KeyboardConfig::Tables::RadioMasters);
                Logger::instance().printf("[RAK] Added new master node: %u\n", from);
                instance.transport_.enqueueTextReliable(from, channel, "[RAK] Paired OK. Use [HELP].");
            }
            return;
        }

        const bool typingSessionActive = instance.typingSession_.has_value();
        const bool isTypingOwner = typingSessionActive && (instance.typingSession_->owner == from);

        const bool querySessionActive = instance.querySession_.has_value();
        const bool isQueryOwner = querySessionActive && (instance.querySession_->owner == from);

        auto reply = [&](std::string_view line) {
            if (line.empty()) {
                return;
            }
            instance.transport_.enqueueTextReliable(from, channel, std::string(line));
        };

        auto isMaster = [&]() {
            Threads::Scope scope(instance.mastersMutex_);
            const std::uint64_t fromAddr = static_cast<std::uint64_t>(from);
            return std::any_of(instance.mastersAddresses.cbegin(), instance.mastersAddresses.cend(),
                               [&](const KeyboardConfig::NodeInfo& n) { return n.address == fromAddr; });
        };

        // In TYPE session mode, any DM text from the session owner is injected into the host keyboard,
        // except the reserved closing command [/TYPE]. We handle [TYPE]/[/TYPE] immediately so the
        // session state is updated before any subsequent messages processed by the same mt_loop() call.
        ParsedCommand cmd{};
        if (tryParseBracketCommand(text, cmd)) {
            if (asciiIEquals(cmd.name, "TYPE")) {
                if (!isMaster()) {
                    reply("[RAK] Unauthorized. Pair first (DM): PAIR:<secret>");
                    return;
                }

                if (querySessionActive) {
                    char buf[96]{};
                    std::snprintf(buf, sizeof(buf), "[RAK] TYPE: busy (QUERY session owned by %u).",
                                  static_cast<unsigned>(instance.querySession_->owner));
                    reply(buf);
                    return;
                }

                const std::uint32_t nowMs = millis();
                if (instance.typingSession_.has_value() && instance.typingSession_->owner != from) {
                    char buf[96]{};
                    std::snprintf(buf, sizeof(buf), "[RAK] TYPE: busy (session owned by %u).", 
                                  static_cast<unsigned>(instance.typingSession_->owner));
                    reply(buf);
                    return;
                }

                if (!instance.typingSession_.has_value()) {
                    instance.typingSession_ = TypingSession{
                        .owner = from,
                        .channel = channel,
                        .lastActivityMs = nowMs,
                    };
                    reply("[RAK] TYPE: session started. Send [/TYPE] to end (timeout 300s).");
                    reply("[RAK] Escapes: \\\\n \\\\t \\\\r \\\\\\\\");
                } else {
                    instance.typingSession_->channel = channel;
                    instance.typingSession_->lastActivityMs = nowMs;
                    reply("[RAK] TYPE: session already active. Send [/TYPE] to end.");
                }

                std::string_view payload;
                if (tryExtractCommandPayload(text, "TYPE", payload)) {
                    payload = trim(payload);
                    if (!payload.empty()) {
                        const std::string phrase = unescapeRadioText(payload);
                        if (!HostKeyboard::instance().enqueueTypeText(phrase)) {
                            reply("[RAK] TYPE: queue full (slow down).");
                        }
                        instance.typingSession_->lastActivityMs = nowMs;
                        Logger::instance().printf("[RAK][TYPE] session cmd payload from=%u len=%u\n", from,
                                                  static_cast<unsigned>(phrase.size()));
                    }
                }

                return;
            }

            if (asciiIEquals(cmd.name, "/TYPE")) {
                if (!isMaster()) {
                    reply("[RAK] Unauthorized. Pair first (DM): PAIR:<secret>");
                    return;
                }

                if (!instance.typingSession_.has_value()) {
                    reply("[RAK] TYPE: no active session.");
                    return;
                }

                if (instance.typingSession_->owner != from) {
                    char buf[96]{};
                    std::snprintf(buf, sizeof(buf), "[RAK] TYPE: session owned by %u.",
                                  static_cast<unsigned>(instance.typingSession_->owner));
                    reply(buf);
                    return;
                }

                instance.typingSession_.reset();
                HostKeyboard::instance().cancelAll();
                reply("[RAK] TYPE: session ended.");
                Logger::instance().printf("[RAK][TYPE] session ended by owner=%u\n", from);
                return;
            }

            if (asciiIEquals(cmd.name, "QUERY")) {
                if (!isMaster()) {
                    reply("[RAK] Unauthorized. Pair first (DM): PAIR:<secret>");
                    return;
                }

                const std::uint32_t nowMs = millis();

                if (typingSessionActive) {
                    char buf[96]{};
                    std::snprintf(buf, sizeof(buf), "[RAK] QUERY: busy (TYPE session owned by %u).",
                                  static_cast<unsigned>(instance.typingSession_->owner));
                    reply(buf);
                    return;
                }

                if (instance.querySession_.has_value() && instance.querySession_->owner != from) {
                    char buf[96]{};
                    std::snprintf(buf, sizeof(buf), "[RAK] QUERY: busy (session owned by %u).",
                                  static_cast<unsigned>(instance.querySession_->owner));
                    reply(buf);
                    return;
                }

                if (!instance.querySession_.has_value()) {
                    instance.querySession_ = QuerySession{
                        .owner = from,
                        .channel = channel,
                        .lastActivityMs = nowMs,
                        .selectedTable = nullptr,
                    };
                    Logger::instance().printf("[RAK][QUERY] session started owner=%u ch=%u\n", from, channel);
                    reply("[RAK] QUERY: session started. Reply with a table number/name. Send [/QUERY] to end (timeout 300s).");
                } else {
                    instance.querySession_->channel = channel;
                    instance.querySession_->lastActivityMs = nowMs;
                    Logger::instance().printf("[RAK][QUERY] session already active owner=%u ch=%u\n", from, channel);
                    reply("[RAK] QUERY: session already active. Reply with a table number/name, or send TABLES.");
                }

                reply("[RAK] Tables:");
                const auto& tables = queryTables();
                for (std::size_t i = 0; i < tables.size(); ++i) {
                    reply(std::to_string(i + 1) + ") " + tables[i]->tableName);
                }
                reply("[RAK] Options: TABLES, COUNT, SCHEMA, RANDOM, ROW <id>, SECRETS, [/QUERY]");
                return;
            }

            if (asciiIEquals(cmd.name, "/QUERY")) {
                if (!isMaster()) {
                    reply("[RAK] Unauthorized. Pair first (DM): PAIR:<secret>");
                    return;
                }

                if (!instance.querySession_.has_value()) {
                    reply("[RAK] QUERY: no active session.");
                    return;
                }

                if (instance.querySession_->owner != from) {
                    char buf[96]{};
                    std::snprintf(buf, sizeof(buf), "[RAK] QUERY: session owned by %u.",
                                  static_cast<unsigned>(instance.querySession_->owner));
                    reply(buf);
                    return;
                }

                instance.querySession_.reset();
                reply("[RAK] QUERY: session ended.");
                Logger::instance().printf("[RAK][QUERY] session ended by owner=%u\n", from);
                return;
            }

            if (isTypingOwner) {
                instance.typingSession_->lastActivityMs = millis();
                instance.typingSession_->channel = channel;

                const std::string phrase = unescapeRadioText(text);
                if (!HostKeyboard::instance().enqueueTypeText(phrase)) {
                    instance.transport_.enqueueTextReliable(from, channel, "[RAK] TYPE: queue full (slow down).");
                }
                Logger::instance().printf("[RAK][TYPE] session text from=%u len=%u\n", from,
                                          static_cast<unsigned>(phrase.size()));
                return;
            }

            if (isQueryOwner) {
                instance.querySession_->lastActivityMs = millis();
                instance.querySession_->channel = channel;
                instance.enqueueCommand(from, to, channel, text);
                Logger::instance().printf("[RAK][QUERY] enqueued text from=%u len=%u\n", from,
                                          static_cast<unsigned>(std::strlen(text)));
                return;
            }

            const bool known =
                asciiIEquals(cmd.name, "HELP") || asciiIEquals(cmd.name, "QUERY") || asciiIEquals(cmd.name, "TABLES") ||
                asciiIEquals(cmd.name, "SCHEMA") || asciiIEquals(cmd.name, "COUNT") || asciiIEquals(cmd.name, "TAIL");
            if (known) {
                Logger::instance().printf("[RAK][CMD] queued from=%u cmd=%.*s arg0=%.*s arg1=%.*s\n",
                                          from,
                                          static_cast<int>(cmd.name.size()),
                                          cmd.name.data(),
                                          static_cast<int>(cmd.arg0.size()),
                                          cmd.arg0.data(),
                                          static_cast<int>(cmd.arg1.size()),
                                          cmd.arg1.data());
                instance.enqueueCommand(from, to, channel, text);
                return;
            }
        } else if (isTypingOwner) {
            instance.typingSession_->lastActivityMs = millis();
            instance.typingSession_->channel = channel;

            const std::string phrase = unescapeRadioText(text);
            if (!HostKeyboard::instance().enqueueTypeText(phrase)) {
                instance.transport_.enqueueTextReliable(from, channel, "[RAK] TYPE: queue full (slow down).");
            }
            Logger::instance().printf("[RAK][TYPE] session text from=%u len=%u\n", from,
                                      static_cast<unsigned>(phrase.size()));
            return;
        } else if (isQueryOwner) {
            instance.querySession_->lastActivityMs = millis();
            instance.querySession_->channel = channel;
            instance.enqueueCommand(from, to, channel, text);
            Logger::instance().printf("[RAK][QUERY] enqueued text from=%u len=%u\n", from,
                                      static_cast<unsigned>(std::strlen(text)));
            return;
        }
    }

    if (std::strlen(text) > 1) {
        NlpManager::getInstance().analyzeSentence(text);
    }
}

void RakManager::enqueueCommand(uint32_t from, uint32_t to, uint8_t channel, const char* text) {
    if (text == nullptr) {
        return;
    }

    constexpr std::size_t kMaxPendingCommands = 8;
    constexpr std::size_t kMaxLoggedChars = 80;

    PendingCommand cmd;
    cmd.from = from;
    cmd.to = to;
    cmd.channel = channel;
    cmd.text = text;

    Threads::Scope scope(commandsMutex_);
    if (pendingCommands_.size() >= kMaxPendingCommands) {
        // Drop oldest to keep the system responsive.
        pendingCommands_.pop_front();
        Logger::instance().println("[RAK][CMD] command queue full, dropping oldest");
    }
    pendingCommands_.push_back(std::move(cmd));

    const auto& queued = pendingCommands_.back();
    const std::size_t shown = std::min<std::size_t>(queued.text.size(), kMaxLoggedChars);
    Logger::instance().printf("[RAK][CMD] command enqueued from=%u pending=%u text=\"%.*s%s\"\n",
                              queued.from,
                              static_cast<unsigned>(pendingCommands_.size()),
                              static_cast<int>(shown),
                              queued.text.c_str(),
                              (queued.text.size() > shown) ? "..." : "");
}

void RakManager::processCommands() {
    PendingCommand cmd;
    {
        Threads::Scope scope(commandsMutex_);
        if (pendingCommands_.empty()) {
            return;
        }

        cmd = std::move(pendingCommands_.front());
        pendingCommands_.pop_front();
    }

    const bool isMaster = [&]() {
        Threads::Scope scope(mastersMutex_);
        const std::uint64_t fromAddr = static_cast<std::uint64_t>(cmd.from);
        return std::any_of(mastersAddresses.cbegin(), mastersAddresses.cend(), [&](const KeyboardConfig::NodeInfo& n) {
            return n.address == fromAddr;
        });
    }();

    if (!isMaster) {
        Logger::instance().printf("[RAK][CMD] unauthorized from=%u\n", cmd.from);
        transport_.enqueueTextReliable(cmd.from, cmd.channel, "[RAK] Unauthorized. Pair first (DM): PAIR:<secret>");
        return;
    }

    auto send = [&](std::string_view line) {
        if (line.empty()) {
            return;
        }
        Logger::instance().printf("[RAK][CMD] reply to=%u len=%u\n", cmd.from, static_cast<unsigned>(line.size()));
        transport_.enqueueTextReliable(cmd.from, cmd.channel, std::string(line));
    };

    const bool inQuerySession = querySession_.has_value() && (querySession_->owner == cmd.from);
	        if (inQuerySession) {
        querySession_->lastActivityMs = millis();
        querySession_->channel = cmd.channel;

        auto sendQueryTables = [&]() {
            send("[RAK] Tables:");
            const auto& tables = queryTables();
            for (std::size_t i = 0; i < tables.size(); ++i) {
                send(std::to_string(i + 1) + ") " + tables[i]->tableName);
            }
        };

        auto sendQueryHelp = [&]() {
            send("[RAK] QUERY session:");
            send(" - Pick a table: reply with its number or name.");
            send(" - Then: send a rowid number, or: ROW <id>, RANDOM, COUNT, SCHEMA.");
            send(" - Extra: SECRETS (top unusual words from Inputs).");
            send(" - TABLES/BACK to pick another table, [/QUERY] to exit.");
            sendQueryTables();
        };

	        auto sendTableSelectedHeader = [&](const DBTable& table) {
	            send(std::string("[RAK] QUERY: selected table ") + table.tableName + ". Fetching sample...");
	            if (!DatabaseManager::getInstance().enqueueQueryTableIntro(cmd.from, cmd.channel, table)) {
	                send("[RAK] DB busy (job queue full). Try again.");
	            }
	        };

        auto ensureSelectedTable = [&]() -> const DBTable* {
            if (querySession_->selectedTable != nullptr) {
                return querySession_->selectedTable;
            }
            return nullptr;
        };

        const std::string_view msg = trim(cmd.text);
        if (msg.empty()) {
            sendQueryHelp();
            return;
        }

        ParsedCommand parsed{};
        if (tryParseBracketCommand(msg, parsed)) {
            Logger::instance().printf("[RAK][QUERY] cmd=%.*s arg0=%.*s arg1=%.*s\n",
                                      static_cast<int>(parsed.name.size()),
                                      parsed.name.data(),
                                      static_cast<int>(parsed.arg0.size()),
                                      parsed.arg0.data(),
                                      static_cast<int>(parsed.arg1.size()),
                                      parsed.arg1.data());

            if (asciiIEquals(parsed.name, "HELP")) {
                sendQueryHelp();
                return;
            }

            if (asciiIEquals(parsed.name, "TABLES") || asciiIEquals(parsed.name, "BACK")) {
                querySession_->selectedTable = nullptr;
                sendQueryTables();
                return;
            }

	            if (asciiIEquals(parsed.name, "SECRETS")) {
	                send("[RAK] SECRETS: computing...");
	                if (!DatabaseManager::getInstance().enqueueTopSecrets(cmd.from, cmd.channel, 5)) {
	                    send("[RAK] DB busy (job queue full). Try again.");
	                }
	                return;
	            }

            if (asciiIEquals(parsed.name, "SCHEMA")) {
                const auto* table = parsed.arg0.empty() ? ensureSelectedTable() : resolveQueryTableSelection(parsed.arg0);
                if (table == nullptr) {
                    send("[RAK] SCHEMA: select a table first (TABLES).");
                    return;
                }
                send(std::string("[RAK] Schema ") + table->tableName + ":");
                for (const auto& col : table->columns) {
                    send(" - " + col.name + " " + col.type);
                }
                return;
            }

	            if (asciiIEquals(parsed.name, "COUNT")) {
	                const auto* table = parsed.arg0.empty() ? ensureSelectedTable() : resolveQueryTableSelection(parsed.arg0);
	                if (table == nullptr) {
	                    send("[RAK] COUNT: select a table first (TABLES).");
	                    return;
	                }
	                send("[RAK] COUNT: working...");
	                if (!DatabaseManager::getInstance().enqueueCountRows(cmd.from, cmd.channel, *table)) {
	                    send("[RAK] DB busy (job queue full). Try again.");
	                }
	                return;
	            }

	            if (asciiIEquals(parsed.name, "RANDOM")) {
	                const auto* table = parsed.arg0.empty() ? ensureSelectedTable() : resolveQueryTableSelection(parsed.arg0);
	                if (table == nullptr) {
	                    send("[RAK] RANDOM: select a table first (TABLES).");
	                    return;
	                }
	                send("[RAK] RANDOM: working...");
	                if (!DatabaseManager::getInstance().enqueueRandomRow(cmd.from, cmd.channel, *table)) {
	                    send("[RAK] DB busy (job queue full). Try again.");
	                }
	                return;
	            }

	            if (asciiIEquals(parsed.name, "ROW")) {
                const DBTable* table = ensureSelectedTable();
                std::uint64_t rowid = 0;

                if (!parsed.arg1.empty()) {
                    const auto* maybeTable = resolveQueryTableSelection(parsed.arg0);
                    if (maybeTable != nullptr && tryParseLeadingU64(parsed.arg1, rowid)) {
                        table = maybeTable;
                    } else if (tryParseLeadingU64(parsed.arg0, rowid)) {
                        // ROW <id>
                    }
                } else if (!parsed.arg0.empty()) {
                    if (!tryParseLeadingU64(parsed.arg0, rowid)) {
                        const auto* maybeTable = resolveQueryTableSelection(parsed.arg0);
                        if (maybeTable != nullptr) {
                            querySession_->selectedTable = maybeTable;
                            sendTableSelectedHeader(*maybeTable);
                            return;
                        }
                    }
                }

                if (table == nullptr) {
                    send("[RAK] ROW: select a table first (TABLES).");
                    return;
                }

	                if (rowid == 0) {
	                    send("[RAK] ROW: specify a rowid (ROW <id>).");
	                    return;
	                }
	                send("[RAK] ROW: working...");
	                if (!DatabaseManager::getInstance().enqueueRowByRowid(cmd.from, cmd.channel, *table, rowid)) {
	                    send("[RAK] DB busy (job queue full). Try again.");
	                }
	                return;
	            }

            // Allow selecting table via [Inputs] etc.
            if (const auto* table = resolveQueryTableSelection(parsed.name); table != nullptr) {
                querySession_->selectedTable = table;
                sendTableSelectedHeader(*table);
                return;
            }

            send("[RAK] QUERY: unknown command. Send HELP.");
            return;
        }

        std::string_view s = msg;
        const std::string_view tok0 = nextToken(s);
        const std::string_view tok1 = nextToken(s);

        if (tok0.empty()) {
            sendQueryHelp();
            return;
        }

        if (asciiIEquals(tok0, "HELP")) {
            sendQueryHelp();
            return;
        }

        if (asciiIEquals(tok0, "TABLES") || asciiIEquals(tok0, "BACK")) {
            querySession_->selectedTable = nullptr;
            sendQueryTables();
            return;
        }

	        if (asciiIEquals(tok0, "SECRETS")) {
	            send("[RAK] SECRETS: computing...");
	            if (!DatabaseManager::getInstance().enqueueTopSecrets(cmd.from, cmd.channel, 5)) {
	                send("[RAK] DB busy (job queue full). Try again.");
	            }
	            return;
	        }

	        if (asciiIEquals(tok0, "COUNT")) {
	            const auto* table = tok1.empty() ? ensureSelectedTable() : resolveQueryTableSelection(tok1);
	            if (table == nullptr) {
	                send("[RAK] COUNT: select a table first (TABLES).");
	                return;
	            }

	            send("[RAK] COUNT: working...");
	            if (!DatabaseManager::getInstance().enqueueCountRows(cmd.from, cmd.channel, *table)) {
	                send("[RAK] DB busy (job queue full). Try again.");
	            }
	            return;
	        }

        if (asciiIEquals(tok0, "SCHEMA")) {
            const auto* table = tok1.empty() ? ensureSelectedTable() : resolveQueryTableSelection(tok1);
            if (table == nullptr) {
                send("[RAK] SCHEMA: select a table first (TABLES).");
                return;
            }
            send(std::string("[RAK] Schema ") + table->tableName + ":");
            for (const auto& col : table->columns) {
                send(" - " + col.name + " " + col.type);
            }
            return;
        }

	        if (asciiIEquals(tok0, "RANDOM")) {
	            const auto* table = tok1.empty() ? ensureSelectedTable() : resolveQueryTableSelection(tok1);
	            if (table == nullptr) {
	                send("[RAK] RANDOM: select a table first (TABLES).");
	                return;
	            }
	            send("[RAK] RANDOM: working...");
	            if (!DatabaseManager::getInstance().enqueueRandomRow(cmd.from, cmd.channel, *table)) {
	                send("[RAK] DB busy (job queue full). Try again.");
	            }
	            return;
	        }

	        if (asciiIEquals(tok0, "ROW")) {
	            const auto* table = ensureSelectedTable();
	            if (table == nullptr) {
	                send("[RAK] ROW: select a table first (TABLES).");
	                return;
	            }

            std::uint64_t rowid = 0;
            if (!tryParseLeadingU64(tok1, rowid) || rowid == 0) {
                send("[RAK] ROW: specify a rowid (ROW <id>).");
                return;
            }

	            send("[RAK] ROW: working...");
	            if (!DatabaseManager::getInstance().enqueueRowByRowid(cmd.from, cmd.channel, *table, rowid)) {
	                send("[RAK] DB busy (job queue full). Try again.");
	            }
	            return;
	        }

        if (querySession_->selectedTable == nullptr) {
            // Table selection is only interpreted from numeric indices before a table is selected.
            if (const auto* table = resolveQueryTableSelection(tok0); table != nullptr) {
                querySession_->selectedTable = table;
                sendTableSelectedHeader(*table);
                return;
            }

            send("[RAK] QUERY: pick a table first (TABLES).");
            return;
        }

        // Row selection by raw rowid (after table selection).
	        {
	            std::uint64_t rowid = 0;
	            if (tryParseLeadingU64(tok0, rowid) && rowid != 0) {
	                send("[RAK] ROW: working...");
	                if (!DatabaseManager::getInstance().enqueueRowByRowid(cmd.from, cmd.channel, *querySession_->selectedTable, rowid)) {
	                    send("[RAK] DB busy (job queue full). Try again.");
	                }
	                return;
	            }
	        }

        // Allow switching table by name without requiring TABLES/BACK.
        if (const auto* table = findKnownTable(tok0); table != nullptr) {
            querySession_->selectedTable = table;
            sendTableSelectedHeader(*table);
            return;
        }

        send("[RAK] QUERY: unrecognized. Send HELP or TABLES.");
        return;
    }

    ParsedCommand parsed{};
    if (!tryParseBracketCommand(cmd.text, parsed)) {
        Logger::instance().println("[RAK][CMD] parse failed (ignored)");
        return;
    }

    Logger::instance().printf("[RAK][CMD] processing from=%u channel=%u cmd=%.*s arg0=%.*s arg1=%.*s\n",
                              cmd.from,
                              cmd.channel,
                              static_cast<int>(parsed.name.size()),
                              parsed.name.data(),
                              static_cast<int>(parsed.arg0.size()),
                              parsed.arg0.data(),
                              static_cast<int>(parsed.arg1.size()),
                              parsed.arg1.data());

    auto sendTables = [&]() {
        send("[RAK] Available tables:");
        send(" - Inputs");
        send(" - RadioMasters");
        send(" - Logs");
    };

    if (asciiIEquals(parsed.name, "HELP")) {
        send("[RAK] Commands:");
        send(" - Pair (DM): PAIR:<secret>");
        send(" - [HELP]");
        send(" - [TYPE] (start typing)  [/TYPE] (end)");
        send(" - [QUERY] (start DB session)  [/QUERY] (end)");
        send(" - [TABLES]  [SCHEMA <table>]  [COUNT <table>]  [TAIL Inputs <n>]");
        send(" - In QUERY session you can reply with: table#, table name, ROW <id>, RANDOM, SECRETS.");
        return;
    }

    if (asciiIEquals(parsed.name, "TABLES")) {
        sendTables();
        return;
    }

    if (asciiIEquals(parsed.name, "SCHEMA")) {
        const auto* table = findKnownTable(parsed.arg0);
        if (table == nullptr) {
            send("[RAK] Unknown table. Use [TABLES].");
            return;
        }

        send(std::string("[RAK] Schema ") + table->tableName + ":");
        for (const auto& col : table->columns) {
            send(" - " + col.name + " " + col.type);
        }
        return;
    }

    if (asciiIEquals(parsed.name, "COUNT")) {
        const auto* table = findKnownTable(parsed.arg0);
        if (table == nullptr) {
            send("[RAK] Unknown table. Use [TABLES].");
            return;
        }
        send("[RAK] COUNT: working...");
        if (!DatabaseManager::getInstance().enqueueCountRows(cmd.from, cmd.channel, *table)) {
            send("[RAK] DB busy (job queue full). Try again.");
        }
        return;
    }

    if (asciiIEquals(parsed.name, "TAIL")) {
        const auto* table = findKnownTable(parsed.arg0);
        if (table == nullptr) {
            send("[RAK] Unknown table. Use [TABLES].");
            return;
        }

        if (table == &KeyboardConfig::Tables::Inputs) {
            const std::size_t n = parseSizeOrDefault(parsed.arg1, 5);
            send("[RAK] TAIL Inputs (latest first):");
            if (!DatabaseManager::getInstance().enqueueTailInputs(cmd.from, cmd.channel, n)) {
                send("[RAK] DB busy (job queue full). Try again.");
            }
            return;
        }

        if (table == &KeyboardConfig::Tables::RadioMasters) {
            send("[RAK] RadioMasters:");
            if (!DatabaseManager::getInstance().enqueueListRadioMasters(cmd.from, cmd.channel)) {
                send("[RAK] DB busy (job queue full). Try again.");
            }
            return;
        }
    }

    send("[RAK] Unknown command. Use [HELP].");
}

void RakManager::pollTypingSessionTimeout(std::uint32_t now) {
    if (!typingSession_.has_value()) {
        return;
    }

    constexpr std::uint32_t kTimeoutMs = 5U * 60U * 1000U;
    if (now - typingSession_->lastActivityMs < kTimeoutMs) {
        return;
    }

    const auto owner = typingSession_->owner;
    const auto channel = typingSession_->channel;

    typingSession_.reset();
    HostKeyboard::instance().cancelAll();
    transport_.enqueueTextReliable(owner, channel, "[RAK] TYPE: session ended (timeout).");
}

void RakManager::pollQuerySessionTimeout(std::uint32_t now) {
    if (!querySession_.has_value()) {
        return;
    }

    constexpr std::uint32_t kTimeoutMs = 5U * 60U * 1000U;
    if (now - querySession_->lastActivityMs < kTimeoutMs) {
        return;
    }

    const auto owner = querySession_->owner;
    const auto channel = querySession_->channel;
    querySession_.reset();
    transport_.enqueueTextReliable(owner, channel, "[RAK] QUERY: session ended (timeout).");
    Logger::instance().printf("[RAK][QUERY] session ended by timeout owner=%u\n", static_cast<unsigned>(owner));
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
    DatabaseManager::getInstance().setReplyCallback(RakManager::dbReplyCallback);

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
    static bool loggedNodeNum = false;
    auto& instance = RakManager::getInstance();

    while (true) {
        const std::uint32_t now = millis();
        mt_loop(now);

        if (!loggedNodeNum && my_node_num != 0U) {
            Logger::instance().printf("[RAK] my_node_num=%u\n", static_cast<unsigned>(my_node_num));
            loggedNodeNum = true;
        }

        // Handle queued commands outside of mt_loop() callbacks.
        instance.processCommands();
        instance.pollTypingSessionTimeout(now);
        instance.pollQuerySessionTimeout(now);

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
