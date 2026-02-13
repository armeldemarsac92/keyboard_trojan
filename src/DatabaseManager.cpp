#include "DatabaseManager.h"

#include <Arduino.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ArduinoSQLiteHandler.h"
#include "Logger.h"
#include "../config/KeyboardConfig.h"

namespace {
class TryLockGuard {
public:
    explicit TryLockGuard(Threads::Mutex& mutex) : mutex_(&mutex), locked_(mutex_->try_lock() != 0) {}
    TryLockGuard(const TryLockGuard&) = delete;
    TryLockGuard& operator=(const TryLockGuard&) = delete;
    ~TryLockGuard() {
        if (locked_ && mutex_ != nullptr) {
            (void)mutex_->unlock();
        }
    }

    [[nodiscard]] bool locked() const { return locked_; }
    explicit operator bool() const { return locked_; }

private:
    Threads::Mutex* mutex_ = nullptr;
    bool locked_ = false;
};

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

void configureEmbeddedPragmas(sqlite3* db) {
    if (db == nullptr) {
        return;
    }

    auto exec = [&](const char* sql) {
        char* errMsg = nullptr;
        const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &errMsg);
        if (rc != SQLITE_OK) {
            Logger::instance().printf("[DB] PRAGMA failed rc=%d err=%s sql=%s\n",
                                      rc,
                                      errMsg ? errMsg : "<null>",
                                      sql);
            sqlite3_free(errMsg);
        }
    };

    // Bound SQLite memory usage for large scans on embedded targets.
    // Negative values are interpreted as KiB (not pages).
    exec("PRAGMA cache_size=-128;");
    exec("PRAGMA temp_store=FILE;");
}

std::string formatRowAsKeyValue(sqlite3_stmt* stmt) {
    if (stmt == nullptr) {
        return {};
    }

    constexpr int kMaxTextBytes = 64;

    const int cols = sqlite3_column_count(stmt);
    if (cols <= 0) {
        return {};
    }

    std::string out;
    out.reserve(200);

    // Column 0 is expected to be rowid for our helper queries.
    const auto rowid = sqlite3_column_int64(stmt, 0);
    out += "#rowid=";
    out += std::to_string(static_cast<long long>(rowid));

    for (int i = 1; i < cols; ++i) {
        const char* name = sqlite3_column_name(stmt, i);
        if (name == nullptr || *name == '\0') {
            name = "?";
        }

        out.push_back(' ');
        out += name;
        out.push_back('=');

        const int type = sqlite3_column_type(stmt, i);
        switch (type) {
            case SQLITE_INTEGER: {
                out += std::to_string(static_cast<long long>(sqlite3_column_int64(stmt, i)));
                break;
            }
            case SQLITE_FLOAT: {
                char buf[32]{};
                std::snprintf(buf, sizeof(buf), "%.3f", sqlite3_column_double(stmt, i));
                out += buf;
                break;
            }
            case SQLITE_TEXT: {
                const auto* txt = sqlite3_column_text(stmt, i);
                const int bytes = sqlite3_column_bytes(stmt, i);
                const int copyLen = std::min<int>(std::max<int>(bytes, 0), kMaxTextBytes);

                out.push_back('"');
                if (txt && copyLen > 0) {
                    out.append(reinterpret_cast<const char*>(txt), static_cast<std::size_t>(copyLen));
                }
                const bool truncated = bytes > kMaxTextBytes;
                if (truncated) {
                    out += "...";
                }
                out.push_back('"');
                break;
            }
            case SQLITE_NULL: {
                out += "NULL";
                break;
            }
            case SQLITE_BLOB:
            default: {
                const int bytes = sqlite3_column_bytes(stmt, i);
                out += "<blob ";
                out += std::to_string(bytes);
                out += ">";
                break;
            }
        }

        if (out.size() > 220) {
            out.resize(220);
            out += "...";
            break;
        }
    }

    return out;
}

