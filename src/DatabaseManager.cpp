#include "DatabaseManager.h"

#include <Arduino.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ArduinoSQLiteHandler.h"
#include "Logger.h"
#include "../config/KeyboardConfig.h"

namespace {
bool isTextColumnType(const std::string& columnType) {
    return columnType.find("TEXT") != std::string::npos;
}

// Escape for SQL string literals: ' => ''.
// This is the minimal fix to avoid malformed INSERT statements when user input contains apostrophes.
std::string escapeSqlStringLiteral(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size());

    for (const char c : value) {
        if (c == '\'') {
            escaped.push_back('\'');
            escaped.push_back('\'');
        } else if (c == '\0') {
            // sqlite3_exec() uses C-strings and would truncate at NUL anyway.
            // Drop it to avoid embedding invisible truncation points.
            continue;
        } else {
            escaped.push_back(c);
        }
    }

    return escaped;
}

void escapeInsertDataInPlace(const DBTable& table, std::vector<std::string>& data) {
    std::size_t dataIndex = 0;

    for (const auto& col : table.columns) {
        if (col.isPrimaryKey) {
            continue;
        }

        if (dataIndex >= data.size()) {
            return;
        }

        if (isTextColumnType(col.type)) {
            data[dataIndex] = escapeSqlStringLiteral(data[dataIndex]);
        } else {
            // Avoid invalid SQL like: VALUES (, ...)
            if (data[dataIndex].empty()) {
                data[dataIndex] = "NULL";
            }
        }

        ++dataIndex;
    }
}

bool executeSqlTransaction(sqlite3* db, const std::vector<std::string>& statements) {
    if (db == nullptr) {
        return false;
    }

    if (statements.empty()) {
        return true;
    }

    char* errMsg = nullptr;
    int rc = sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        Logger::instance().printf("[DB] BEGIN failed: %s\n", errMsg ? errMsg : "<null>");
        sqlite3_free(errMsg);
        return false;
    }

    auto rollback = [&]() {
        // Some failures (e.g. OOM opening the journal) may leave us in autocommit mode, making ROLLBACK noisy.
        if (sqlite3_get_autocommit(db) == 0) {
            (void)sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
        }
    };

    for (const auto& stmt : statements) {
        errMsg = nullptr;
        rc = sqlite3_exec(db, stmt.c_str(), nullptr, nullptr, &errMsg);
        if (rc != SQLITE_OK) {
            constexpr std::size_t kMaxLoggedSqlChars = 120;
            const std::size_t shown = std::min(stmt.size(), kMaxLoggedSqlChars);
            Logger::instance().printf("[DB] SQL exec failed: rc=%d err=%s sql=\"%.*s%s\"\n",
                                      rc,
                                      errMsg ? errMsg : "<null>",
                                      static_cast<int>(shown),
                                      stmt.c_str(),
                                      (stmt.size() > shown) ? "..." : "");
            sqlite3_free(errMsg);
            rollback();
            return false;
        }
    }

    errMsg = nullptr;
    rc = sqlite3_exec(db, "COMMIT;", nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        Logger::instance().printf("[DB] COMMIT failed: %s\n", errMsg ? errMsg : "<null>");
        sqlite3_free(errMsg);
        rollback();
        return false;
    }

    return true;
}
}  // namespace

DatabaseManager::DatabaseManager() {
    setupDatabase();
    dbConnection = createOpenSQLConnection(KeyboardConfig::DBName.data());
    dbAvailable_ = (dbConnection != nullptr);

    if (dbAvailable_) {
        const bool inputsOk = createSQLTable(dbConnection, KeyboardConfig::Tables::Inputs);
        const bool mastersOk = createSQLTable(dbConnection, KeyboardConfig::Tables::RadioMasters);
        dbAvailable_ = inputsOk && mastersOk;
    }
}

DatabaseManager& DatabaseManager::getInstance() {
    static DatabaseManager instance;
    return instance;
}

