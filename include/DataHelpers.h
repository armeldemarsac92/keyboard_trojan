#ifndef DATA_HELPERS_H
#define DATA_HELPERS_H

#include <cstdint>
#include <vector>
#include <string>
#include "LettersBuffer.h"
#include "StatsBuffer.h"

namespace DataHelpers {
    [[nodiscard]] inline std::vector<std::string> stringifyInputData(const LettersBuffer& word, const std::uint32_t ts, const StatsBuffer& history) {
        return {
            word.get(),
            std::to_string(ts),
            std::to_string(history.getVariance()),
            std::to_string(history.getAverage()),
            std::to_string(word.getEntropy())
        };
    }
}

#endif
