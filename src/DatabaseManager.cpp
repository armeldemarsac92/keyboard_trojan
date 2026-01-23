#include "DatabaseManager.h"
#include "ArduinoSQLiteHandler.h"
#include "usb_serial.h"
#include "../config/KeyboardConfig.h"

DatabaseManager::DatabaseManager() {
    setupDatabase();
    dbConnection = createOpenSQLConnection(KeyboardConfig::DBName.c_str());
    createSQLTable(dbConnection, KeyboardConfig::Tables::Inputs);
}

DatabaseManager& DatabaseManager::getInstance() {
    static DatabaseManager instance;
    return instance;
}

void DatabaseManager::saveData(const std::vector<std::string>& data) {
    if (data.empty()) return;

    std::string sqlStatement = buildSQLInsertStatement(KeyboardConfig::Tables::Inputs, data);

    queueMutex.lock();

    pendingStatements.push_back(sqlStatement);

    queueMutex.unlock();

    Serial.print("QUEUED: ");
    Serial.println(data[0].c_str());
}

void DatabaseManager::processQueue() {
    static uint32_t lastWriteTime = 0;
    std::vector<std::string> batchToSave;

    queueMutex.lock();

    bool shouldWrite = (pendingStatements.size() >= 20) ||
                       (!pendingStatements.empty() && (millis() - lastWriteTime > 5000));

    if (!shouldWrite) {
        queueMutex.unlock();
        return;
    }

    batchToSave = std::move(pendingStatements);
    pendingStatements.clear();
    queueMutex.unlock();

    if (!batchToSave.empty()) {
        executeSQLTransaction(dbConnection, batchToSave);
        lastWriteTime = millis(); // Reset timer
    }
}