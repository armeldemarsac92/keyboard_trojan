#pragma once
#include <cstdint>

struct InputData {
    char word[256];
    std::uint32_t timestamp;
    float avgInterval;
    float variance;
    float entropy;
};
