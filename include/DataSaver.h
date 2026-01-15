#ifndef DATASAVER_H
#define DATASAVER_H

#include <cstdint>

struct WordMetadata {
    char word[256];
    uint32_t timestamp;
    float avgInterval;     // Vitesse (sec)
    float variance;        // Stabilité
    float entropy;         // Complexité
};

void saveToFile(const WordMetadata& data);

#endif