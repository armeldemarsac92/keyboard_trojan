#include "TextAnalyzer.h"
#include <cmath>
#include <cstring>

namespace InputAnalyzer {
    
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
