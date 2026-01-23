#pragma once
#include <vector>
#include <cstdint>
#include <numeric> // For math functions
#include <cmath>   // For std::sqrt

class StatsBuffer {
private:
    static const size_t MAX_CAPACITY = 100;
    std::vector<uint32_t> intervals;

public:
    StatsBuffer();

    void add(uint32_t interval);
    void backspace();
    void clear();

    const std::vector<uint32_t>& getIntervals() const;
    bool isEmpty() const;

    float getAverage() const;
    float getVariance() const;
};