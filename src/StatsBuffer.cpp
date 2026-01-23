#include "StatsBuffer.h"

StatsBuffer::StatsBuffer() {
    // Optimization: Reserve memory once so it acts like a fixed array
    intervals.reserve(MAX_CAPACITY);
}

void StatsBuffer::add(uint32_t interval) {
    if (intervals.size() < MAX_CAPACITY) {
        intervals.push_back(interval);
    }
}

void StatsBuffer::backspace() {
    // If user backspaces a letter, we remove the corresponding timing data
    if (!intervals.empty()) {
        intervals.pop_back();
    }
}

void StatsBuffer::clear() {
    intervals.clear();
}

const std::vector<uint32_t>& StatsBuffer::getIntervals() const {
    return intervals;
}

bool StatsBuffer::isEmpty() const {
    return intervals.empty();
}

// --- Math Logic (Calculated on the fly for the current word) ---

float StatsBuffer::getAverage() const {
    if (intervals.empty()) return 0.0f;

    // Modern C++: accumulate sums up the vector
    // 0.0 starts the sum as a double to prevent overflow
    double sum = std::accumulate(intervals.begin(), intervals.end(), 0.0);
    return (float)(sum / intervals.size());
}

float StatsBuffer::getVariance() const {
    if (intervals.size() < 2) return 0.0f;

    float mean = getAverage();
    double sumSqDiff = 0.0;

    for (uint32_t val : intervals) {
        float diff = val - mean;
        sumSqDiff += diff * diff;
    }

    return (float)(sumSqDiff / intervals.size());
}