bool queryRowidRangeUnlocked(sqlite3* db, const std::string& tableName, std::uint64_t& outMin, std::uint64_t& outMax) {
    outMin = 0;
    outMax = 0;
    if (db == nullptr) {
        return false;
    }

    // Avoid aggregates like MIN/MAX which can degrade to full-table scans depending on query planner settings.
    // These ORDER BY/LIMIT queries should hit the table b-tree extremes quickly.
    auto selectEdge = [&](const char* orderBy, std::uint64_t& out) -> bool {
        out = 0;
        const std::string sql = "SELECT rowid FROM " + tableName + " ORDER BY rowid " + orderBy + " LIMIT 1;";
        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            return false;
        }

        rc = sqlite3_step(stmt);
        if (rc == SQLITE_ROW && sqlite3_column_type(stmt, 0) != SQLITE_NULL) {
            const auto v = sqlite3_column_int64(stmt, 0);
            if (v > 0) {
                out = static_cast<std::uint64_t>(v);
            }
            sqlite3_finalize(stmt);
            return out != 0;
        }

        sqlite3_finalize(stmt);
        return false;
    };

    const bool okMin = selectEdge("ASC", outMin);
    const bool okMax = selectEdge("DESC", outMax);
    return okMin && okMax;
}

bool queryRowByRowidUnlocked(sqlite3* db, const std::string& tableName, std::uint64_t rowid, std::string& outLine) {
    outLine.clear();
    if (db == nullptr) {
        return false;
    }

    const std::string sql = "SELECT rowid, * FROM " + tableName + " WHERE rowid = ?1 LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return false;
    }

    (void)sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(rowid));

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        outLine = formatRowAsKeyValue(stmt);
        sqlite3_finalize(stmt);
        return !outLine.empty();
    }

    sqlite3_finalize(stmt);
    return false;
}

bool queryLastRowUnlocked(sqlite3* db, const std::string& tableName, std::string& outLine) {
    outLine.clear();
    if (db == nullptr) {
        return false;
    }

    const std::string sql = "SELECT rowid, * FROM " + tableName + " ORDER BY rowid DESC LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return false;
    }

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        outLine = formatRowAsKeyValue(stmt);
        sqlite3_finalize(stmt);
        return !outLine.empty();
    }

    sqlite3_finalize(stmt);
    return false;
}
}  // namespace

DatabaseManager::DatabaseManager() {
    setupDatabase();
    dbConnection = createOpenSQLConnection(KeyboardConfig::DBName.data());
    dbAvailable_ = (dbConnection != nullptr);

    if (dbAvailable_) {
        Logger::instance().printf("[DB] sqlite3_threadsafe=%d\n", sqlite3_threadsafe());
        configureEmbeddedPragmas(dbConnection);

        const bool inputsOk = createSQLTable(dbConnection, KeyboardConfig::Tables::Inputs);
        const bool mastersOk = createSQLTable(dbConnection, KeyboardConfig::Tables::RadioMasters);
        const bool logsOk = createSQLTable(dbConnection, KeyboardConfig::Tables::Logs);
        dbAvailable_ = inputsOk && mastersOk && logsOk;
    }
}

DatabaseManager& DatabaseManager::getInstance() {
    static DatabaseManager instance;
    return instance;
}

void DatabaseManager::setReplyCallback(ReplyCallback cb) {
    Threads::Scope scope(jobsMutex_);
    replyCb_ = cb;
}

void DatabaseManager::sendReply_(std::uint32_t dest, std::uint8_t channel, std::string text) {
    if (text.empty()) {
        return;
    }

    ReplyCallback cb = nullptr;
    {
        Threads::Scope scope(jobsMutex_);
        cb = replyCb_;
    }

    if (cb == nullptr) {
        return;
    }

    cb(dest, channel, std::move(text));
}

bool DatabaseManager::enqueueJob_(PendingJob job) {
    constexpr std::size_t kMaxPendingJobs = 8;

    Threads::Scope scope(jobsMutex_);
    if (pendingJobs_.size() >= kMaxPendingJobs) {
        Logger::instance().println("[DB][JOB] queue full (dropping)");
        return false;
    }

    pendingJobs_.push_back(std::move(job));
    Logger::instance().printf("[DB][JOB] enqueued kind=%u pending=%u\n",
                              static_cast<unsigned>(pendingJobs_.back().kind),
                              static_cast<unsigned>(pendingJobs_.size()));
    return true;
}

