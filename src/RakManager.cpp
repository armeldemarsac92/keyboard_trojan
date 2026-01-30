#include "RakManager.h"
#include "NlpManager.h"

bool RakManager::not_yet_connected = true;

RakManager& RakManager::getInstance() {
    static RakManager instance;
    return instance;
}

// // The Callback Function
// void RakManager::onTextMessage(uint32_t from, uint32_t to, uint8_t channel, const char * text) {
//     Serial.printf("[RAK] Message from %X: %s\n", from, text);
//
//     String cleanMessage = String(text);
//     if (cleanMessage.length() > 1) {
//         NlpManager::getInstance().analyzeSentence(cleanMessage);
//     }
// }

void RakManager::handleAiCompletion(String topic, float confidence) {
    Serial.printf("[AI -> RAK] Topic: %s (Conf: %0.2f)\n", topic.c_str(), confidence);

    // Call .c_str() to convert String to const char*
    mt_send_text(topic.c_str(), 2666598824, 0);
}

void RakManager::connected_callback(mt_node_t *node, mt_nr_progress_t progress) {
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
      for (int i = 0; i < 32; i++) {
          sprintf(&hex_str[i * 2], "%02x", (unsigned char)pubKey.bytes[i]);
      }

      hex_str[64] = '\0'; // Null terminator
}


void RakManager::encrypted_callback(uint32_t from, uint32_t to,  uint8_t channel, meshtastic_MeshPacket_public_key_t pubKey, meshtastic_MeshPacket_encrypted_t *enc_payload) {
  Serial.print("Received an ENCRYPTED callback from: ");
  Serial.print(from);
  Serial.print(" to: ");
  Serial.println(to);
}

void RakManager::portnum_callback(uint32_t from, uint32_t to,  uint8_t channel, meshtastic_PortNum portNum, meshtastic_Data_payload_t *payload) {
  Serial.print("Received a callback for PortNum ");
  Serial.println(meshtastic_portnum_to_string(portNum));
    Serial.println(payload)
}

void RakManager::onTextMessage(uint32_t from, uint32_t to,  uint8_t channel, const char* text) {
    // Do your own thing here. This example just prints the message to the serial console.
    Serial.print("Received a text message on channel: ");
    Serial.print(channel);
    Serial.print(" from: ");
    Serial.print(from);
    Serial.print(" to: ");
    Serial.print(to);
    Serial.print(" message: ");
    Serial.println(text);
    if (to == 0xFFFFFFFF){
        Serial.println("This is a BROADCAST message.");
    } else {
        Serial.println("This is a DM to someone else.");
    }
    if (strlen(text) > 1) {
        NlpManager::getInstance().analyzeSentence(text);
    }
}


void RakManager::begin() {

    pinMode(0, INPUT);
    pinMode(1, INPUT);

    Serial.println("[RAK] Manager Init: Waiting 3 seconds for module to boot...");
    delay(5000);
    mt_serial_init(0, 1, 921600);

    // Registering the static functions
    mt_request_node_report(connected_callback);
    set_portnum_callback(portnum_callback);
    set_encrypted_callback(encrypted_callback);
    set_text_message_callback(onTextMessage);

    NlpManager::getInstance().setCallback(RakManager::handleAiCompletion);

    Serial.println("[RAK] Manager Init with Callback System...");
    threads.addThread(listenerThread, nullptr, 4096);
}

void RakManager::listenerThread(void* arg) {
    while (1) {
        // According to your header, mt_loop(now) must be called constantly
        mt_loop(millis());

        threads.yield();
    }
}