/**
 * @file  MessageRepository.cpp
 * @brief 聊天记录表的实现。
 */

#include "server/core/MessageRepository.h"

#include "server/core/TimeUtil.h"

#include <algorithm>

namespace dp {
namespace {

/**
 * @brief 建表语句。
 *
 * 字段说明：
 *   - `seq`        服务端全局单调序号，与协议里的 seq 一致
 *   - `sender_id`  发送者的**连接** id（每次连接都不同，与协议 `from` 对齐）
 *   - `account_id` 发送者的**持久**用户 id（0 = 未匹配到用户库）
 *   - `sender_name` 昵称快照：用户改名后，历史仍显示当时的名字
 *   - `kind`       0 = 文本，1 = 语音
 *
 * `UNIQUE(room_id, seq)` 让重复写入变成静默忽略（`INSERT OR IGNORE`）。
 */
constexpr const char* kSchema = R"SQL(
CREATE TABLE IF NOT EXISTS messages (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    room_id     TEXT    NOT NULL,
    seq         INTEGER NOT NULL,
    sender_id   INTEGER NOT NULL,
    account_id  INTEGER NOT NULL DEFAULT 0,
    sender_name TEXT    NOT NULL,
    kind        INTEGER NOT NULL DEFAULT 0,
    text        TEXT    NOT NULL DEFAULT '',
    audio       BLOB,
    duration_ms INTEGER NOT NULL DEFAULT 0,
    created_at  INTEGER NOT NULL,
    UNIQUE(room_id, seq)
);
CREATE INDEX IF NOT EXISTS idx_messages_room_seq ON messages(room_id, seq);
CREATE INDEX IF NOT EXISTS idx_messages_account  ON messages(account_id);
CREATE INDEX IF NOT EXISTS idx_messages_created  ON messages(created_at);
)SQL";

}  // namespace

bool MessageRepository::open(const std::string& path, std::string* error) {
    return db_.open(path, error);
}

bool MessageRepository::ensureSchema(std::string* error) {
    return db_.exec(kSchema, error);
}