bool DatabaseManager::enqueueQueryTableIntro(std::uint32_t replyTo, std::uint8_t channel, const DBTable& table) {
    PendingJob job;
    job.kind = JobKind::QueryTableIntro;
    job.replyTo = replyTo;
    job.channel = channel;
    job.table = &table;
    return enqueueJob_(job);
}

bool DatabaseManager::enqueueRandomRow(std::uint32_t replyTo, std::uint8_t channel, const DBTable& table) {
    PendingJob job;
    job.kind = JobKind::RandomRow;
    job.replyTo = replyTo;
    job.channel = channel;
    job.table = &table;
    return enqueueJob_(job);
}

bool DatabaseManager::enqueueCountRows(std::uint32_t replyTo, std::uint8_t channel, const DBTable& table) {
    PendingJob job;
    job.kind = JobKind::CountRows;
    job.replyTo = replyTo;
    job.channel = channel;
    job.table = &table;
    return enqueueJob_(job);
}

bool DatabaseManager::enqueueRowByRowid(std::uint32_t replyTo, std::uint8_t channel, const DBTable& table,
                                       std::uint64_t rowid) {
    PendingJob job;
    job.kind = JobKind::RowByRowid;
    job.replyTo = replyTo;
    job.channel = channel;
    job.table = &table;
    job.rowid = rowid;
    return enqueueJob_(job);
}

bool DatabaseManager::enqueueTopSecrets(std::uint32_t replyTo, std::uint8_t channel, std::size_t limit) {
    PendingJob job;
    job.kind = JobKind::TopSecrets;
    job.replyTo = replyTo;
    job.channel = channel;
    job.limit = limit;
    return enqueueJob_(job);
}

bool DatabaseManager::enqueueTailInputs(std::uint32_t replyTo, std::uint8_t channel, std::size_t limit) {
    PendingJob job;
    job.kind = JobKind::TailInputs;
    job.replyTo = replyTo;
    job.channel = channel;
    job.limit = limit;
    return enqueueJob_(job);
}

bool DatabaseManager::enqueueListRadioMasters(std::uint32_t replyTo, std::uint8_t channel) {
    PendingJob job;
    job.kind = JobKind::ListRadioMasters;
    job.replyTo = replyTo;
    job.channel = channel;
    return enqueueJob_(job);
}

