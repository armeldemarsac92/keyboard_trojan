#include "RakManager.h"
#include "NlpManager.h"

RakManager& RakManager::getInstance() {
    static RakManager instance;
    return instance;
}

void RakManager::begin() {
    pinMode(0, INPUT);
    pinMode(1, INPUT);

    Serial.println("[RAK] Manager Init: Waiting 3 seconds for module to boot...");
    delay(3000);

    Serial1.begin(115200);

    threads.addThread(listenerThread, nullptr, 4096);
}

void RakManager::listenerThread(void* arg) {
    while (1) {
        if (Serial1.available()) {
            Serial.println("[RAK] Listener started...");
            String incoming = Serial1.readStringUntil('\n');
            incoming.trim(); // Clean up

            if (incoming.length() > 0) {
                int separatorIndex = incoming.indexOf(':');
                String cleanMessage;

                if (separatorIndex != -1 && separatorIndex < 10) {
                    cleanMessage = incoming.substring(separatorIndex + 1);
                } else {
                    cleanMessage = incoming;
                }

                cleanMessage.trim();

                Serial.print("[RAK] Reçu: \"");
                Serial.print(cleanMessage);
                Serial.println("\"");

                if (cleanMessage.length() > 1) {
                    NlpManager::getInstance().analyzeSentence(cleanMessage);
                }
            }
        }

        threads.yield();
    }
}