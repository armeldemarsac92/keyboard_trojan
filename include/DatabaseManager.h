#pragma once

#include <cstddef>
#include <deque>
#include <functional>
#include <cstdint>
#include <string>
#include <vector>

#include <TeensyThreads.h>

#include "ArduinoSQLiteHandler.h"

namespace KeyboardConfig {
    struct NodeInfo;
}

class DatabaseManager {
private:
    enum class JobKind : std::uint8_t {
        QueryTableIntro,
        RandomRow,
        CountRows,
        RowByRowid,
        TopSecrets,
        TailInputs,
        ListRadioMasters,
    };

    struct PendingJob {
        JobKind kind = JobKind::RandomRow;
        std::uint32_t replyTo = 0;
        std::uint8_t channel = 0;
        const DBTable* table = nullptr;
        std::uint64_t rowid = 0;
        std::size_t limit = 0;
    };

    std::vector<std::string> pendingStatements_;
    sqlite3* dbConnection = nullptr;
    Threads::Mutex queueMutex;
    Threads::Mutex dbMutex_;
    Threads::Mutex jobsMutex_;
    std::deque<PendingJob> pendingJobs_;
    bool dbAvailable_ = false;

    DatabaseManager();

    void getData(const std::function<void(sqlite3_stmt*)>& callback, const DBTable& table);

    [[nodiscard]] bool enqueueJob_(PendingJob job);
    void processJobsOnce_();

    void sendReply_(std::uint32_t dest, std::uint8_t channel, std::string text);

public:
    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;

    using ReplyCallback = void (*)(std::uint32_t dest, std::uint8_t channel, std::string text);
    void setReplyCallback(ReplyCallback cb);

    static DatabaseManager& getInstance();
    void cleanupDuplicates();
    [[nodiscard]] std::vector<KeyboardConfig::NodeInfo> getRadioNodes();
    void saveData(std::vector<std::string> data, const DBTable& table);
    void processQueue();

    // Read helpers for the RAK command protocol (bounded to keep the system responsive).
    [[nodiscard]] bool countRows(const DBTable& table, std::uint32_t& outCount);
    [[nodiscard]] std::vector<std::string> tailInputs(std::size_t limit);

    // Generic helpers for interactive query sessions.
    [[nodiscard]] bool randomRow(const DBTable& table, std::string& outLine);
    [[nodiscard]] bool rowByRowid(const DBTable& table, std::uint64_t rowid, std::string& outLine);
    [[nodiscard]] bool rowidRange(const DBTable& table, std::uint64_t& outMinRowid, std::uint64_t& outMaxRowid);

    // "Secrets": words that appear often and have high entropy + variance (p90 thresholds).
    [[nodiscard]] std::vector<std::string> topSecrets(std::size_t limit);

    // Async DB jobs (processed on the DB writer thread via processQueue()).
    [[nodiscard]] bool enqueueQueryTableIntro(std::uint32_t replyTo, std::uint8_t channel, const DBTable& table);
    [[nodiscard]] bool enqueueRandomRow(std::uint32_t replyTo, std::uint8_t channel, const DBTable& table);
    [[nodiscard]] bool enqueueCountRows(std::uint32_t replyTo, std::uint8_t channel, const DBTable& table);
    [[nodiscard]] bool enqueueRowByRowid(std::uint32_t replyTo, std::uint8_t channel, const DBTable& table,
                                         std::uint64_t rowid);
    [[nodiscard]] bool enqueueTopSecrets(std::uint32_t replyTo, std::uint8_t channel, std::size_t limit);
    [[nodiscard]] bool enqueueTailInputs(std::uint32_t replyTo, std::uint8_t channel, std::size_t limit);
    [[nodiscard]] bool enqueueListRadioMasters(std::uint32_t replyTo, std::uint8_t channel);

private:
    ReplyCallback replyCb_ = nullptr;
};
