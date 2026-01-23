// #include "DataHelpers.h"
// #include <Arduino.h>
// #include <string>
// #include "ArduinoSQLiteHandler.h"
// #include "../config/KeyboardConfig.h"
//
// sqlite3* databaseConnection = nullptr;
// std::vector<std::string> sqlBuffer;
//
// void saveToFile(const WordMetadata& data) {
//     if (databaseConnection == nullptr) {
//         Serial.println("Error: Database not initialized!");
//         return;
//     }
//
//     std::vector<std::string> dataToInsert = stringifyWordMetadata(data);
//     std::string sqlStatement = buildSQLInsertStatement(KeyboardConfig::Tables::Inputs, dataToInsert);
//
//     sqlBuffer.push_back(sqlStatement);
//
//     if (sqlBuffer.size() >= 10) {
//         Serial.println("Buffer full. Executing Transaction...");
//
//         bool success = executeSQLTransaction(databaseConnection, sqlBuffer);
//
//         if (success) {
//             sqlBuffer.clear();
//         } else {
//             Serial.println("Transaction Failed! Retrying later.");
//
//             if (sqlBuffer.size() > 50) {
//                 Serial.println("CRITICAL: SQL Buffer overflow. Purging data to prevent crash.");
//                 sqlBuffer.clear();
//             }
//         }
//     }
//
//     Serial.print("SAVING: [TS:");
//
//     Serial.print(data.timestamp);
//
//     Serial.print("] ");
//
//     Serial.print(data.word);
//
//
//     // On affiche les stats uniquement si le mot n'est pas un raccourci vide d'intérêt
//
//     if (data.avgInterval > 0.0001f || data.entropy > 0.0001f) {
//
//         Serial.print(" (Avg: ");
//
//         Serial.print(data.avgInterval, 4);
//
//         Serial.print("s, Var: ");
//
//         Serial.print(data.variance, 6); // La variance est souvent très petite
//
//         Serial.print(", Ent: ");
//
//         Serial.print(data.entropy, 2);
//
//         Serial.print(")");
//
//     }
//
//
//     Serial.println();
//
// }