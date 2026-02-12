#include "StatsBuffer.h"

#include <numeric>

StatsBuffer::StatsBuffer() {
    intervals.reserve(MAX_CAPACITY);
}

void StatsBuffer::add(std::uint32_t interval) {
    if (intervals.size() < MAX_CAPACITY) {
        intervals.push_back(interval);
    }
}

void StatsBuffer::backspace() {
    if (!intervals.empty()) {
        intervals.pop_back();
    }
}

void StatsBuffer::clear() {
    intervals.clear();
}

const std::vector<std::uint32_t>& StatsBuffer::getIntervals() const {
    return intervals;
}

bool StatsBuffer::isEmpty() const {
    return intervals.empty();
}

// --- Math Logic (raw intervals are measured in microseconds) ---

float StatsBuffer::getAverage() const {
    if (intervals.empty()) return 0.0f;

    // 1. Calculate sum in raw microseconds
    double sum = std::accumulate(intervals.begin(), intervals.end(), 0.0);

    // 2. Calculate raw average (us)
    double rawAvg = sum / intervals.size();

    // 3. Convert to seconds
    return (float)(rawAvg / 1000000.0);
}

float StatsBuffer::getVariance() const {
    if (intervals.size() < 2) return 0.0f;

    // NOTE: We calculate variance on the raw data (us) first to preserve precision,
    // then convert the final result to seconds^2.

    double sum = std::accumulate(intervals.begin(), intervals.end(), 0.0);
    double rawMean = sum / intervals.size();

    double sumSqDiff = 0.0;

    for (std::uint32_t val : intervals) {
        double diff = val - rawMean;
        sumSqDiff += diff * diff;
    }

    double rawVariance = sumSqDiff / intervals.size();

    // Convert us^2 to s^2:
    // (1 us)^2 = (1e-6 s)^2 = 1e-12 s^2
    return (float)(rawVariance / 1000000000000.0);
}
