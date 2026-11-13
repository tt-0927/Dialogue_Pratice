/**
 * @file  Database.cpp
 * @brief SQLite 封装的实现。
 */

#include "server/core/Database.h"

#include "third_party/sqlite3/sqlite3.h"

namespace dp {

// ---------------------------------------------------------------- Database

Database::~Database() {
    close();
}

bool Database::open(const std::string& path, std::string* error) {
    close();

    const int rc = sqlite3_open_v2(path.c_str(), &db_,
                                   SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE
                                       | SQLITE_OPEN_FULLMUTEX,
                                   nullptr);
    if (rc != SQLITE_OK) {
        if (error != nullptr) {
            *error = db_ != nullptr ? sqlite3_errmsg(db_) : "sqlite3_open_v2 failed";
        }
        close();
        return false;
    }

    // WAL：写入不阻塞读；busy_timeout 防止偶发的多进程访问直接失败。
    std::string pragmaError;
    if (!exec("PRAGMA journal_mode=WAL;", &pragmaError)
        || !exec("PRAGMA synchronous=NORMAL;", &pragmaError)
        || !exec("PRAGMA foreign_keys=ON;", &pragmaError)
        || !exec("PRAGMA busy_timeout=3000;", &pragmaError)) {
        if (error != nullptr) *error = pragmaError;
        close();
        return false;
    }
    return true;
}

void Database::close() {
    if (db_ == nullptr) return;
    sqlite3_close(db_);
    db_ = nullptr;
}

bool Database::exec(const std::string& sql, std::string* error) {
    if (db_ == nullptr) {
        if (error != nullptr) *error = "database not open";
        return false;
    }

    char* message = nullptr;
    const int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &message);
    if (rc != SQLITE_OK) {
        if (error != nullptr) {
            *error = message != nullptr ? message : "sqlite3_exec failed";
        }
        if (message != nullptr) sqlite3_free(message);
        return false;
    }
    if (message != nullptr) sqlite3_free(message);
    return true;
}

std::string Database::lastError() const {
    if (db_ == nullptr) return "database not open";
    return sqlite3_errmsg(db_);
}

// ---------------------------------------------------------------- Statement

Statement::Statement(Database& db, const std::string& sql) {
    if (db.handle() == nullptr) {
        error_ = "database not open";
        return;
    }

    const int rc = sqlite3_prepare_v2(db.handle(), sql.c_str(),
                                      static_cast<int>(sql.size()), &stmt_, nullptr);
    if (rc != SQLITE_OK) {
        error_ = sqlite3_errmsg(db.handle());
        stmt_ = nullptr;
    }
}

Statement::~Statement() {
    if (stmt_ != nullptr) sqlite3_finalize(stmt_);
}

void Statement::bindText(int index, const std::string& value) {
    if (stmt_ == nullptr) return;
    // SQLITE_TRANSIENT：让 sqlite 自己拷贝，调用方不必保证生命周期
    sqlite3_bind_text(stmt_, index, value.c_str(), static_cast<int>(value.size()),
                      SQLITE_TRANSIENT);
}

void Statement::bindInt64(int index, int64_t value) {
    if (stmt_ == nullptr) return;
    sqlite3_bind_int64(stmt_, index, value);
}

void Statement::bindBlob(int index, const std::string& value) {
    if (stmt_ == nullptr) return;
    if (value.empty()) {
        sqlite3_bind_zeroblob(stmt_, index, 0);
        return;
    }
    sqlite3_bind_blob(stmt_, index, value.data(), static_cast<int>(value.size()),
                      SQLITE_TRANSIENT);
}

bool Statement::step() {
    if (stmt_ == nullptr) {
        failed_ = true;
        return false;
    }

    const int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW) return true;
    if (rc == SQLITE_DONE) return false;

    failed_ = true;
    error_ = sqlite3_errstr(rc);
    return false;
}

int64_t Statement::columnInt64(int column) const {
    if (stmt_ == nullptr) return 0;
    return sqlite3_column_int64(stmt_, column);
}

std::string Statement::columnText(int column) const {
    if (stmt_ == nullptr) return std::string();
    const unsigned char* text = sqlite3_column_text(stmt_, column);
    if (text == nullptr) return std::string();
    const int bytes = sqlite3_column_bytes(stmt_, column);
    return std::string(reinterpret_cast<const char*>(text), static_cast<size_t>(bytes));
}

std::string Statement::columnBlob(int column) const {
    if (stmt_ == nullptr) return std::string();
    const void* data = sqlite3_column_blob(stmt_, column);
    if (data == nullptr) return std::string();
    const int bytes = sqlite3_column_bytes(stmt_, column);
    return std::string(static_cast<const char*>(data), static_cast<size_t>(bytes));
}

bool Statement::columnIsNull(int column) const {
    if (stmt_ == nullptr) return true;
    return sqlite3_column_type(stmt_, column) == SQLITE_NULL;
}

void Statement::reset() {
    if (stmt_ == nullptr) return;
    sqlite3_reset(stmt_);
    failed_ = false;
    error_.clear();
}

}  // namespace dp
