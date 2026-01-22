#include "DataSaver.h"
#include <Arduino.h>
#include "MemoryInfo.hpp"
#include "ArduinoSQLite.hpp"

void saveToFile(const WordMetadata& data) {
    // Format enrichi pour le debugging et l'IA
    // SAVING: [TS:123456] mot (Avg: 0.1234s, Var: 0.0000, Ent: 2.50)

    Serial.print("SAVING: [TS:");
    Serial.print(data.timestamp);
    Serial.print("] ");
    Serial.print(data.word);

    // On affiche les stats uniquement si le mot n'est pas un raccourci vide d'intérêt
    if (data.avgInterval > 0.0001f || data.entropy > 0.0001f) {
        Serial.print(" (Avg: ");
        Serial.print(data.avgInterval, 4);
        Serial.print("s, Var: ");
        Serial.print(data.variance, 6); // La variance est souvent très petite
        Serial.print(", Ent: ");
        Serial.print(data.entropy, 2);
        Serial.print(")");
    }

    Serial.println();
}