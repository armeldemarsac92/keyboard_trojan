#include <Arduino.h>
#include <TeensyThreads.h>
#include <cstdint>
#include <string>

#include "DataHelpers.h"
#include "DatabaseManager.h"
#include "KeyHelper.h"
#include "LettersBuffer.h"
#include "Logger.h"
#include "HidBridge.h"
#include "Queuing.h"
#include "StatsBuffer.h"
#include "../config/KeyboardConfig.h"

static LettersBuffer currentLettersBuffer;
static StatsBuffer timeDeltaBetweenKeysBuffer;
namespace {
constexpr std::uint32_t kWordSplitTimeoutUs = 5'000'000U;
}

void InputHandlerFunc() {
    std::uint32_t lastKeyPressTS = 0;
    bool pendingSpace = false;
    std::uint32_t lastQueueOverwriteCount = 0;
    std::uint32_t lastQueueOverwriteLogMs = 0;

    auto flushCurrentBufferToDb = [&](const char* reason, const std::uint32_t ts_us) {
        if (currentLettersBuffer.isEmpty()) {
            return;
        }

        constexpr std::size_t kMaxLoggedChars = 64;
        const auto& word = currentLettersBuffer.get();
        const std::size_t shown = std::min<std::size_t>(word.size(), kMaxLoggedChars);
        const bool truncated = word.size() > shown;

        const double ts_s = static_cast<double>(ts_us) / 1'000'000.0;
        Logger::instance().printf("[INPUT] flush=%s ts=%.3f len=%u word=\"%.*s%s\"\n",
                                  reason,
                                  ts_s,
                                  static_cast<unsigned>(word.size()),
                                  static_cast<int>(shown),
                                  word.c_str(),
                                  truncated ? "..." : "");

        const std::string activeWindow = HidBridge::instance().activeWindowSnapshot();
        DatabaseManager::getInstance().saveData(
            DataHelpers::stringifyInputData(currentLettersBuffer, ts_us, timeDeltaBetweenKeysBuffer, activeWindow),
            KeyboardConfig::Tables::Inputs
        );
        currentLettersBuffer.clear();
        timeDeltaBetweenKeysBuffer.clear();
    };

    while (true) {
        const std::uint32_t queueOverwrites = queueOverwriteCount;
        if (queueOverwrites != lastQueueOverwriteCount) {
            const std::uint32_t nowMs = millis();
            const std::uint32_t delta = queueOverwrites - lastQueueOverwriteCount;
            if ((nowMs - lastQueueOverwriteLogMs) >= 1000U || delta >= 32U) {
                Logger::instance().printf("[INPUT] queue overwrites=%u (+%u)\n",
                                          static_cast<unsigned>(queueOverwrites),
                                          static_cast<unsigned>(delta));
                lastQueueOverwriteCount = queueOverwrites;
                lastQueueOverwriteLogMs = nowMs;
            }
        }

        if (head != tail) {
            const KeyEvent event{
                queue[tail].key,
                queue[tail].modifiers,
                queue[tail].timestamp
            };

            if (KeyHelper::isSystemNoise(event.key)) {
                tail = (tail + 1) % Q_SIZE;
                continue;
            }

            if (lastKeyPressTS == 0) lastKeyPressTS = event.timestamp;
            const std::uint32_t delta = event.timestamp - lastKeyPressTS;


            if (KeyHelper::hasModifier(event.modifiers) && event.key != 0) {
                flushCurrentBufferToDb("modifier", lastKeyPressTS);

                currentLettersBuffer.addShortcut(event.key, event.modifiers);

                flushCurrentBufferToDb("shortcut", event.timestamp);
            }

            else if (delta > kWordSplitTimeoutUs && !currentLettersBuffer.isEmpty() && !pendingSpace) {
                flushCurrentBufferToDb("timeout", lastKeyPressTS);
            }

            else if (KeyHelper::isLetterOrNumber(event.key)) {
                if (pendingSpace) {
                    flushCurrentBufferToDb("space", lastKeyPressTS);
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
                flushCurrentBufferToDb("enter", lastKeyPressTS);
                pendingSpace = false;

            }

            lastKeyPressTS = event.timestamp;
            tail = (tail + 1) % Q_SIZE;
        } else {
            threads.yield();
        }
    }
}
