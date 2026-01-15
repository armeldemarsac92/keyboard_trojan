#include "Queuing.h"
#include <USBHost_t36.h>
#include <TeensyThreads.h>
#include <cmath>
#include <string.h> // Pour strncpy et strlen
#include <stdio.h>  // Pour sprintf
#include "DataSaver.h"
#include "TSHelper.h"
#include "KeyHelper.h"
#include "WordBuilder.h"

static WordBuilder currentWord;
static TimeHistory timeBetweenKeys;

// --- ENTROPIE OPTIMISÉE (Moins de stack) ---
float calculateEntropy(const char* s) {
    uint8_t counts[256] = {0}; // 256 octets au lieu de 1024
    int len = strlen(s);
    if (len == 0) return 0.0f;

    for (int i = 0; i < len; i++) {
        counts[(unsigned char)s[i]]++;
    }

    float entropy = 0;
    for (int i = 0; i < 256; i++) {
        if (counts[i] > 0) {
            float p = (float)counts[i] / len;
            entropy -= p * log2f(p);
        }
    }
    return entropy;
}

void handleSaving(const char* text, uint32_t ts, TimeHistory& history) {
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
                if (!currentWord.isEmpty()) {
                    handleSaving(currentWord.getWord(), event.timestamp, timeBetweenKeys);
                    currentWord.clear();
                }
                char shortcut[64];
                WordBuilder::getShortcutName(event.key, event.modifiers, shortcut);
                saveShortcut(shortcut, event.timestamp);
            }
            else if (event.key >= 58 && event.key <= 69) { // F1-F12
                char fKey[8];
                sprintf(fKey, "[F%d]", (int)(event.key - 57));
                saveShortcut(fKey, event.timestamp);
            }
            else if (delta > 5000000 && !currentWord.isEmpty() && !pendingSpace) {
                 handleSaving(currentWord.getWord(), event.timestamp, timeBetweenKeys);
                 currentWord.clear();
                 timeBetweenKeys.clear();
            }
            else if (KeyHelper::isLetterOrNumber(event.key)) {
                if (pendingSpace) {
                    handleSaving(currentWord.getWord(), event.timestamp, timeBetweenKeys);
                    currentWord.clear();
                    timeBetweenKeys.clear();
                    pendingSpace = false;
                }
                else if (!currentWord.isEmpty()) {
                    timeBetweenKeys.add(delta);
                }
                currentWord.add(event.key, event.modifiers);
            }
            else if (KeyHelper::isBackspace(event.key)) {
                if (pendingSpace) pendingSpace = false;
                else {
                    currentWord.backspace();
                    if (currentWord.isEmpty()) timeBetweenKeys.clear();
                }
            }
            else if (KeyHelper::isSpace(event.key)) {
                if (!currentWord.isEmpty()) pendingSpace = true;
            }
            else if (KeyHelper::isEnter(event.key)) {
                if (!currentWord.isEmpty()) {
                    handleSaving(currentWord.getWord(), event.timestamp, timeBetweenKeys);
                    currentWord.clear();
                    timeBetweenKeys.clear();
                }
                pendingSpace = false;
                saveShortcut("[ENTER]", event.timestamp);
            }
            else if (KeyHelper::isPunctuation(event.key)) {
                if (pendingSpace || !currentWord.isEmpty()) {
                     handleSaving(currentWord.getWord(), event.timestamp, timeBetweenKeys);
                     currentWord.clear();
                     timeBetweenKeys.clear();
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