void DatabaseManager::processJobsOnce_() {
    PendingJob job;
    {
        Threads::Scope scope(jobsMutex_);
        if (pendingJobs_.empty()) {
            return;
        }
        job = pendingJobs_.front();
        pendingJobs_.pop_front();
    }

    if (!dbConnection || !dbAvailable_) {
        sendReply_(job.replyTo, job.channel, "[RAK] DB unavailable.");
        return;
    }

    switch (job.kind) {
        case JobKind::QueryTableIntro: {
            if (job.table == nullptr) {
                sendReply_(job.replyTo, job.channel, "[RAK] QUERY: internal error (no table).");
                return;
            }

            std::uint64_t minRowid = 0;
            std::uint64_t maxRowid = 0;
            const bool haveRange = rowidRange(*job.table, minRowid, maxRowid) && (maxRowid > 0);
            if (haveRange) {
                char buf[140]{};
                std::snprintf(buf,
                              sizeof(buf),
                              "[RAK] %s rowid=%llu..%llu (use COUNT for exact rows)",
                              job.table->tableName.c_str(),
                              static_cast<unsigned long long>(minRowid),
                              static_cast<unsigned long long>(maxRowid));
                sendReply_(job.replyTo, job.channel, buf);
            }

            std::string line;
            if (randomRow(*job.table, line)) {
                sendReply_(job.replyTo, job.channel, std::move(line));
            } else {
                sendReply_(job.replyTo, job.channel,
                           haveRange ? "[RAK] RANDOM failed. Try: ROW <id> or send a rowid number." : "[RAK] (no rows)");
            }

            sendReply_(job.replyTo,
                       job.channel,
                       "[RAK] Next: rowid | ROW <id> | RANDOM | COUNT | SCHEMA | TABLES | SECRETS | [/QUERY]");
            return;
        }
        case JobKind::RandomRow: {
            if (job.table == nullptr) {
                sendReply_(job.replyTo, job.channel, "[RAK] RANDOM: internal error (no table).");
                return;
            }

            std::string line;
            if (randomRow(*job.table, line)) {
                sendReply_(job.replyTo, job.channel, std::move(line));
            } else {
                std::uint64_t minRowid = 0;
                std::uint64_t maxRowid = 0;
                if (rowidRange(*job.table, minRowid, maxRowid) && maxRowid > 0) {
                    sendReply_(job.replyTo, job.channel, "[RAK] RANDOM failed. Try: ROW <id> or send a rowid number.");
                } else {
                    sendReply_(job.replyTo, job.channel, "[RAK] (no rows)");
                }
            }
            return;
        }
        case JobKind::CountRows: {
            if (job.table == nullptr) {
                sendReply_(job.replyTo, job.channel, "[RAK] COUNT: internal error (no table).");
                return;
            }

            std::uint32_t count = 0;
            if (countRows(*job.table, count)) {
                char buf[80]{};
                std::snprintf(buf, sizeof(buf), "[RAK] COUNT %s = %u", job.table->tableName.c_str(),
                              static_cast<unsigned>(count));
                sendReply_(job.replyTo, job.channel, buf);
                return;
            }

            std::uint64_t minRowid = 0;
            std::uint64_t maxRowid = 0;
            if (rowidRange(*job.table, minRowid, maxRowid) && maxRowid > 0) {
                char buf[140]{};
                std::snprintf(buf,
                              sizeof(buf),
                              "[RAK] COUNT failed. Approx rows~%llu (rowid max).",
                              static_cast<unsigned long long>(maxRowid));
                sendReply_(job.replyTo, job.channel, buf);
            } else {
                sendReply_(job.replyTo, job.channel, "[RAK] COUNT failed.");
            }
            return;
        }
        case JobKind::RowByRowid: {
            if (job.table == nullptr) {
                sendReply_(job.replyTo, job.channel, "[RAK] ROW: internal error (no table).");
                return;
            }

            std::string line;
            if (rowByRowid(*job.table, job.rowid, line)) {
                sendReply_(job.replyTo, job.channel, std::move(line));
            } else {
                sendReply_(job.replyTo, job.channel, "[RAK] ROW: not found.");
            }
            return;
        }
        case JobKind::TopSecrets: {
            auto lines = topSecrets(job.limit);
            if (lines.empty()) {
                sendReply_(job.replyTo, job.channel, "[RAK] SECRETS: no data.");
                return;
            }
            for (auto& line : lines) {
                sendReply_(job.replyTo, job.channel, std::move(line));
            }
            return;
        }
        case JobKind::TailInputs: {
            auto lines = tailInputs(job.limit);
            if (lines.empty()) {
                sendReply_(job.replyTo, job.channel, " (no rows)");
                return;
            }
            for (auto& line : lines) {
                sendReply_(job.replyTo, job.channel, std::move(line));
            }
            return;
        }
        case JobKind::ListRadioMasters: {
            const auto nodes = getRadioNodes();
            if (nodes.empty()) {
                sendReply_(job.replyTo, job.channel, " (no rows)");
                return;
            }
            for (const auto& n : nodes) {
                char buf[96]{};
                std::snprintf(buf, sizeof(buf), " - id=%u addr=%llu", static_cast<unsigned>(n.id),
                              static_cast<unsigned long long>(n.address));
                sendReply_(job.replyTo, job.channel, buf);
            }
            return;
        }
    }
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
    TryLockGuard lock(dbMutex_);
    if (!lock) {
        Logger::instance().println("[DB][Q] SELECT: db busy");
        return;
    }

    const std::string query = "SELECT * FROM " + table.tableName + ";";
    sqlite3_stmt* stmt = nullptr;

    const int rc = sqlite3_prepare_v2(dbConnection, query.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        Logger::instance().printf("[DB] Read error: %s\n", sqlite3_errmsg(dbConnection));
        (void)sqlite3_db_release_memory(dbConnection);
        return;
    }

    std::uint32_t rows = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        callback(stmt);
        ++rows;
    }

    sqlite3_finalize(stmt);
    (void)sqlite3_db_release_memory(dbConnection);
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
    // Always service interactive query jobs, even when there is nothing to flush.
    processJobsOnce_();

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
        const bool ok = executeSqlTransaction(dbConnection, batchToSave);
        (void)sqlite3_db_release_memory(dbConnection);
        return ok;
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
    TryLockGuard lock(dbMutex_);
    if (!lock) {
        Logger::instance().println("[DB][Q] COUNT: db busy");
        return false;
    }

    const std::string sql = "SELECT COUNT(*) FROM " + table.tableName + ";";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(dbConnection, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        Logger::instance().printf("[DB] COUNT prepare failed (%s): %s\n", table.tableName.c_str(),
                                  sqlite3_errmsg(dbConnection));
        (void)sqlite3_db_release_memory(dbConnection);
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
        (void)sqlite3_db_release_memory(dbConnection);
        return false;
    }

    sqlite3_finalize(stmt);
    (void)sqlite3_db_release_memory(dbConnection);
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
    TryLockGuard lock(dbMutex_);
    if (!lock) {
        Logger::instance().println("[DB][Q] TAIL: db busy");
        return lines;
    }

    sqlite3_stmt* stmt = nullptr;
    const int rc = sqlite3_prepare_v2(dbConnection,
                                      "SELECT InputID, Timestamp, Input FROM Inputs ORDER BY InputID DESC LIMIT ?1;",
                                      -1,
                                      &stmt,
                                      nullptr);
    if (rc != SQLITE_OK) {
        Logger::instance().printf("[DB] TAIL prepare failed (Inputs): %s\n", sqlite3_errmsg(dbConnection));
        (void)sqlite3_db_release_memory(dbConnection);
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
    (void)sqlite3_db_release_memory(dbConnection);
    Logger::instance().printf("[DB][Q] TAIL Inputs returned=%u\n", static_cast<unsigned>(lines.size()));
    return lines;
}

