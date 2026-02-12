#include "DatabaseManager.h"

#include <Arduino.h>
#include <cstdint>

#include "ArduinoSQLiteHandler.h"
#include "Logger.h"
#include "usb_serial.h"
#include "../config/KeyboardConfig.h"

DatabaseManager::DatabaseManager() {
    setupDatabase();
    dbConnection = createOpenSQLConnection(KeyboardConfig::DBName.data());
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
        const std::uint32_t id = static_cast<std::uint32_t>(sqlite3_column_int(row, 0));
        const std::uint64_t addr = static_cast<std::uint64_t>(sqlite3_column_int64(row, 1));
        results.push_back({id, addr});
    }, KeyboardConfig::Tables::RadioMasters);

    return results;
}

void DatabaseManager::getData(const std::function<void(sqlite3_stmt*)>& callback, const DBTable& table) {
    if (!dbConnection) return;

    Threads::Scope scope(queueMutex);

    const std::string query = "SELECT * FROM " + table.tableName + ";";
    sqlite3_stmt* stmt = nullptr;

    const int rc = sqlite3_prepare_v2(dbConnection, query.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        Logger::instance().printf("Database Read Error: %s\n", sqlite3_errmsg(dbConnection));
        return;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        callback(stmt);
    }

    sqlite3_finalize(stmt);
}

void DatabaseManager::saveData(const std::vector<std::string>& data, const DBTable& table) {
    if (data.empty()) return;

    std::string sqlStatement = buildSQLInsertStatement(table, data);
    if (sqlStatement.empty()) return;

    Threads::Scope scope(queueMutex);
    pendingStatements.push_back(sqlStatement);

    Logger::instance().print("QUEUED: ");
    Logger::instance().println(data[0].c_str());
}

void DatabaseManager::processQueue() {
    static std::uint32_t lastWriteTime = 0;
    constexpr std::size_t kBatchSize = 20;
    constexpr std::uint32_t kMaxFlushDelayMs = 60'000;

    std::vector<std::string> batchToSave;

    {
        Threads::Scope scope(queueMutex);
        const bool shouldWrite = (pendingStatements.size() >= kBatchSize) ||
                                 (!pendingStatements.empty() && (millis() - lastWriteTime > kMaxFlushDelayMs));
        if (!shouldWrite) {
            return;
        }

        batchToSave = std::move(pendingStatements);
        pendingStatements.clear();
    }

    if (!batchToSave.empty()) {
        executeSQLTransaction(dbConnection, batchToSave);
        lastWriteTime = millis();
    }
}
