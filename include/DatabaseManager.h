#pragma once

// ✅ CRITICAL: Arduino.h must be first to define yield(), millis(), etc.
#include <Arduino.h>

#include <vector>
#include <string>

// ✅ REMOVED: #include <mutex> (This caused the conflict!)

#include <TeensyThreads.h> // for Threads::Mutex
#include "ArduinoSQLiteHandler.h"

class DatabaseManager {
private:
    std::vector<std::string> pendingStatements;
    sqlite3* dbConnection;

    // The Mutex from TeensyThreads
    Threads::Mutex queueMutex;

    DatabaseManager();

public:
    DatabaseManager(const DatabaseManager&) = delete;
    void operator=(const DatabaseManager&) = delete;

    static DatabaseManager& getInstance();

    void saveData(const std::vector<std::string>& data);
    void processQueue();
};