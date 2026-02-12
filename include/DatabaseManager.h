#pragma once

#include <functional>
#include <string>
#include <vector>
#include <TeensyThreads.h>
#include "ArduinoSQLiteHandler.h"

namespace KeyboardConfig {
    struct NodeInfo;
}

class DatabaseManager {
private:
    std::vector<std::string> pendingStatements;
    sqlite3* dbConnection = nullptr;
    Threads::Mutex queueMutex;

    DatabaseManager();

    void getData(const std::function<void(sqlite3_stmt*)>& callback, const DBTable& table);


public:
    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;

    static DatabaseManager& getInstance();
    void cleanupDuplicates();
    [[nodiscard]] std::vector<KeyboardConfig::NodeInfo> getRadioNodes();
    void saveData(const std::vector<std::string>& data, const DBTable& table);
    void processQueue();
};
