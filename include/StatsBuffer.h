#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

class StatsBuffer {
private:
    static constexpr std::size_t MAX_CAPACITY = 100;
    std::vector<std::uint32_t> intervals;

public:
    StatsBuffer();

    void add(std::uint32_t interval);
    void backspace();
    void clear();

    const std::vector<std::uint32_t>& getIntervals() const;
    bool isEmpty() const;

    float getAverage() const;
    float getVariance() const;
};
