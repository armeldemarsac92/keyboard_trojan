#ifndef KEYBOARD_KEYBOARDCONFIG_H
#define KEYBOARD_KEYBOARDCONFIG_H

#include <cstddef>
#include <cstdint>
#include <vector>
#include <string>
#include <string_view>
#include "dbTypes.h"

namespace KeyboardConfig {
    inline constexpr std::uint32_t SerialBaudRate = 115200U;
    inline constexpr int MaxRetries = 5;

    namespace Security {
        inline constexpr std::string_view MasterEnrollmentCommand{"PAIR"};
        inline constexpr std::string_view MasterEnrollmentSecret{"CHANGE_ME_TO_A_LONG_RANDOM_SECRET"};
        constexpr std::size_t MinMasterEnrollmentSecretLength = 24;
    }

    inline constexpr std::string_view DBName{"logger.db"};

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

        inline const DBTable RadioMasters = {
            "RadioMasters",
            {
                {"MasterID", "INTEGER PRIMAREY KEY", true},
                {"MasterMeshID", "INTEGER UNIQUE"}
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

    struct NodeInfo {
        std::uint32_t id;
        std::uint64_t address;
    };
}

#endif //KEYBOARD_KEYBOARDCONFIG_H
