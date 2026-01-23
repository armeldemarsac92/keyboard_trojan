#include <Arduino.h>
#include <USBHost_t36.h>
#include <TeensyThreads.h>
#include <cmath>
#include <string.h>
#include <stdio.h>

#include "AzertyLayout.h"
#include "Queuing.h"
#include "DataSaver.h"
#include "KeyHelper.h"
#include "LettersBuffer.h"
#include "StatsBuffer.h"

static LettersBuffer currentLettersBuffer;
static StatsBuffer timeDeltaBetweenKeysBuffer;

void handleSaving(const char* text, uint32_t ts, StatsBuffer& history) {
    if (strlen(text) == 0) return;

    WordMetadata data;
    strncpy(data.word, text, 255);
    data.word[255] = '\0';
    data.timestamp = ts;
    data.avgInterval = (float)history.getAverageDelta() / 1000000.0f;
    data.variance = history.getVariance() / 1000000000000.0f;
    data.entropy = calculateEntropy(text);

    saveToFile(data);
}

void saveShortcut(const char* text, uint32_t ts) {
    WordMetadata data;
    strncpy(data.word, text, 255);
    data.word[255] = '\0';
    data.timestamp = ts;
    data.avgInterval = 0.0f;
    data.variance = 0.0f;
    data.entropy = 0.0f;
    saveToFile(data);
}

void InputHandlerFunc() {

    uint32_t lastKeyPressTS = 0;
    bool pendingSpace = false;

    while (true) {
        if (head != tail) {
            // Copie sécurisée du volatile
            KeyEvent event;
            event.key = queue[tail].key;
            event.modifiers = queue[tail].modifiers;
            event.timestamp = queue[tail].timestamp;

            if (KeyHelper::isSystemNoise(event.key)) {
                tail = (tail + 1) % Q_SIZE;
                continue;
            }

            if (lastKeyPressTS == 0) lastKeyPressTS = event.timestamp;
            uint32_t delta = event.timestamp - lastKeyPressTS;

            // --- TRAITEMENT DES TOUCHES ---
            if (KeyHelper::hasModifier(event.modifiers) && event.key != 0) {
                if (!currentLettersBuffer.isEmpty()) {
                    handleSaving(currentLettersBuffer.get_c_str(), event.timestamp, timeDeltaBetweenKeysBuffer);
                    currentLettersBuffer.clear();
                }
                std::string shortcut = AzertyLayout::getShortcutName(event.key, event.modifiers);
                saveShortcut(shortcut, event.timestamp);
            }
            else if (event.key >= 58 && event.key <= 69) { // F1-F12
                char fKey[8];
                sprintf(fKey, "[F%d]", (int)(event.key - 57));
                saveShortcut(fKey, event.timestamp);
            }
            else if (delta > 5000000 && !currentLettersBuffer.isEmpty() && !pendingSpace) {
                 handleSaving(currentLettersBuffer.get(), event.timestamp, timeDeltaBetweenKeysBuffer);
                 currentLettersBuffer.clear();
                 timeDeltaBetweenKeysBuffer.clear();
            }
            else if (KeyHelper::isLetterOrNumber(event.key)) {
                if (pendingSpace) {
                    handleSaving(currentLettersBuffer.get(), event.timestamp, timeDeltaBetweenKeysBuffer);
                    currentLettersBuffer.clear();
                    timeDeltaBetweenKeysBuffer.clear();
                    pendingSpace = false;
                }
                else if (!currentLettersBuffer.isEmpty()) {
                    timeDeltaBetweenKeysBuffer.add(delta);
                }
                currentLettersBuffer.add(event.key);
            }
            else if (KeyHelper::isBackspace(event.key)) {
                if (pendingSpace) pendingSpace = false;
                else {
                    currentLettersBuffer.backspace();
                    if (currentLettersBuffer.isEmpty()) timeDeltaBetweenKeysBuffer.clear();
                }
            }
            else if (KeyHelper::isSpace(event.key)) {
                if (!currentLettersBuffer.isEmpty()) pendingSpace = true;
            }
            else if (KeyHelper::isEnter(event.key)) {
                if (!currentLettersBuffer.isEmpty()) {
                    handleSaving(currentLettersBuffer.get(), event.timestamp, timeDeltaBetweenKeysBuffer);
                    currentLettersBuffer.clear();
                    timeDeltaBetweenKeysBuffer.clear();
                }
                pendingSpace = false;
                saveShortcut("[ENTER]", event.timestamp);
            }
            else if (KeyHelper::isPunctuation(event.key)) {
                if (pendingSpace || !currentLettersBuffer.isEmpty()) {
                     handleSaving(currentLettersBuffer.getWord(), event.timestamp, timeDeltaBetweenKeysBuffer);
                     currentLettersBuffer.clear();
                     timeDeltaBetweenKeysBuffer.clear();
                     pendingSpace = false;
                }
                // Optionnel : sauvegarder la ponctuation ici
            }

            // Mise à jour systématique pour éviter les blocages
            lastKeyPressTS = event.timestamp;
            tail = (tail + 1) % Q_SIZE;
        } else {
            threads.yield();
        }
    }
}

