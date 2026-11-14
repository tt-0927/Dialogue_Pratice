/**
 * @file       MessageRepository.cpp
 * @brief      聊天记录表的实现。
 * @author     JaTin
 * @date       2026-11-13
 * @note       修改记录：2026-11-14 JaTin 统一为 Doxygen 风格注释。
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

/**
 * @brief  namespace。
 */
}

/**
 * @brief      获取 open相关的数据并返回处理结果。
 * @param[in] strPath        文件路径；类型 const std::string&。
 * @param[in] pError         错误信息；类型 std::string*。
 * @return     成功返回 true；参数非法或执行失败时返回 false。
 * @author     JaTin
 */
bool CMessageRepository::open(const std::string& strPath, std::string* pError) {
    return db_.open(strPath, pError);
}

/**
 * @brief      确保 schema。
 * @param[in] pError         错误信息；类型 std::string*。
 * @return     成功返回 true；参数非法或执行失败时返回 false。
 * @author     JaTin
 */
bool CMessageRepository::ensureSchema(std::string* pError) {
    return db_.exec(kSchema, pError);
}

/**
 * @brief      追加 append；失败时返回 false。
 * @param[in] strRoomId      输入数据；类型 const std::string&。
 * @param[in] stEntry        条目；类型 const HistoryEntry_S&。
 * @param[in] lAccountId     账号编号；类型 int64_t。
 * @param[in] pError         错误信息；类型 std::string*。
 * @return     成功返回 true；参数非法或执行失败时返回 false。
 * @author     JaTin
 */
