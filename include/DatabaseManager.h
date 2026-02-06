#pragma once

// ✅ CRITICAL: Arduino.h must be first to define yield(), millis(), etc.
#include <Arduino.h>

#include <vector>
#include <string>

// ✅ REMOVED: #include <mutex> (This caused the conflict!)

#include <TeensyThreads.h> // for Threads::Mutex
#include "ArduinoSQLiteHandler.h"

namespace KeyboardConfig {
    struct NodeInfo;
}

class DatabaseManager {
private:
    std::vector<std::string> pendingStatements;
    sqlite3* dbConnection;

    // The Mutex from TeensyThreads
    Threads::Mutex queueMutex;

    DatabaseManager();

    void getData(const std::function<void(sqlite3_stmt*)>& callback, const DBTable& table);


public:
    DatabaseManager(const DatabaseManager&) = delete;
    void operator=(const DatabaseManager&) = delete;

    static DatabaseManager& getInstance();
    void cleanupDuplicates();
    std::vector<KeyboardConfig::NodeInfo> getRadioNodes();
    void saveData(const std::vector<std::string>& data, const DBTable& table);
    void processQueue();
};
