#include "DatabaseManager.h"

#include <Arduino.h>
#include <array>
#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ArduinoSQLiteHandler.h"
#include "Logger.h"
#include "usb_serial.h"
#include "../config/KeyboardConfig.h"

namespace {
bool isColumnType(const std::string& type, const std::string_view needle) {
    return type.find(needle) != std::string::npos;
}

bool isTextColumnType(const std::string& columnType) {
    return isColumnType(columnType, "TEXT");
}

bool isIntegerColumnType(const std::string& columnType) {
    return isColumnType(columnType, "INTEGER");
}

bool isRealColumnType(const std::string& columnType) {
    return isColumnType(columnType, "REAL");
}

std::string buildInsertSql(const DBTable& table) {
    std::string sql = "INSERT INTO " + table.tableName + " (";
    std::string values = "VALUES (";

    bool first = true;
    std::size_t insertableCols = 0;
    for (const auto& col : table.columns) {
        if (col.isPrimaryKey) {
            continue;
        }

        if (!first) {
            sql += ", ";
            values += ", ";
        }
        first = false;

        sql += col.name;
        values += "?";
        ++insertableCols;
    }

    if (insertableCols == 0) {
        return {};
    }

    sql += ") ";
    values += ");";
    sql += values;
    return sql;
}

bool tryParseInt64(const std::string& s, std::int64_t& out) {
    if (s.empty()) {
        return false;
    }

    char* end = nullptr;
    errno = 0;
    const long long v = std::strtoll(s.c_str(), &end, 10);
    if (errno != 0) {
        return false;
    }

    if (end == nullptr || end == s.c_str() || *end != '\0') {
        return false;
    }

    out = static_cast<std::int64_t>(v);
    return true;
}

bool tryParseDouble(const std::string& s, double& out) {
    if (s.empty()) {
        return false;
    }

    char* end = nullptr;
    errno = 0;
    const double v = std::strtod(s.c_str(), &end);
    if (errno != 0) {
        return false;
    }

    if (end == nullptr || end == s.c_str() || *end != '\0') {
        return false;
    }

    out = v;
    return true;
}

int bindColumnValue(sqlite3_stmt* stmt, int paramIndex, const DBColumn& col, const std::string& value) {
    if (stmt == nullptr) {
        return SQLITE_MISUSE;
    }

    if (isTextColumnType(col.type)) {
        return sqlite3_bind_text(stmt, paramIndex, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
    }

    if (isIntegerColumnType(col.type)) {
        std::int64_t v = 0;
        if (tryParseInt64(value, v)) {
            return sqlite3_bind_int64(stmt, paramIndex, v);
        }
        if (value.empty()) {
            return sqlite3_bind_null(stmt, paramIndex);
        }
        // Fallback: keep the row, but log invalid numeric input.
        Logger::instance().printf("[DB] Warning: invalid INTEGER value for column '%s': '%s'\n", col.name.c_str(),
                                  value.c_str());
        return sqlite3_bind_text(stmt, paramIndex, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
    }

    if (isRealColumnType(col.type)) {
        double v = 0.0;
        if (tryParseDouble(value, v)) {
            return sqlite3_bind_double(stmt, paramIndex, v);
        }
        if (value.empty()) {
            return sqlite3_bind_null(stmt, paramIndex);
        }
        Logger::instance().printf("[DB] Warning: invalid REAL value for column '%s': '%s'\n", col.name.c_str(),
                                  value.c_str());
        return sqlite3_bind_text(stmt, paramIndex, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
    }

    // Unknown column type: bind as text.
    return sqlite3_bind_text(stmt, paramIndex, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
}

int bindInsertValues(sqlite3_stmt* stmt, const DBTable& table, const std::vector<std::string>& values) {
    int paramIndex = 1;
    std::size_t dataIndex = 0;

    for (const auto& col : table.columns) {
        if (col.isPrimaryKey) {
            continue;
        }

        if (dataIndex >= values.size()) {
            return SQLITE_MISMATCH;
        }

        const int rc = bindColumnValue(stmt, paramIndex, col, values[dataIndex]);
        if (rc != SQLITE_OK) {
            return rc;
        }

        ++paramIndex;
        ++dataIndex;
    }

    // Extra provided values are considered a mismatch (programmer error).
    if (dataIndex != values.size()) {
        return SQLITE_MISMATCH;
    }

    return SQLITE_OK;
}

struct PreparedInsertStmt {
    const DBTable* table = nullptr;
    std::string sql;
    sqlite3_stmt* stmt = nullptr;
};

void finalizePrepared(std::vector<PreparedInsertStmt>& prepared) {
    for (auto& p : prepared) {
        if (p.stmt) {
            sqlite3_finalize(p.stmt);
            p.stmt = nullptr;
        }
    }
}

template <typename Inserts>
bool executeInsertTransaction(sqlite3* db, const Inserts& inserts) {
    if (db == nullptr) {
        return false;
    }

    if (inserts.empty()) {
        return true;
    }

    char* errMsg = nullptr;
    int rc = sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        Logger::instance().printf("[DB] BEGIN failed: %s\n", errMsg ? errMsg : "<null>");
        sqlite3_free(errMsg);
        return false;
    }

	    std::vector<PreparedInsertStmt> prepared;
	    prepared.reserve(2);  // common case: Inputs + RadioMasters

	    auto rollback = [&]() {
	        // Some failures (e.g. OOM opening the journal) may leave us in autocommit mode, making ROLLBACK noisy.
	        if (sqlite3_get_autocommit(db) == 0) {
	            (void)sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
	        }
	    };

    for (const auto& ins : inserts) {
        if (ins.table == nullptr) {
            rollback();
            finalizePrepared(prepared);
            return false;
        }

        const DBTable& table = *ins.table;

        PreparedInsertStmt* cached = nullptr;
        for (auto& p : prepared) {
            if (p.table == ins.table) {
                cached = &p;
                break;
            }
        }

        if (cached == nullptr) {
            PreparedInsertStmt p;
            p.table = ins.table;
            p.sql = buildInsertSql(table);
            if (p.sql.empty()) {
                Logger::instance().printf("[DB] Failed to build INSERT SQL for table '%s'\n", table.tableName.c_str());
                rollback();
                finalizePrepared(prepared);
                return false;
            }

            rc = sqlite3_prepare_v2(db, p.sql.c_str(), -1, &p.stmt, nullptr);
            if (rc != SQLITE_OK) {
                Logger::instance().printf("[DB] Prepare failed (%s): %s\n", table.tableName.c_str(), sqlite3_errmsg(db));
                rollback();
                finalizePrepared(prepared);
                return false;
            }

            prepared.push_back(std::move(p));
            cached = &prepared.back();
        }

        rc = sqlite3_reset(cached->stmt);
        if (rc != SQLITE_OK) {
            Logger::instance().printf("[DB] Reset failed (%s): %s\n", table.tableName.c_str(), sqlite3_errmsg(db));
            rollback();
            finalizePrepared(prepared);
            return false;
        }

        rc = sqlite3_clear_bindings(cached->stmt);
        if (rc != SQLITE_OK) {
            Logger::instance().printf("[DB] Clear bindings failed (%s): %s\n", table.tableName.c_str(),
                                      sqlite3_errmsg(db));
            rollback();
            finalizePrepared(prepared);
            return false;
        }

        rc = bindInsertValues(cached->stmt, table, ins.values);
        if (rc != SQLITE_OK) {
            Logger::instance().printf("[DB] Bind failed (%s): rc=%d\n", table.tableName.c_str(), rc);
            rollback();
            finalizePrepared(prepared);
            return false;
        }

        rc = sqlite3_step(cached->stmt);
        if (rc != SQLITE_DONE) {
            Logger::instance().printf("[DB] Step failed (%s): rc=%d err=%s\n", table.tableName.c_str(), rc,
                                      sqlite3_errmsg(db));
            rollback();
            finalizePrepared(prepared);
            return false;
        }
    }

    rc = sqlite3_exec(db, "COMMIT;", nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        Logger::instance().printf("[DB] COMMIT failed: %s\n", errMsg ? errMsg : "<null>");
        sqlite3_free(errMsg);
        rollback();
        finalizePrepared(prepared);
        return false;
    }

    finalizePrepared(prepared);
    return true;
}

	int migrateInputTimestampsToSecondsStep(sqlite3* db, std::size_t maxRows) {
	    if (db == nullptr || maxRows == 0) {
	        return 0;
	    }

	    // Historical rows used to store `micros()` directly (microseconds). We now store seconds.
	    // `micros()` is a 32-bit counter (wraps at ~4294 seconds), therefore any timestamp >10k cannot be valid
	    // seconds-based data and is safe to convert from microseconds.
	    constexpr double kLegacyThresholdSeconds = 10000.0;
	    constexpr double kMicrosToSeconds = 1000000.0;
	    constexpr std::size_t kMaxRowsPerStep = 32;

	    const std::size_t cappedRows = std::min(maxRows, kMaxRowsPerStep);
	    if (cappedRows == 0) {
	        return 0;
	    }

	    struct ScopedStmt {
	        sqlite3_stmt* stmt = nullptr;
	        ScopedStmt() = default;
	        ~ScopedStmt() {
	            if (stmt) {
	                sqlite3_finalize(stmt);
	                stmt = nullptr;
	            }
	        }
	        ScopedStmt(const ScopedStmt&) = delete;
	        ScopedStmt& operator=(const ScopedStmt&) = delete;
	        ScopedStmt(ScopedStmt&&) = delete;
	        ScopedStmt& operator=(ScopedStmt&&) = delete;
	    };

	    std::array<sqlite3_int64, kMaxRowsPerStep> rowids{};
	    std::size_t rowCount = 0;

	    // Avoid a single complex UPDATE-with-subquery which can OOM on-device. Instead:
	    // 1) Select a small batch of rowids that look like legacy microseconds timestamps
	    // 2) Update each row individually inside a short transaction
	    {
	        ScopedStmt selectStmt;
	        int rc = sqlite3_prepare_v2(db,
	                                   "SELECT rowid FROM Inputs WHERE Timestamp > ?1 LIMIT ?2;",
	                                   -1,
	                                   &selectStmt.stmt,
	                                   nullptr);
	        if (rc == SQLITE_NOMEM) {
	            Logger::instance().println("[DB] Timestamp migration failed: out of memory (prepare SELECT)");
	            return -2;
	        }
	        if (rc != SQLITE_OK) {
	            Logger::instance().printf("[DB] Timestamp migration failed (prepare SELECT): %s\n", sqlite3_errmsg(db));
	            return -1;
	        }

	        rc = sqlite3_bind_double(selectStmt.stmt, 1, kLegacyThresholdSeconds);
	        if (rc == SQLITE_NOMEM) {
	            Logger::instance().println("[DB] Timestamp migration failed: out of memory (bind threshold)");
	            return -2;
	        }
	        if (rc != SQLITE_OK) {
	            Logger::instance().printf("[DB] Timestamp migration failed (bind threshold): rc=%d\n", rc);
	            return -1;
	        }

	        rc = sqlite3_bind_int(selectStmt.stmt, 2, static_cast<int>(cappedRows));
	        if (rc == SQLITE_NOMEM) {
	            Logger::instance().println("[DB] Timestamp migration failed: out of memory (bind limit)");
	            return -2;
	        }
	        if (rc != SQLITE_OK) {
	            Logger::instance().printf("[DB] Timestamp migration failed (bind limit): rc=%d\n", rc);
	            return -1;
	        }

	        int stepRc = SQLITE_OK;
	        while (rowCount < cappedRows) {
	            stepRc = sqlite3_step(selectStmt.stmt);
	            if (stepRc == SQLITE_ROW) {
	                rowids[rowCount] = sqlite3_column_int64(selectStmt.stmt, 0);
	                ++rowCount;
	                continue;
	            }
	            break;
	        }

	        if (stepRc != SQLITE_DONE && stepRc != SQLITE_ROW) {
	            if (stepRc == SQLITE_NOMEM) {
	                Logger::instance().println("[DB] Timestamp migration failed: out of memory (step SELECT)");
	                return -2;
	            }
	            Logger::instance().printf("[DB] Timestamp migration failed (step SELECT): rc=%d err=%s\n",
	                                      stepRc,
	                                      sqlite3_errmsg(db));
	            return -1;
	        }
	    }

	    if (rowCount == 0) {
	        return 0;
	    }

	    ScopedStmt updateStmt;
	    {
	        const int rc = sqlite3_prepare_v2(db,
	                                          "UPDATE Inputs SET Timestamp = Timestamp / ?1 WHERE rowid = ?2;",
	                                          -1,
	                                          &updateStmt.stmt,
	                                          nullptr);
	        if (rc == SQLITE_NOMEM) {
	            Logger::instance().println("[DB] Timestamp migration failed: out of memory (prepare UPDATE)");
	            return -2;
	        }
	        if (rc != SQLITE_OK) {
	            Logger::instance().printf("[DB] Timestamp migration failed (prepare UPDATE): %s\n", sqlite3_errmsg(db));
	            return -1;
	        }
	    }

	    char* errMsg = nullptr;
	    int rc = sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, &errMsg);
	    if (rc != SQLITE_OK) {
	        Logger::instance().printf("[DB] Timestamp migration failed (BEGIN): %s\n", errMsg ? errMsg : "<null>");
	        sqlite3_free(errMsg);
	        return (rc == SQLITE_NOMEM) ? -2 : -1;
	    }

	    auto rollback = [&]() {
	        (void)sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
	    };

	    for (std::size_t i = 0; i < rowCount; ++i) {
	        rc = sqlite3_reset(updateStmt.stmt);
	        if (rc != SQLITE_OK) {
	            Logger::instance().printf("[DB] Timestamp migration failed (reset UPDATE): %s\n", sqlite3_errmsg(db));
	            rollback();
	            return (rc == SQLITE_NOMEM) ? -2 : -1;
	        }

	        rc = sqlite3_clear_bindings(updateStmt.stmt);
	        if (rc != SQLITE_OK) {
	            Logger::instance().printf("[DB] Timestamp migration failed (clear bindings UPDATE): %s\n",
	                                      sqlite3_errmsg(db));
	            rollback();
	            return (rc == SQLITE_NOMEM) ? -2 : -1;
	        }

	        rc = sqlite3_bind_double(updateStmt.stmt, 1, kMicrosToSeconds);
	        if (rc != SQLITE_OK) {
	            Logger::instance().printf("[DB] Timestamp migration failed (bind divisor UPDATE): rc=%d\n", rc);
	            rollback();
	            return (rc == SQLITE_NOMEM) ? -2 : -1;
	        }

	        rc = sqlite3_bind_int64(updateStmt.stmt, 2, rowids[i]);
	        if (rc != SQLITE_OK) {
	            Logger::instance().printf("[DB] Timestamp migration failed (bind rowid UPDATE): rc=%d\n", rc);
	            rollback();
	            return (rc == SQLITE_NOMEM) ? -2 : -1;
	        }

	        rc = sqlite3_step(updateStmt.stmt);
	        if (rc != SQLITE_DONE) {
	            Logger::instance().printf("[DB] Timestamp migration failed (step UPDATE): rc=%d err=%s\n",
	                                      rc,
	                                      sqlite3_errmsg(db));
	            rollback();
	            return (rc == SQLITE_NOMEM) ? -2 : -1;
	        }
	    }

	    rc = sqlite3_exec(db, "COMMIT;", nullptr, nullptr, &errMsg);
	    if (rc != SQLITE_OK) {
	        Logger::instance().printf("[DB] Timestamp migration failed (COMMIT): %s\n", errMsg ? errMsg : "<null>");
	        sqlite3_free(errMsg);
	        rollback();
	        return (rc == SQLITE_NOMEM) ? -2 : -1;
	    }

	    return static_cast<int>(rowCount);
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

    this->getData([&results](sqlite3_stmt* row) {
        const std::uint32_t id = static_cast<std::uint32_t>(sqlite3_column_int(row, 0));
        const std::uint64_t addr = static_cast<std::uint64_t>(sqlite3_column_int64(row, 1));
        results.push_back({id, addr});
    }, KeyboardConfig::Tables::RadioMasters);

    return results;
}

void DatabaseManager::getData(const std::function<void(sqlite3_stmt*)>& callback, const DBTable& table) {
    if (!dbConnection || !dbAvailable_) {
        return;
    }

    Threads::Scope scope(dbMutex_);

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

    PendingInsert pending;
    pending.table = &table;
    pending.values = std::move(data);

    Threads::Scope scope(queueMutex);
    constexpr std::size_t kMaxPendingInserts = 500;
    if (pendingInserts_.size() >= kMaxPendingInserts) {
        static bool warnedQueueFull = false;
        if (!warnedQueueFull) {
            Logger::instance().println("[DB] Insert queue full. Dropping rows.");
            warnedQueueFull = true;
        }
        return;
    }
    pendingInserts_.push_back(std::move(pending));
}

void DatabaseManager::processQueue() {
    if (!dbConnection || !dbAvailable_) {
        return;
    }

    static std::uint32_t lastWriteTime = 0;
    static std::uint32_t lastFailureTime = 0;
    static std::uint32_t lastMigrationTime = 0;
    static bool migrationDone = false;
    static bool migrationAnnounced = false;
    constexpr std::size_t kBatchSize = 20;
    constexpr std::size_t kMaxBatchPerFlush = 50;
    constexpr std::uint32_t kMaxFlushDelayMs = 60'000;
	    constexpr std::uint32_t kFailureBackoffMs = 5'000;
	    constexpr std::uint32_t kMigrationIntervalMs = 250;
	    constexpr std::size_t kMigrationChunkRows = 32;

    std::vector<PendingInsert> batchToSave;
    bool queueEmpty = false;
    const std::uint32_t now = millis();

    {
        Threads::Scope scope(queueMutex);
        queueEmpty = pendingInserts_.empty();

        const bool backoffActive = (lastFailureTime != 0U) && (now - lastFailureTime < kFailureBackoffMs);
        const bool shouldWrite = (pendingInserts_.size() >= kBatchSize) ||
                                 (!pendingInserts_.empty() && (now - lastWriteTime > kMaxFlushDelayMs));

        if (shouldWrite && !backoffActive) {
            const std::size_t toFlush = std::min<std::size_t>(pendingInserts_.size(), kMaxBatchPerFlush);
            batchToSave.reserve(toFlush);

            const std::size_t startIndex = pendingInserts_.size() - toFlush;
            for (std::size_t i = startIndex; i < pendingInserts_.size(); ++i) {
                batchToSave.push_back(std::move(pendingInserts_[i]));
            }
            pendingInserts_.resize(startIndex);
        }
    }

	    if (!batchToSave.empty()) {
	        const bool ok = [&]() {
	            Threads::Scope dbLock(dbMutex_);
	            return executeInsertTransaction(dbConnection, batchToSave);
	        }();

	        if (!ok) {
	            Logger::instance().println("[DB] Transaction failed, re-queueing rows.");
	            Threads::Scope scope(queueMutex);
	            pendingInserts_.insert(pendingInserts_.end(),
	                                   std::make_move_iterator(batchToSave.begin()),
	                                   std::make_move_iterator(batchToSave.end()));
	            lastFailureTime = now;
	        } else {
	            lastFailureTime = 0;

	            std::size_t inputsCount = 0;
	            std::size_t mastersCount = 0;
	            std::size_t otherCount = 0;
	            for (const auto& ins : batchToSave) {
	                if (ins.table == &KeyboardConfig::Tables::Inputs) {
	                    ++inputsCount;
	                } else if (ins.table == &KeyboardConfig::Tables::RadioMasters) {
	                    ++mastersCount;
	                } else {
	                    ++otherCount;
	                }
	            }

	            std::size_t pending = 0;
	            {
	                Threads::Scope scope(queueMutex);
	                pending = pendingInserts_.size();
	            }

	            Logger::instance().printf("[DB] Committed %u insert(s) (Inputs=%u Masters=%u Other=%u). Pending=%u\n",
	                                      static_cast<unsigned>(batchToSave.size()),
	                                      static_cast<unsigned>(inputsCount),
	                                      static_cast<unsigned>(mastersCount),
	                                      static_cast<unsigned>(otherCount),
	                                      static_cast<unsigned>(pending));
	        }
	        lastWriteTime = now;
	        return;
	    }

    // No writes this tick. Optionally migrate legacy timestamps in small chunks.
    const bool backoffActive = (lastFailureTime != 0U) && (now - lastFailureTime < kFailureBackoffMs);
    if (!migrationDone && !backoffActive && queueEmpty && (now - lastMigrationTime >= kMigrationIntervalMs)) {
        lastMigrationTime = now;

        if (!migrationAnnounced) {
            Logger::instance().println("[DB] Migrating legacy timestamps to seconds...");
            migrationAnnounced = true;
        }

        const int changed = [&]() {
            Threads::Scope dbLock(dbMutex_);
            return migrateInputTimestampsToSecondsStep(dbConnection, kMigrationChunkRows);
        }();

	        if (changed == 0) {
	            migrationDone = true;
	            Logger::instance().println("[DB] Timestamp migration complete.");
	        } else if (changed == -2) {
	            // If we hit SQLITE_NOMEM during migration, stop trying: repeated attempts can further fragment heap
	            // and will spam logs. If legacy data matters, migrate off-device or delete/recreate the DB.
	            migrationDone = true;
	            Logger::instance().println("[DB] Timestamp migration disabled (out of memory).");
	        } else if (changed < 0) {
	            // Avoid hammering the SD card on repeated failures.
	            lastFailureTime = now;
	        }
	    }
	}
