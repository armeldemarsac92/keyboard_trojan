#ifndef KEYBOARD_KEYBOARDCONFIG_H
#define KEYBOARD_KEYBOARDCONFIG_H

#include <string>
#include <vector>
#include "dbTypes.h"

namespace KeyboardConfig {
    constexpr int SerialBaudRate = 115200;
    constexpr int MaxRetries = 5;

    inline const std::string DBName = "logger.db";

    namespace Tables {
        inline const DBTable Inputs = {
            "Inputs",
            {
                {"InputID", "INTEGER PRIMARY KEY", true},
                {"Input", "TEXT"},
                {"Timestamp", "TEXT"},
                {"Variance", "REAL"},
                {"AvgDelayBetweenStrokes", "REAL"},
                {"Entropy", "REAL"}
            }
        };

        inline const DBTable Logs = {
            "Logs",
            {
                {"LogID", "INTEGER PRIMARY KEY", true},
                {"Message", "TEXT"},
                {"Severity", "INTEGER"}
            }
        };
    }
}

#endif //KEYBOARD_KEYBOARDCONFIG_H