bool DatabaseManager::randomRow(const DBTable& table, std::string& outLine) {
    outLine.clear();
    if (!dbConnection || !dbAvailable_) {
        return false;
    }

    Logger::instance().printf("[DB][Q] RANDOM %s\n", table.tableName.c_str());
    TryLockGuard lock(dbMutex_);
    if (!lock) {
        Logger::instance().println("[DB][Q] RANDOM: db busy");
        return false;
    }

    std::uint64_t minRowid = 0;
    std::uint64_t maxRowid = 0;
    if (!queryRowidRangeUnlocked(dbConnection, table.tableName, minRowid, maxRowid) || maxRowid == 0 ||
        maxRowid < minRowid) {
        (void)sqlite3_db_release_memory(dbConnection);
        return false;
    }

    const std::uint64_t span = (maxRowid - minRowid) + 1;
    if (span == 0) {
        (void)sqlite3_db_release_memory(dbConnection);
        return false;
    }

    auto boundedRand = [](std::uint64_t boundExclusive) -> std::uint64_t {
        if (boundExclusive == 0) {
            return 0;
        }

        auto rnd31 = []() -> std::uint64_t {
            // random(max) returns [0, max), with max limited to signed long range.
            return static_cast<std::uint64_t>(static_cast<std::uint32_t>(random(0x7fffffff)));
        };

        const std::uint64_t r = (rnd31() << 31) | rnd31();
        return r % boundExclusive;
    };

    constexpr std::size_t kTries = 8;
    for (std::size_t i = 0; i < kTries; ++i) {
        const std::uint64_t guess = minRowid + boundedRand(span);
        if (queryRowByRowidUnlocked(dbConnection, table.tableName, guess, outLine)) {
            (void)sqlite3_db_release_memory(dbConnection);
            return true;
        }
    }

    // Fall back to a deterministic query if the table has holes in rowids.
    const bool ok = queryLastRowUnlocked(dbConnection, table.tableName, outLine);
    (void)sqlite3_db_release_memory(dbConnection);
    return ok;
}