bool MessageRepository::append(const std::string& roomId, const HistoryEntry& entry,
                               int64_t accountId, std::string* error) {
    if (!db_.isOpen()) {
        if (error != nullptr) *error = "database not open";
        return false;
    }

    Statement stmt(db_, "INSERT OR IGNORE INTO messages "
                        "(room_id, seq, sender_id, account_id, sender_name, kind, "
                        " text, audio, duration_ms, created_at) "
                        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    if (!stmt.ok()) {
        if (error != nullptr) *error = stmt.error();
        return false;
    }

    stmt.bindText(1, roomId);
    stmt.bindInt64(2, static_cast<int64_t>(entry.seq));
    stmt.bindInt64(3, static_cast<int64_t>(entry.from));
    stmt.bindInt64(4, accountId);
    stmt.bindText(5, entry.name);
    stmt.bindInt64(6, entry.kind == HistoryKind::Voice ? 1 : 0);
    stmt.bindText(7, entry.text);
    stmt.bindBlob(8, entry.audio);
    stmt.bindInt64(9, static_cast<int64_t>(entry.durationMs));
    stmt.bindInt64(10, entry.ts);

    stmt.step();
    if (stmt.failed()) {
        if (error != nullptr) *error = stmt.error();
        return false;
    }
    return true;
}

std::vector<HistoryEntry> MessageRepository::loadRecent(const std::string& roomId, int limit) {
    std::vector<HistoryEntry> out;
    if (!db_.isOpen() || limit <= 0) return out;

    Statement stmt(db_, "SELECT seq, sender_id, sender_name, kind, text, audio, "
                        "duration_ms, created_at "
                        "FROM messages WHERE room_id = ? ORDER BY seq DESC LIMIT ?");
    if (!stmt.ok()) return out;

    stmt.bindText(1, roomId);
    stmt.bindInt64(2, limit);

    while (stmt.step()) {
        HistoryEntry entry;
        entry.seq = static_cast<uint64_t>(stmt.columnInt64(0));
        entry.from = static_cast<uint64_t>(stmt.columnInt64(1));
        entry.name = stmt.columnText(2);
        entry.kind = stmt.columnInt64(3) != 0 ? HistoryKind::Voice : HistoryKind::Text;
        entry.text = stmt.columnText(4);
        entry.audio = stmt.columnBlob(5);
        entry.durationMs = static_cast<uint32_t>(stmt.columnInt64(6));
        entry.ts = stmt.columnInt64(7);
        out.push_back(std::move(entry));
    }

    std::reverse(out.begin(), out.end());   // 查询是倒序，返回正序
    return out;
}

size_t MessageRepository::countExpired(int retentionDays) {
    if (!db_.isOpen() || retentionDays <= 0) return 0;

    const int64_t cutoff = static_cast<int64_t>(nowMs())
                           - static_cast<int64_t>(retentionDays) * 86400000LL;

    Statement stmt(db_, "SELECT COUNT(*) FROM messages WHERE created_at < ?");
    if (!stmt.ok()) return 0;
    stmt.bindInt64(1, cutoff);
    if (!stmt.step()) return 0;
    return static_cast<size_t>(stmt.columnInt64(0));
}

int MessageRepository::archiveAndPrune(int retentionDays, const std::string& archivePath,
                                       std::string* error) {
    if (!db_.isOpen() || retentionDays <= 0 || archivePath.empty()) return 0;

    const size_t expired = countExpired(retentionDays);
    if (expired == 0) return 0;

    const int64_t cutoff = static_cast<int64_t>(nowMs())
                           - static_cast<int64_t>(retentionDays) * 86400000LL;

    // ATTACH 不支持参数绑定，路径只能拼进 SQL —— 手工转义单引号。
    std::string escaped;
    escaped.reserve(archivePath.size());
    for (char c : archivePath) {
        if (c == '\'') escaped += "''";
        else escaped += c;
    }

    const std::string attach = "ATTACH DATABASE '" + escaped + "' AS arch";
    if (!db_.exec(attach, error)) return 0;

    // 归档库建表（结构必须与主库一致，才能用 SELECT * 复制）
    const std::string createArchive = R"SQL(
CREATE TABLE IF NOT EXISTS arch.messages (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    room_id     TEXT    NOT NULL,
    seq         INTEGER NOT NULL,
    sender_id   INTEGER NOT NULL,
    account_id  INTEGER NOT NULL DEFAULT 0,
    sender_name TEXT    NOT NULL,
    kind        INTEGER NOT NULL DEFAULT 0,
    text        TEXT    NOT NULL DEFAULT '',
    audio       BLOB,
    duration_ms INTEGER NOT NULL DEFAULT 0,
    created_at  INTEGER NOT NULL,
    UNIQUE(room_id, seq)
))SQL";
    if (!db_.exec(createArchive, error)) {
        db_.exec("DETACH DATABASE arch", nullptr);
        return 0;
    }

    // 复制到归档库：唯一键 + INSERT OR IGNORE 保证重复执行不产生副本
    int archived = 0;
    {
        Statement copy(db_, "INSERT OR IGNORE INTO arch.messages "
                            "(room_id, seq, sender_id, account_id, sender_name, kind, "
                            " text, audio, duration_ms, created_at) "
                            "SELECT room_id, seq, sender_id, account_id, sender_name, kind, "
                            "text, audio, duration_ms, created_at FROM main.messages "
                            "WHERE created_at < ?");
        if (copy.ok()) {
            copy.bindInt64(1, cutoff);
            copy.step();
            if (copy.failed()) {
                if (error != nullptr) *error = copy.error();
                db_.exec("DETACH DATABASE arch", nullptr);
                return 0;
            }
            archived = static_cast<int>(expired);
        }
    }

    // 复制成功后才删除主库记录
    {
        Statement del(db_, "DELETE FROM main.messages WHERE created_at < ?");
        if (!del.ok()) {
            if (error != nullptr) *error = del.error();
            db_.exec("DETACH DATABASE arch", nullptr);
            return 0;
        }
        del.bindInt64(1, cutoff);
        del.step();
        if (del.failed() && error != nullptr) *error = del.error();
    }

    db_.exec("DETACH DATABASE arch", nullptr);
    return archived;
}

size_t MessageRepository::count() {
    if (!db_.isOpen()) return 0;

    Statement stmt(db_, "SELECT COUNT(*) FROM messages");
    if (!stmt.ok() || !stmt.step()) return 0;
    return static_cast<size_t>(stmt.columnInt64(0));
}

}  // namespace dp
