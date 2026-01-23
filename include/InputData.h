#pragma once
#include <stdint.h>

struct InputData {
    char word[256];
    uint32_t timestamp;
    float avgInterval;
    float variance;
    float entropy;
};
