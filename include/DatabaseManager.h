#pragma once
#include <Arduino.h>
#include <vector>
#include <string>
#include "dbTypes.h"

struct sqlite3;

class DatabaseManager {
private:
    sqlite3* dbConnection = nullptr;
    std::vector<std::string> sqlBuffer;

    DatabaseManager();

public:
    DatabaseManager(const DatabaseManager&) = delete;
    void operator=(const DatabaseManager&) = delete;

    static DatabaseManager& getInstance();

    sqlite3* getDB() { return dbConnection; }

    void saveData(const std::vector<std::string>& data);
};