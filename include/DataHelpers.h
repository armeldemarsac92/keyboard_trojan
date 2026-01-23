#ifndef DATA_HELPERS_H
#define DATA_HELPERS_H

#include <vector>
#include <string>
#include "LettersBuffer.h"
#include "StatsBuffer.h"

namespace DataHelpers {
    inline std::vector<std::string> stringifyInputData(const LettersBuffer& word, const uint32_t ts, const StatsBuffer& history) {
        std::vector<std::string> data;

        data.push_back(word.get());

        data.push_back(std::to_string(ts));
        data.push_back(std::to_string(history.getAverage()));
        data.push_back(std::to_string(history.getVariance()));
        data.push_back(std::to_string(word.getEntropy()));

        return data;
    }
}

#endif