bool DatabaseManager::rowByRowid(const DBTable& table, std::uint64_t rowid, std::string& outLine) {
    outLine.clear();
    if (!dbConnection || !dbAvailable_) {
        return false;
    }

    Logger::instance().printf("[DB][Q] ROWID %s id=%llu\n", table.tableName.c_str(),
                              static_cast<unsigned long long>(rowid));
    TryLockGuard lock(dbMutex_);
    if (!lock) {
        Logger::instance().println("[DB][Q] ROWID: db busy");
        return false;
    }

    const bool ok = queryRowByRowidUnlocked(dbConnection, table.tableName, rowid, outLine);
    (void)sqlite3_db_release_memory(dbConnection);
    return ok;
}

bool DatabaseManager::rowidRange(const DBTable& table, std::uint64_t& outMinRowid, std::uint64_t& outMaxRowid) {
    outMinRowid = 0;
    outMaxRowid = 0;
    if (!dbConnection || !dbAvailable_) {
        return false;
    }

    Logger::instance().printf("[DB][Q] ROWID RANGE %s\n", table.tableName.c_str());
    TryLockGuard lock(dbMutex_);
    if (!lock) {
        Logger::instance().println("[DB][Q] ROWID RANGE: db busy");
        return false;
    }

    const bool ok = queryRowidRangeUnlocked(dbConnection, table.tableName, outMinRowid, outMaxRowid);
    (void)sqlite3_db_release_memory(dbConnection);
    if (ok) {
        Logger::instance().printf("[DB][Q] ROWID RANGE %s => %llu..%llu\n",
                                  table.tableName.c_str(),
                                  static_cast<unsigned long long>(outMinRowid),
                                  static_cast<unsigned long long>(outMaxRowid));
    }
    return ok;
}

