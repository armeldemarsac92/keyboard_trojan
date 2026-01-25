#include "RakManager.h"

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
        // Check if data is available on the RAK port
        if (Serial1.available()) {
            Serial.println("[RAK] Listener started...");
            String incoming = Serial1.readStringUntil('\n');
            incoming.trim(); // Clean up

            if (incoming.length() > 0) {
                Serial.print("[RAK MSG]: ");
                Serial.println(incoming);
            }
        }

        // yield is crucial to let the rest of your Teensy run
        threads.yield();
    }
}