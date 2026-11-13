/**
 * @file  UserRepository.cpp
 * @brief 用户表访问的实现。
 */

#include "server/core/UserRepository.h"

#include "server/core/Crypto.h"
#include "server/core/TimeUtil.h"

namespace dp {
namespace {

/// @brief 建表语句：字段含义见 README 的「用户库」一节。
constexpr const char* kSchema = R"SQL(
CREATE TABLE IF NOT EXISTS users (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    username      TEXT    NOT NULL UNIQUE,
    password_hash TEXT    NOT NULL,
    salt          TEXT    NOT NULL,
    nickname      TEXT    NOT NULL,
    age           INTEGER NOT NULL DEFAULT 0,
    description   TEXT    NOT NULL DEFAULT '',
    enabled       INTEGER NOT NULL DEFAULT 1,
    created_at    INTEGER NOT NULL,
    last_login_at INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX IF NOT EXISTS idx_users_nickname ON users(nickname);
)SQL";

/// @brief 查询列顺序必须与 readRow 一致。
constexpr const char* kSelectColumns =
    "id, username, nickname, age, description, enabled";

}  // namespace

bool UserRepository::open(const std::string& path, std::string* error) {
    return db_.open(path, error);
}

bool UserRepository::ensureSchema(std::string* error) {
    return db_.exec(kSchema, error);
}

bool UserRepository::readRow(Statement& stmt, UserRecord* out) {
    if (out == nullptr) return false;
    out->id = stmt.columnInt64(0);
    out->username = stmt.columnText(1);
    out->nickname = stmt.columnText(2);
    out->age = static_cast<int>(stmt.columnInt64(3));
    out->description = stmt.columnText(4);
    out->enabled = stmt.columnInt64(5) != 0;
    return true;
}

std::optional<UserRecord> UserRepository::findByNickname(const std::string& nickname) {
    if (!db_.isOpen()) return std::nullopt;

    // COLLATE NOCASE：用户填昵称时大小写随意，不该因此匹配不到账号。
    // 注意 SQLite 的 NOCASE 只对 ASCII 生效，中文昵称仍是精确匹配。
    Statement stmt(db_, std::string("SELECT ") + kSelectColumns
                            + " FROM users WHERE nickname = ? COLLATE NOCASE"
                              " AND enabled = 1 LIMIT 1");
    if (!stmt.ok()) return std::nullopt;

    stmt.bindText(1, nickname);
    if (!stmt.step()) return std::nullopt;

    UserRecord record;
    if (!readRow(stmt, &record)) return std::nullopt;
    return record;
}

std::optional<UserRecord> UserRepository::findByUsername(const std::string& username) {
    if (!db_.isOpen()) return std::nullopt;

    Statement stmt(db_, std::string("SELECT ") + kSelectColumns
                            + " FROM users WHERE username = ? LIMIT 1");
    if (!stmt.ok()) return std::nullopt;

    stmt.bindText(1, username);
    if (!stmt.step()) return std::nullopt;

    UserRecord record;
    if (!readRow(stmt, &record)) return std::nullopt;
    return record;
}

bool UserRepository::verifyCredentials(const std::string& username,
                                       const std::string& password,
                                       UserRecord* out) {
    if (!db_.isOpen()) return false;

    Statement stmt(db_, "SELECT id, username, nickname, age, description, enabled, "
                        "password_hash, salt FROM users WHERE username = ? LIMIT 1");
    if (!stmt.ok()) return false;

    stmt.bindText(1, username);
    if (!stmt.step()) return false;   // 账号不存在

    UserRecord record;
    readRow(stmt, &record);
    if (!record.enabled) return false;

    const std::string expectedHash = stmt.columnText(6);
    const std::string salt = stmt.columnText(7);
    if (!verifyPassword(salt, password, expectedHash)) return false;

    if (out != nullptr) *out = record;
    return true;
}

bool UserRepository::createUser(const std::string& username, const std::string& password,
                                const std::string& nickname, int age,
                                const std::string& description, std::string* error) {
    if (!db_.isOpen()) {
        if (error != nullptr) *error = "database not open";
        return false;
    }

    const std::string salt = randomSalt();
    const std::string hash = hashPassword(salt, password);

    Statement stmt(db_, "INSERT INTO users "
                        "(username, password_hash, salt, nickname, age, description, "
                        " enabled, created_at) VALUES (?, ?, ?, ?, ?, ?, 1, ?)");
    if (!stmt.ok()) {
        if (error != nullptr) *error = stmt.error();
        return false;
    }

    stmt.bindText(1, username);
    stmt.bindText(2, hash);
    stmt.bindText(3, salt);
    stmt.bindText(4, nickname);
    stmt.bindInt64(5, age);
    stmt.bindText(6, description);
    stmt.bindInt64(7, static_cast<int64_t>(nowMs()));

    stmt.step();
    if (stmt.failed()) {
        if (error != nullptr) *error = stmt.error();
        return false;
    }
    return true;
}

bool UserRepository::touchLogin(int64_t userId) {
    if (!db_.isOpen()) return false;

    Statement stmt(db_, "UPDATE users SET last_login_at = ? WHERE id = ?");
    if (!stmt.ok()) return false;

    stmt.bindInt64(1, static_cast<int64_t>(nowMs()));
    stmt.bindInt64(2, userId);
    stmt.step();
    return !stmt.failed();
}

size_t UserRepository::count() {
    if (!db_.isOpen()) return 0;

    Statement stmt(db_, "SELECT COUNT(*) FROM users");
    if (!stmt.ok() || !stmt.step()) return 0;
    return static_cast<size_t>(stmt.columnInt64(0));
}

}  // namespace dp
