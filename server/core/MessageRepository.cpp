/*
 * 文件名:    MessageRepository.cpp
 * 创建日期:  2026-11-13
 * 作者:      JaTin
 * 功能描述:  聊天记录表的实现。
 * 修改记录:  2026-11-13 JaTin 按 C/C++ 编码规范整改注释与命名。
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

/* namespace */
}

/*
 * 功能描述:  执行 open 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [in]  path  参数。
 * param [in]  error  参数。
 * return:    返回值，含义见功能描述。
 */
bool CMessageRepository::open(const std::string& path, std::string* error) {
    return db_.open(path, error);
}

/*
 * 功能描述:  执行 ensureSchema 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [in]  error  参数。
 * return:    返回值，含义见功能描述。
 */
bool CMessageRepository::ensureSchema(std::string* error) {
    return db_.exec(kSchema, error);
}

bool CMessageRepository::append(const std::string& roomId, const HistoryEntry_S& entry,
                               int64_t accountId, std::string* error) {
    if (!db_.isOpen()) {
        if (error != nullptr) *error = "database not open";
        return false;
    }

    CStatement stmt(db_, "INSERT OR IGNORE INTO messages "
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
    stmt.bindInt64(6, entry.kind == HistoryKind_EN::Voice ? 1 : 0);
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

/*
 * 功能描述:  执行 loadRecent 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [in]  roomId  参数。
 * param [in]  limit  参数。
 * return:    返回值，含义见功能描述。
 */
std::vector<HistoryEntry_S> CMessageRepository::loadRecent(const std::string& roomId, int limit) {
    std::vector<HistoryEntry_S> out;
    if (!db_.isOpen() || limit <= 0) return out;

    CStatement stmt(db_, "SELECT seq, sender_id, sender_name, kind, text, audio, "
                        "duration_ms, created_at "
                        "FROM messages WHERE room_id = ? ORDER BY seq DESC LIMIT ?");
    if (!stmt.ok()) return out;

    stmt.bindText(1, roomId);
    stmt.bindInt64(2, limit);

    while (stmt.step()) {
        HistoryEntry_S entry;
        entry.seq = static_cast<uint64_t>(stmt.columnInt64(0));
        entry.from = static_cast<uint64_t>(stmt.columnInt64(1));
        entry.name = stmt.columnText(2);
        entry.kind = stmt.columnInt64(3) != 0 ? HistoryKind_EN::Voice : HistoryKind_EN::Text;
        entry.text = stmt.columnText(4);
        entry.audio = stmt.columnBlob(5);
        entry.durationMs = static_cast<uint32_t>(stmt.columnInt64(6));
        entry.ts = stmt.columnInt64(7);
        out.push_back(std::move(entry));
    }

    /* 查询是倒序，返回正序 */
    std::reverse(out.begin(), out.end());
    return out;
}

/*
 * 功能描述:  执行 countExpired 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [in]  retentionDays  参数。
 * return:    返回值，含义见功能描述。
 */
size_t CMessageRepository::countExpired(int retentionDays) {
    if (!db_.isOpen() || retentionDays <= 0) return 0;

    const int64_t cutoff = static_cast<int64_t>(nowMs())
                           - static_cast<int64_t>(retentionDays) * 86400000LL;

    CStatement stmt(db_, "SELECT COUNT(*) FROM messages WHERE created_at < ?");
    if (!stmt.ok()) return 0;
    stmt.bindInt64(1, cutoff);
    if (!stmt.step()) return 0;
    return static_cast<size_t>(stmt.columnInt64(0));
}

int CMessageRepository::archiveAndPrune(int retentionDays, const std::string& archivePath,
                                       std::string* error) {
    if (!db_.isOpen() || retentionDays <= 0 || archivePath.empty()) return 0;

    const size_t expired = countExpired(retentionDays);
    if (expired == 0) return 0;

    const int64_t cutoff = static_cast<int64_t>(nowMs())
                           - static_cast<int64_t>(retentionDays) * 86400000LL;

    /* ATTACH 不支持参数绑定，路径只能拼进 SQL —— 手工转义单引号。 */
    std::string escaped;
    escaped.reserve(archivePath.size());
    for (char c : archivePath) {
        if (c == '\'') escaped += "''";
        else escaped += c;
    }

    const std::string attach = "ATTACH DATABASE '" + escaped + "' AS arch";
    if (!db_.exec(attach, error)) return 0;

    /* 归档库建表（结构必须与主库一致，才能用 SELECT * 复制） */
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

    /* 复制到归档库：唯一键 + INSERT OR IGNORE 保证重复执行不产生副本 */
    int archived = 0;
    {
        CStatement copy(db_, "INSERT OR IGNORE INTO arch.messages "
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

    /* 复制成功后才删除主库记录 */
    {
        CStatement del(db_, "DELETE FROM main.messages WHERE created_at < ?");
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

/*
 * 功能描述:  执行 count 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * return:    返回值，含义见功能描述。
 */
size_t CMessageRepository::count() {
    if (!db_.isOpen()) return 0;

    CStatement stmt(db_, "SELECT COUNT(*) FROM messages");
    if (!stmt.ok() || !stmt.step()) return 0;
    return static_cast<size_t>(stmt.columnInt64(0));
}

/* namespace dp */
}