bool CMessageRepository::append(const std::string& strRoomId, const HistoryEntry_S& stEntry,
                               int64_t lAccountId, std::string* pError) {
    if (!db_.isOpen()) {
        if (pError != nullptr) *pError = "database not open";
        return false;
    }

    CStatement stStmt(db_, "INSERT OR IGNORE INTO messages "
                        "(room_id, seq, sender_id, account_id, sender_name, kind, "
                        " text, audio, duration_ms, created_at) "
                        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    if (!stStmt.ok()) {
        if (pError != nullptr) *pError = stStmt.error();
        return false;
    }

    stStmt.bindText(1, strRoomId);
    stStmt.bindInt64(2, static_cast<int64_t>(stEntry.seq));
    stStmt.bindInt64(3, static_cast<int64_t>(stEntry.from));
    stStmt.bindInt64(4, lAccountId);
    stStmt.bindText(5, stEntry.name);
    stStmt.bindInt64(6, stEntry.kind == HistoryKind_EN::Voice ? 1 : 0);
    stStmt.bindText(7, stEntry.text);
    stStmt.bindBlob(8, stEntry.audio);
    stStmt.bindInt64(9, static_cast<int64_t>(stEntry.durationMs));
    stStmt.bindInt64(10, stEntry.ts);

    stStmt.step();
    if (stStmt.failed()) {
        if (pError != nullptr) *pError = stStmt.error();
        return false;
    }
    return true;
}

/**
 * @brief      加载 recent相关的数据并返回处理结果。
 * @param[in] strRoomId      输入数据；类型 const std::string&。
 * @param[in] nLimit         输入数据；类型 int。
 * @return     返回 vector<HistoryEntry_S> CMessageRepository:: 类型的处理结果。
 * @author     JaTin
 */
std::vector<HistoryEntry_S> CMessageRepository::loadRecent(const std::string& strRoomId, int nLimit) {
    std::vector<HistoryEntry_S> aOut;
    if (!db_.isOpen() || nLimit <= 0) return aOut;

    CStatement stStmt(db_, "SELECT seq, sender_id, sender_name, kind, text, audio, "
                        "duration_ms, created_at "
                        "FROM messages WHERE room_id = ? ORDER BY seq DESC LIMIT ?");
    if (!stStmt.ok()) return aOut;

    stStmt.bindText(1, strRoomId);
    stStmt.bindInt64(2, nLimit);

    while (stStmt.step()) {
        HistoryEntry_S stEntry;
        stEntry.seq = static_cast<uint64_t>(stStmt.columnInt64(0));
        stEntry.from = static_cast<uint64_t>(stStmt.columnInt64(1));
        stEntry.name = stStmt.columnText(2);
        stEntry.kind = stStmt.columnInt64(3) != 0 ? HistoryKind_EN::Voice : HistoryKind_EN::Text;
        stEntry.text = stStmt.columnText(4);
        stEntry.audio = stStmt.columnBlob(5);
        stEntry.durationMs = static_cast<uint32_t>(stStmt.columnInt64(6));
        stEntry.ts = stStmt.columnInt64(7);
        aOut.push_back(std::move(stEntry));
    }

    /**
     * @brief  查询是倒序，返回正序。
     */
    std::reverse(aOut.begin(), aOut.end());
    return aOut;
}

/**
 * @brief      统计 expired。
 * @param[in] nRetentionDays 输入数据；类型 int。
 * @return     返回计数或长度；无有效数据时返回 0。
 * @author     JaTin
 */
size_t CMessageRepository::countExpired(int nRetentionDays) {
    if (!db_.isOpen() || nRetentionDays <= 0) return 0;

    const int64_t lCutoff = static_cast<int64_t>(nowMs())
                           - static_cast<int64_t>(nRetentionDays) * 86400000LL;

    CStatement stStmt(db_, "SELECT COUNT(*) FROM messages WHERE created_at < ?");
    if (!stStmt.ok()) return 0;
    stStmt.bindInt64(1, lCutoff);
    if (!stStmt.step()) return 0;
    return static_cast<size_t>(stStmt.columnInt64(0));
}

/**
 * @brief      归档 andprune。
 * @param[in] nRetentionDays 输入数据；类型 int。
 * @param[in] strArchivePath 输入数据；类型 const std::string&。
 * @param[in] pError         错误信息；类型 std::string*。
 * @return     返回处理结果码；0 表示成功，负值或 -1 表示失败。
 * @author     JaTin
 */
int CMessageRepository::archiveAndPrune(int nRetentionDays, const std::string& strArchivePath,
                                       std::string* pError) {
    if (!db_.isOpen() || nRetentionDays <= 0 || strArchivePath.empty()) return 0;

    const size_t uExpired = countExpired(nRetentionDays);
    if (uExpired == 0) return 0;

    const int64_t lCutoff = static_cast<int64_t>(nowMs())
                           - static_cast<int64_t>(nRetentionDays) * 86400000LL;

    /**
     * @brief  ATTACH 不支持参数绑定，路径只能拼进 SQL —— 手工转义单引号。
     */
    std::string strEscaped;
    strEscaped.reserve(strArchivePath.size());
    for (char chC : strArchivePath) {
        if (chC == '\'') strEscaped += "''";
        else strEscaped += chC;
    }

    const std::string strAttach = "ATTACH DATABASE '" + strEscaped + "' AS arch";
    if (!db_.exec(strAttach, pError)) return 0;

    /**
     * @brief  归档库建表（结构必须与主库一致，才能用 SELECT * 复制）。
     */
    const std::string strCreateArchive = R"SQL(
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
    if (!db_.exec(strCreateArchive, pError)) {
        db_.exec("DETACH DATABASE arch", nullptr);
        return 0;
    }

    /**
     * @brief  复制到归档库：唯一键 + INSERT OR IGNORE 保证重复执行不产生副本。
     */
    int nArchived = 0;
    {
        CStatement stCopy(db_, "INSERT OR IGNORE INTO arch.messages "
                            "(room_id, seq, sender_id, account_id, sender_name, kind, "
                            " text, audio, duration_ms, created_at) "
                            "SELECT room_id, seq, sender_id, account_id, sender_name, kind, "
                            "text, audio, duration_ms, created_at FROM main.messages "
                            "WHERE created_at < ?");
        if (stCopy.ok()) {
            stCopy.bindInt64(1, lCutoff);
            stCopy.step();
            if (stCopy.failed()) {
                if (pError != nullptr) *pError = stCopy.error();
                db_.exec("DETACH DATABASE arch", nullptr);
                return 0;
            }
            nArchived = static_cast<int>(uExpired);
        }
    }

    /**
     * @brief  复制成功后才删除主库记录。
     */
    {
        CStatement stDel(db_, "DELETE FROM main.messages WHERE created_at < ?");
        if (!stDel.ok()) {
            if (pError != nullptr) *pError = stDel.error();
            db_.exec("DETACH DATABASE arch", nullptr);
            return 0;
        }
        stDel.bindInt64(1, lCutoff);
        stDel.step();
        if (stDel.failed() && pError != nullptr) *pError = stDel.error();
    }

    db_.exec("DETACH DATABASE arch", nullptr);
    return nArchived;
}

/**
 * @brief      统计 count。
 * @note       本函数无输入参数。
 * @return     返回计数或长度；无有效数据时返回 0。
 * @author     JaTin
 */
size_t CMessageRepository::count() {
    if (!db_.isOpen()) return 0;

    CStatement stStmt(db_, "SELECT COUNT(*) FROM messages");
    if (!stStmt.ok() || !stStmt.step()) return 0;
    return static_cast<size_t>(stStmt.columnInt64(0));
}

/**
 * @brief  namespace dp。
 */
}
