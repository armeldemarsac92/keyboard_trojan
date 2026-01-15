#ifndef DATASAVER_H
#define DATASAVER_H

#include <cstdint>

struct WordMetadata {
    char word[256];
    char windowName[64];  // Le contexte envoyé par l'implant
    uint32_t timestamp;
    float avgInterval;     // Vitesse (sec)
    float variance;        // Stabilité
    float entropy;         // Complexité
};

void saveToFile(const WordMetadata& data);

#endif