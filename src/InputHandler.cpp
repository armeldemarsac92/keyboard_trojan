#include <Arduino.h>
#include <USBHost_t36.h>
#include <TeensyThreads.h>
#include <cmath>
#include <string.h>
#include <stdio.h>

#include "AzertyLayout.h"
#include "DatabaseManager.h"
#include "Queuing.h"
#include "DataHelpers.h"
#include "InputData.h"
#include "KeyHelper.h"
#include "LettersBuffer.h"
#include "StatsBuffer.h"
#include "../config/KeyboardConfig.h"

static LettersBuffer currentLettersBuffer;
static StatsBuffer timeDeltaBetweenKeysBuffer;

void InputHandlerFunc() {

    uint32_t lastKeyPressTS = 0;
    bool pendingSpace = false;

    while (true) {
        if (head != tail) {
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


            if (KeyHelper::hasModifier(event.modifiers) && event.key != 0) {
                if (!currentLettersBuffer.isEmpty()) {
                    DatabaseManager::getInstance().saveData(
                        DataHelpers::stringifyInputData(currentLettersBuffer, lastKeyPressTS, timeDeltaBetweenKeysBuffer),
                        KeyboardConfig::Tables::Inputs
                    );
                    currentLettersBuffer.clear();
                    timeDeltaBetweenKeysBuffer.clear();
                }

                currentLettersBuffer.addShortcut(event.key, event.modifiers);

                DatabaseManager::getInstance().saveData(
                    DataHelpers::stringifyInputData(currentLettersBuffer, event.timestamp, timeDeltaBetweenKeysBuffer),
                    KeyboardConfig::Tables::Inputs
                );
                currentLettersBuffer.clear();
            }

            else if (delta > 5000000 && !currentLettersBuffer.isEmpty() && !pendingSpace) {
                 DatabaseManager::getInstance().saveData(
                    DataHelpers::stringifyInputData(currentLettersBuffer, lastKeyPressTS, timeDeltaBetweenKeysBuffer),
                    KeyboardConfig::Tables::Inputs
                 );
                 currentLettersBuffer.clear();
                 timeDeltaBetweenKeysBuffer.clear();
            }

            else if (KeyHelper::isLetterOrNumber(event.key)) {
                if (pendingSpace) {
                    DatabaseManager::getInstance().saveData(
                        DataHelpers::stringifyInputData(currentLettersBuffer, lastKeyPressTS, timeDeltaBetweenKeysBuffer),
                        KeyboardConfig::Tables::Inputs
                    );
                    currentLettersBuffer.clear();
                    timeDeltaBetweenKeysBuffer.clear();
                    pendingSpace = false;
                }
                else if (!currentLettersBuffer.isEmpty()) {
                    timeDeltaBetweenKeysBuffer.add(delta);
                }

                currentLettersBuffer.addChar(event.key, event.modifiers);
            }

            else if (KeyHelper::isBackspace(event.key)) {
                if (pendingSpace) pendingSpace = false;
                else {
                    currentLettersBuffer.backspace();
                    timeDeltaBetweenKeysBuffer.backspace();
                    if (currentLettersBuffer.isEmpty()) timeDeltaBetweenKeysBuffer.clear();
                }
            }

            else if (KeyHelper::isSpace(event.key)) {
                if (!currentLettersBuffer.isEmpty()) pendingSpace = true;
            }

            else if (KeyHelper::isEnter(event.key)) {
                if (!currentLettersBuffer.isEmpty()) {
                    DatabaseManager::getInstance().saveData(
                        DataHelpers::stringifyInputData(currentLettersBuffer, lastKeyPressTS, timeDeltaBetweenKeysBuffer),
                        KeyboardConfig::Tables::Inputs
                    );
                    currentLettersBuffer.clear();
                    timeDeltaBetweenKeysBuffer.clear();
                }
                pendingSpace = false;

            }

            lastKeyPressTS = event.timestamp;
            tail = (tail + 1) % Q_SIZE;
        } else {
            threads.yield();
        }
    }
}