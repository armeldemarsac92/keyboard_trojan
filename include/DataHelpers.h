#ifndef DATA_HELPERS_H
#define DATA_HELPERS_H

#include <cstdint>
#include <vector>
#include <string>
#include <string_view>
#include "LettersBuffer.h"
#include "StatsBuffer.h"

namespace DataHelpers {
    [[nodiscard]] inline std::vector<std::string> stringifyInputData(const LettersBuffer& word, const std::uint32_t ts_us, const StatsBuffer& history) {
        const double ts_seconds = static_cast<double>(ts_us) / 1'000'000.0;
        return {
            word.get(),
            std::to_string(ts_seconds),
            std::to_string(history.getVariance()),
            std::to_string(history.getAverage()),
            std::to_string(word.getEntropy())
        };
    }
}

#endif
