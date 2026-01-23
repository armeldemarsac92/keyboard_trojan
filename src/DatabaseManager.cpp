#include "DatabaseManager.h"
#include "ArduinoSQLiteHandler.h"
#include "../config/KeyboardConfig.h"

DatabaseManager::DatabaseManager() {
    setupDatabase();
    dbConnection = createOpenSQLConnection(KeyboardConfig::DBName.c_str());
}

DatabaseManager& DatabaseManager::getInstance() {
    static DatabaseManager instance;
    return instance;
}

void DatabaseManager::saveData(const std::vector<std::string>& data) {

    if (dbConnection == nullptr) {
        Serial.println("Error: Database not initialized!");
        return;
    }

    std::string sqlStatement = buildSQLInsertStatement(KeyboardConfig::Tables::Inputs, data);

    sqlBuffer.push_back(sqlStatement);

    if (sqlBuffer.size() >= 10) {
        Serial.println("Buffer full. Executing Transaction...");

        bool success = executeSQLTransaction(dbConnection, sqlBuffer);

        if (success) {
            Serial.println("Transaction Success! Clearing buffer.");
            sqlBuffer.clear();
        } else {
            Serial.println("Transaction Failed! Retrying later.");

            if (sqlBuffer.size() > 50) {
                Serial.println("CRITICAL: SQL Buffer overflow. Purging data to prevent crash.");
                sqlBuffer.clear();
            }
        }
    }
}