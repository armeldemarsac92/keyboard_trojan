#include "MathUtils.h"
#include <cmath>

namespace MathUtils {

    float calculateAverage(const uint32_t* data, int count) {
        if (count == 0) return 0.0f;
        
        // Use double for accumulation to prevent overflow/precision loss
        double total = 0; 
        for (int i = 0; i < count; i++) {
            total += data[i];
        }
        return (float)(total / count);
    }

    float calculateVariance(const uint32_t* data, int count) {
        if (count < 2) return 0.0f;

        float avg = calculateAverage(data, count);
        double sumSqDiff = 0; 
        
        for (int i = 0; i < count; i++) {
            float diff = (float)data[i] - avg;
            sumSqDiff += diff * diff;
        }
        return (float)(sumSqDiff / count);
    }

    float calculateEntropy(const char* s) {
        int counts[256] = {0};
        size_t len = 0;

        for (const char* p = s; *p; ++p) {
            counts[(unsigned char)*p]++;
            len++;
        }

        if (len == 0) return 0.0f;

        float entropy = 0.0f;
        float invLen = 1.0f / (float)len;

        for (int i = 0; i < 256; i++) {
            if (counts[i] > 0) {
                float p = counts[i] * invLen;
                entropy -= p * log2f(p);
            }
        }
        return entropy;
    }
}