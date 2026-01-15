#ifndef TSHELPER_H
#define TSHELPER_H

#include <cmath> // Pour le calcul de la variance

class TimeHistory {
private:
    static const int SIZE = 100;
    uint32_t buffer[SIZE];
    int head = 0;
    bool isFull = false;

public:
    void add(uint32_t ts) {
        buffer[head] = ts;
        head = (head + 1);
        if (head >= SIZE) { head = 0; isFull = true; }
    }

    uint32_t getAverageDelta() {
        int count = isFull ? SIZE : head;
        if (count == 0) return 0;
        uint32_t total = 0;
        for (int i = 0; i < count; i++) total += buffer[i];
        return total / count;
    }

    // --- NOUVEAU : Calcul de la Variance ---
    float getVariance() {
        int count = isFull ? SIZE : head;
        if (count < 2) return 0.0f;

        float avg = (float)getAverageDelta();
        float sumSqDiff = 0;
        for (int i = 0; i < count; i++) {
            float diff = (float)buffer[i] - avg;
            sumSqDiff += diff * diff;
        }
        return sumSqDiff / count;
    }

    void clear() { head = 0; isFull = false; }
};

#endif