std::vector<KeyboardConfig::NodeInfo> DatabaseManager::getRadioNodes() {
    std::vector<KeyboardConfig::NodeInfo> results;

    Logger::instance().println("[DB][Q] SELECT RadioMasters");
    this->getData([&results](sqlite3_stmt* row) {
        const std::uint32_t id = static_cast<std::uint32_t>(sqlite3_column_int(row, 0));
        const std::uint64_t addr = static_cast<std::uint64_t>(sqlite3_column_int64(row, 1));
        results.push_back({id, addr});
    }, KeyboardConfig::Tables::RadioMasters);

    Logger::instance().printf("[DB][Q] SELECT RadioMasters returned=%u\n", static_cast<unsigned>(results.size()));
    return results;
}

void DatabaseManager::getData(const std::function<void(sqlite3_stmt*)>& callback, const DBTable& table) {
    if (!dbConnection || !dbAvailable_) {
        return;
    }

    Logger::instance().printf("[DB][Q] SELECT * FROM %s\n", table.tableName.c_str());
    Threads::Scope scope(dbMutex_);

    const std::string query = "SELECT * FROM " + table.tableName + ";";
    sqlite3_stmt* stmt = nullptr;

    const int rc = sqlite3_prepare_v2(dbConnection, query.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        Logger::instance().printf("[DB] Read error: %s\n", sqlite3_errmsg(dbConnection));
        return;
    }

    std::uint32_t rows = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        callback(stmt);
        ++rows;
    }

    sqlite3_finalize(stmt);
    Logger::instance().printf("[DB][Q] SELECT * FROM %s rows=%u\n", table.tableName.c_str(), static_cast<unsigned>(rows));
}

void DatabaseManager::saveData(std::vector<std::string> data, const DBTable& table) {
    if (data.empty()) {
        return;
    }

    if (!dbAvailable_) {
        // Keep keyboard responsive even if the SD/DB is missing by dropping rows.
        static bool warned = false;
        if (!warned) {
            Logger::instance().println("[DB] Database unavailable. Dropping rows.");
            warned = true;
        }
        return;
    }

    escapeInsertDataInPlace(table, data);
    std::string sqlStatement = buildSQLInsertStatement(table, data);
    if (sqlStatement.empty()) {
        return;
    }

    Threads::Scope scope(queueMutex);
    constexpr std::size_t kMaxPendingStatements = 500;
    if (pendingStatements_.size() >= kMaxPendingStatements) {
        static bool warnedQueueFull = false;
        if (!warnedQueueFull) {
            Logger::instance().println("[DB] Insert queue full. Dropping rows.");
            warnedQueueFull = true;
        }
        return;
    }

    pendingStatements_.push_back(std::move(sqlStatement));
}

void DatabaseManager::processQueue() {
    if (!dbConnection || !dbAvailable_) {
        return;
    }

    static std::uint32_t lastWriteTime = 0;
    static std::uint32_t lastFailureTime = 0;

    constexpr std::size_t kBatchSize = 20;
    constexpr std::size_t kMaxBatchPerFlush = 50;
    constexpr std::uint32_t kMaxFlushDelayMs = 60'000;
    constexpr std::uint32_t kFailureBackoffMs = 5'000;

    std::vector<std::string> batchToSave;
    const std::uint32_t now = millis();

    {
        Threads::Scope scope(queueMutex);

        const bool backoffActive = (lastFailureTime != 0U) && (now - lastFailureTime < kFailureBackoffMs);
        const bool shouldWrite = (pendingStatements_.size() >= kBatchSize) ||
                                 (!pendingStatements_.empty() && (now - lastWriteTime > kMaxFlushDelayMs));

        if (backoffActive || !shouldWrite) {
            return;
        }

        const std::size_t toFlush = std::min<std::size_t>(pendingStatements_.size(), kMaxBatchPerFlush);
        batchToSave.reserve(toFlush);

        // Flush oldest first to preserve chronological ordering.
        for (std::size_t i = 0; i < toFlush; ++i) {
            batchToSave.push_back(std::move(pendingStatements_[i]));
        }
        using Diff = std::vector<std::string>::difference_type;
        pendingStatements_.erase(pendingStatements_.begin(), pendingStatements_.begin() + static_cast<Diff>(toFlush));
    }

    const bool ok = [&]() {
        Threads::Scope dbLock(dbMutex_);
        return executeSqlTransaction(dbConnection, batchToSave);
    }();

    if (!ok) {
        Logger::instance().println("[DB] Transaction failed, re-queueing rows.");
        Threads::Scope scope(queueMutex);
        pendingStatements_.insert(pendingStatements_.begin(),
                                  std::make_move_iterator(batchToSave.begin()),
                                  std::make_move_iterator(batchToSave.end()));
        lastFailureTime = now;
    } else {
        lastFailureTime = 0;

        std::size_t pending = 0;
        {
            Threads::Scope scope(queueMutex);
            pending = pendingStatements_.size();
        }

        Logger::instance().printf("[DB] Committed %u insert(s). Pending=%u\n",
                                  static_cast<unsigned>(batchToSave.size()),
                                  static_cast<unsigned>(pending));
    }

    lastWriteTime = now;
}

