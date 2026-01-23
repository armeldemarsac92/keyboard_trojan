#include "StatsBuffer.h"

StatsBuffer::StatsBuffer() {
    intervals.reserve(MAX_CAPACITY);
}

void StatsBuffer::add(uint32_t interval) {
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

const std::vector<uint32_t>& StatsBuffer::getIntervals() const {
    return intervals;
}

bool StatsBuffer::isEmpty() const {
    return intervals.empty();
}

// --- Math Logic (Now converts to Seconds) ---

float StatsBuffer::getAverage() const {
    if (intervals.empty()) return 0.0f;

    // 1. Calculate Sum in raw Milliseconds
    double sum = std::accumulate(intervals.begin(), intervals.end(), 0.0);

    // 2. Calculate Raw Average (ms)
    double rawAvg = sum / intervals.size();

    // 3. Convert to Seconds (1000 ms = 1.0 s)
    return (float)(rawAvg / 1000000.0);
}

float StatsBuffer::getVariance() const {
    if (intervals.size() < 2) return 0.0f;

    // NOTE: We calculate variance on the raw data (ms) first to preserve precision,
    // then convert the final result to seconds^2.

    double sum = std::accumulate(intervals.begin(), intervals.end(), 0.0);
    double rawMean = sum / intervals.size();

    double sumSqDiff = 0.0;

    for (uint32_t val : intervals) {
        double diff = val - rawMean;
        sumSqDiff += diff * diff;
    }

    double rawVariance = sumSqDiff / intervals.size();

    // Convert ms^2 to s^2
    // (1 ms)^2 = (0.001 s)^2 = 0.000001 s^2
    // So we divide by 1,000,000
    return (float)(rawVariance / 1000000000.0);
}