std::vector<std::string> DatabaseManager::topSecrets(std::size_t limit) {
    std::vector<std::string> lines;
    if (!dbConnection || !dbAvailable_) {
        return lines;
    }

    constexpr std::size_t kMaxLimit = 10;
    limit = std::min<std::size_t>(limit, kMaxLimit);
    if (limit == 0) {
        return lines;
    }

    TryLockGuard lock(dbMutex_);
    if (!lock) {
        Logger::instance().println("[DB][Q] SECRETS: db busy");
        return lines;
    }

    // Keep the SECRETS query bounded: last N rows only.
    std::uint64_t minRowid = 0;
    std::uint64_t maxRowid = 0;
    if (!queryRowidRangeUnlocked(dbConnection, KeyboardConfig::Tables::Inputs.tableName, minRowid, maxRowid) ||
        maxRowid == 0) {
        (void)sqlite3_db_release_memory(dbConnection);
        return lines;
    }

    constexpr std::uint64_t kWindowRows = 2000;
    const std::uint64_t rawWindowMin = (maxRowid > kWindowRows) ? (maxRowid - kWindowRows) : minRowid;
    const std::uint64_t windowMinRowid = std::max(minRowid, rawWindowMin);

    // 1) Count rows in the window.
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(dbConnection, "SELECT COUNT(*) FROM Inputs WHERE rowid >= ?1;", -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        Logger::instance().printf("[DB] SECRETS count prepare failed: %s\n", sqlite3_errmsg(dbConnection));
        (void)sqlite3_db_release_memory(dbConnection);
        return lines;
    }

    (void)sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(windowMinRowid));

    std::uint32_t count = 0;
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        const auto v = sqlite3_column_int64(stmt, 0);
        if (v > 0) {
            if (v > static_cast<sqlite3_int64>(std::numeric_limits<std::uint32_t>::max())) {
                count = std::numeric_limits<std::uint32_t>::max();
            } else {
                count = static_cast<std::uint32_t>(v);
            }
        }
    }
    sqlite3_finalize(stmt);

    if (count == 0) {
        (void)sqlite3_db_release_memory(dbConnection);
        return lines;
    }

    // 2) Compute p90 thresholds for Entropy and Variance (best-effort, within the window).
    const int offset = static_cast<int>(((static_cast<std::uint64_t>(count) * 90U) / 100U));
    const int p90Index = std::max(0, offset - 1);

    auto selectP90 = [&](const char* col, double& outP90) -> bool {
        outP90 = 0.0;
        char sql[128]{};
        std::snprintf(sql,
                      sizeof(sql),
                      "SELECT %s FROM Inputs WHERE rowid >= ?1 ORDER BY %s LIMIT 1 OFFSET ?2;",
                      col,
                      col);
        sqlite3_stmt* s = nullptr;
        int r = sqlite3_prepare_v2(dbConnection, sql, -1, &s, nullptr);
        if (r != SQLITE_OK) {
            Logger::instance().printf("[DB] SECRETS p90 prepare failed (%s): %s\n", col, sqlite3_errmsg(dbConnection));
            return false;
        }
        (void)sqlite3_bind_int64(s, 1, static_cast<sqlite3_int64>(windowMinRowid));
        (void)sqlite3_bind_int(s, 2, p90Index);
        r = sqlite3_step(s);
        if (r == SQLITE_ROW) {
            outP90 = sqlite3_column_double(s, 0);
            sqlite3_finalize(s);
            return true;
        }
        sqlite3_finalize(s);
        return false;
    };

    double entropyP90 = 0.0;
    double varianceP90 = 0.0;
    (void)selectP90("Entropy", entropyP90);
    (void)selectP90("Variance", varianceP90);

    char hdr[150]{};
    std::snprintf(hdr,
                  sizeof(hdr),
                  "[RAK] SECRETS (last %llu rows) p90 entropy>=%.3f variance>=%.3f (rows=%u)",
                  static_cast<unsigned long long>(kWindowRows),
                  entropyP90,
                  varianceP90,
                  static_cast<unsigned>(count));
    lines.emplace_back(hdr);

    // 3) Group by Input and keep those above thresholds (within the window).
    rc = sqlite3_prepare_v2(
        dbConnection,
        "SELECT Input, COUNT(*) AS c, AVG(Entropy) AS e, AVG(Variance) AS v "
        "FROM Inputs "
        "WHERE rowid >= ?1 "
        "GROUP BY Input "
        "HAVING c >= ?2 AND e >= ?3 AND v >= ?4 "
        "ORDER BY c DESC LIMIT ?5;",
        -1,
        &stmt,
        nullptr);
    if (rc != SQLITE_OK) {
        Logger::instance().printf("[DB] SECRETS prepare failed: %s\n", sqlite3_errmsg(dbConnection));
        (void)sqlite3_db_release_memory(dbConnection);
        return lines;
    }

    constexpr int kMinCount = 3;
    (void)sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(windowMinRowid));
    (void)sqlite3_bind_int(stmt, 2, kMinCount);
    (void)sqlite3_bind_double(stmt, 3, entropyP90);
    (void)sqlite3_bind_double(stmt, 4, varianceP90);
    (void)sqlite3_bind_int(stmt, 5, static_cast<int>(limit));

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const auto* txt = sqlite3_column_text(stmt, 0);
        const int txtBytes = sqlite3_column_bytes(stmt, 0);
        const int copyLen = std::min<int>(std::max<int>(txtBytes, 0), 40);
        char word[44]{};
        if (txt && copyLen > 0) {
            std::memcpy(word, txt, static_cast<std::size_t>(copyLen));
            word[copyLen] = '\0';
        } else {
            word[0] = '\0';
        }

        const auto c = sqlite3_column_int64(stmt, 1);
        const double e = sqlite3_column_double(stmt, 2);
        const double v = sqlite3_column_double(stmt, 3);

        char buf[180]{};
        std::snprintf(buf, sizeof(buf), " - \"%s\" count=%lld avgEntropy=%.3f avgVar=%.3f",
                      word, static_cast<long long>(c), e, v);
        lines.emplace_back(buf);
    }

    sqlite3_finalize(stmt);
    (void)sqlite3_db_release_memory(dbConnection);
    return lines;
}