bool DatabaseManager::countRows(const DBTable& table, std::uint32_t& outCount) {
    outCount = 0;

    if (!dbConnection || !dbAvailable_) {
        Logger::instance().println("[DB][Q] COUNT: db unavailable");
        return false;
    }

    Logger::instance().printf("[DB][Q] COUNT %s\n", table.tableName.c_str());
    Threads::Scope scope(dbMutex_);

    const std::string sql = "SELECT COUNT(*) FROM " + table.tableName + ";";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(dbConnection, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        Logger::instance().printf("[DB] COUNT prepare failed (%s): %s\n", table.tableName.c_str(),
                                  sqlite3_errmsg(dbConnection));
        return false;
    }

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        const auto v = sqlite3_column_int64(stmt, 0);
        if (v >= 0) {
            outCount = static_cast<std::uint32_t>(v);
        }
    } else {
        Logger::instance().printf("[DB] COUNT step failed (%s): rc=%d err=%s\n", table.tableName.c_str(), rc,
                                  sqlite3_errmsg(dbConnection));
        sqlite3_finalize(stmt);
        return false;
    }

    sqlite3_finalize(stmt);
    Logger::instance().printf("[DB][Q] COUNT %s => %u\n", table.tableName.c_str(), static_cast<unsigned>(outCount));
    return true;
}

std::vector<std::string> DatabaseManager::tailInputs(std::size_t limit) {
    std::vector<std::string> lines;
    if (!dbConnection || !dbAvailable_) {
        Logger::instance().println("[DB][Q] TAIL Inputs: db unavailable");
        return lines;
    }

    constexpr std::size_t kMaxLimit = 10;
    constexpr int kMaxWordBytes = 48;
    limit = std::min<std::size_t>(limit, kMaxLimit);
    if (limit == 0) {
        return lines;
    }

    Logger::instance().printf("[DB][Q] TAIL Inputs limit=%u\n", static_cast<unsigned>(limit));
    Threads::Scope scope(dbMutex_);

    sqlite3_stmt* stmt = nullptr;
    const int rc = sqlite3_prepare_v2(dbConnection,
                                      "SELECT InputID, Timestamp, Input FROM Inputs ORDER BY InputID DESC LIMIT ?1;",
                                      -1,
                                      &stmt,
                                      nullptr);
    if (rc != SQLITE_OK) {
        Logger::instance().printf("[DB] TAIL prepare failed (Inputs): %s\n", sqlite3_errmsg(dbConnection));
        return lines;
    }

    (void)sqlite3_bind_int(stmt, 1, static_cast<int>(limit));

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const auto id = sqlite3_column_int64(stmt, 0);
        const double ts = sqlite3_column_double(stmt, 1);

        const auto* txt = sqlite3_column_text(stmt, 2);
        const int txtBytes = sqlite3_column_bytes(stmt, 2);
        const int copyLen = std::min<int>(std::max<int>(txtBytes, 0), kMaxWordBytes);

        char word[kMaxWordBytes + 4]{};  // + "..."
        if (txt && copyLen > 0) {
            std::memcpy(word, txt, static_cast<std::size_t>(copyLen));
            word[copyLen] = '\0';
        } else {
            word[0] = '\0';
        }

        const bool truncated = txtBytes > kMaxWordBytes;

        char buf[220]{};
        std::snprintf(buf, sizeof(buf), "#%lld ts=%.3f input=\"%s%s\"", static_cast<long long>(id), ts, word,
                      truncated ? "..." : "");
        lines.emplace_back(buf);
    }

    sqlite3_finalize(stmt);
    Logger::instance().printf("[DB][Q] TAIL Inputs returned=%u\n", static_cast<unsigned>(lines.size()));
    return lines;
}
