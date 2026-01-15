#include "DataSaver.h"
#include <Arduino.h>

void saveToFile(const WordMetadata& data) {
    // Format : [WINDOW] word (Avg: 0.000s, Var: 0.000, Ent: 0.00)
    Serial.print("SAVING: [");
    Serial.print(data.windowName);
    Serial.print("] ");
    Serial.print(data.word);

    if (data.avgInterval > 0.0001f || data.entropy > 0.0001f) {
        Serial.print(" (Avg: ");
        Serial.print(data.avgInterval, 4);
        Serial.print("s, Var: ");
        Serial.print(data.variance, 6);
        Serial.print(", Ent: ");
        Serial.print(data.entropy, 2);
        Serial.print(")");
    }
    Serial.println();
}