#include "DatabaseManager.h"
#include "ArduinoSQLiteHandler.h"
#include "usb_serial.h"
#include "../config/KeyboardConfig.h"

DatabaseManager::DatabaseManager() {
    setupDatabase();
    dbConnection = createOpenSQLConnection(KeyboardConfig::DBName.c_str());
    createSQLTable(dbConnection, KeyboardConfig::Tables::Inputs);
    createSQLTable(dbConnection, KeyboardConfig::Tables::RadioMasters);
}

DatabaseManager& DatabaseManager::getInstance() {
    static DatabaseManager instance;
    return instance;
}

std::vector<KeyboardConfig::NodeInfo> DatabaseManager::getRadioNodes() {
    std::vector<KeyboardConfig::NodeInfo> results;

    this->getData([&results](sqlite3_stmt* row) {
        uint32_t id = static_cast<uint32_t>(sqlite3_column_int(row, 0));

        uint64_t addr = static_cast<uint64_t>(sqlite3_column_int64(row, 1));

        results.push_back({id, addr});

    }, KeyboardConfig::Tables::RadioMasters);

    return results;
}

void DatabaseManager::getData(const std::function<void(sqlite3_stmt*)>& callback, const DBTable& table) {
    if (!dbConnection) return;

    queueMutex.lock();

    std::string tableName = table.tableName;
    std::string query = "SELECT * FROM " + tableName + ";";

    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(dbConnection, query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            callback(stmt);
        }
    } else {
        Serial.printf("Database Read Error: %s\n", sqlite3_errmsg(dbConnection));
    }

    sqlite3_finalize(stmt);

    queueMutex.unlock();
}

void DatabaseManager::saveData(const std::vector<std::string>& data, const DBTable& table) {
    if (data.empty()) return;

    std::string sqlStatement = buildSQLInsertStatement(table, data);

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
                       (!pendingStatements.empty() && (millis() - lastWriteTime > 60000));

    if (!shouldWrite) {
        queueMutex.unlock();
        return;
    }

    batchToSave = std::move(pendingStatements);
    pendingStatements.clear();
    queueMutex.unlock();

    if (!batchToSave.empty()) {
        executeSQLTransaction(dbConnection, batchToSave);
        lastWriteTime = millis();
    }
}