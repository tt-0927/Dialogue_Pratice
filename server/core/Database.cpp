/*
 * 文件名:    Database.cpp
 * 创建日期:  2026-11-13
 * 作者:      JaTin
 * 功能描述:  SQLite 封装的实现。
 * 修改记录:  2026-11-13 JaTin 按 C/C++ 编码规范整改注释与命名。
 */

#include "server/core/Database.h"

#include "third_party/sqlite3/sqlite3.h"

namespace dp {

/* ---------------------------------------------------------------- CDatabase */

/*
 * 功能描述:  执行 CDatabase 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * return:    返回值，含义见功能描述。
 */
CDatabase::~CDatabase() {
    close();
}

/*
 * 功能描述:  执行 open 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [in]  path  参数。
 * param [in]  error  参数。
 * return:    返回值，含义见功能描述。
 */
bool CDatabase::open(const std::string& path, std::string* error) {
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

    /* WAL：写入不阻塞读；busy_timeout 防止偶发的多进程访问直接失败。 */
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

/*
 * 功能描述:  执行 close 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * return:    void
 */
void CDatabase::close() {
    if (db_ == nullptr) return;
    sqlite3_close(db_);
    db_ = nullptr;
}

/*
 * 功能描述:  执行 exec 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [in]  sql  参数。
 * param [in]  error  参数。
 * return:    返回值，含义见功能描述。
 */
bool CDatabase::exec(const std::string& sql, std::string* error) {
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

/*
 * 功能描述:  执行 lastError 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * return:    返回值，含义见功能描述。
 */
std::string CDatabase::lastError() const {
    if (db_ == nullptr) return "database not open";
    return sqlite3_errmsg(db_);
}

/* ---------------------------------------------------------------- CStatement */

/*
 * 功能描述:  执行 CStatement 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [out]  db  参数。
 * param [in]  sql  参数。
 * return:    返回值，含义见功能描述。
 */
CStatement::CStatement(CDatabase& db, const std::string& sql) {
    if (db.handle() == nullptr) {
        m_strError = "database not open";
        return;
    }

    const int rc = sqlite3_prepare_v2(db.handle(), sql.c_str(),
                                      static_cast<int>(sql.size()), &m_pStmt, nullptr);
    if (rc != SQLITE_OK) {
        m_strError = sqlite3_errmsg(db.handle());
        m_pStmt = nullptr;
    }
}

/*
 * 功能描述:  执行 CStatement 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * return:    返回值，含义见功能描述。
 */
CStatement::~CStatement() {
    if (m_pStmt != nullptr) sqlite3_finalize(m_pStmt);
}

/*
 * 功能描述:  执行 bindText 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [in]  index  参数。
 * param [in]  value  参数。
 * return:    void
 */
void CStatement::bindText(int index, const std::string& value) {
    if (m_pStmt == nullptr) return;
    /* SQLITE_TRANSIENT：让 sqlite 自己拷贝，调用方不必保证生命周期 */
    sqlite3_bind_text(m_pStmt, index, value.c_str(), static_cast<int>(value.size()),
                      SQLITE_TRANSIENT);
}

/*
 * 功能描述:  执行 bindInt64 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [in]  index  参数。
 * param [in]  value  参数。
 * return:    void
 */
void CStatement::bindInt64(int index, int64_t value) {
    if (m_pStmt == nullptr) return;
    sqlite3_bind_int64(m_pStmt, index, value);
}

/*
 * 功能描述:  执行 bindBlob 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [in]  index  参数。
 * param [in]  value  参数。
 * return:    void
 */
void CStatement::bindBlob(int index, const std::string& value) {
    if (m_pStmt == nullptr) return;
    if (value.empty()) {
        sqlite3_bind_zeroblob(m_pStmt, index, 0);
        return;
    }
    sqlite3_bind_blob(m_pStmt, index, value.data(), static_cast<int>(value.size()),
                      SQLITE_TRANSIENT);
}

/*
 * 功能描述:  执行 step 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * return:    返回值，含义见功能描述。
 */
bool CStatement::step() {
    if (m_pStmt == nullptr) {
        m_nFailed = true;
        return false;
    }

    const int rc = sqlite3_step(m_pStmt);
    if (rc == SQLITE_ROW) return true;
    if (rc == SQLITE_DONE) return false;

    m_nFailed = true;
    m_strError = sqlite3_errstr(rc);
    return false;
}

/*
 * 功能描述:  执行 columnInt64 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [in]  column  参数。
 * return:    返回值，含义见功能描述。
 */
int64_t CStatement::columnInt64(int column) const {
    if (m_pStmt == nullptr) return 0;
    return sqlite3_column_int64(m_pStmt, column);
}

/*
 * 功能描述:  执行 columnText 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [in]  column  参数。
 * return:    返回值，含义见功能描述。
 */
std::string CStatement::columnText(int column) const {
    if (m_pStmt == nullptr) return std::string();
    const unsigned char* text = sqlite3_column_text(m_pStmt, column);
    if (text == nullptr) return std::string();
    const int bytes = sqlite3_column_bytes(m_pStmt, column);
    return std::string(reinterpret_cast<const char*>(text), static_cast<size_t>(bytes));
}

/*
 * 功能描述:  执行 columnBlob 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [in]  column  参数。
 * return:    返回值，含义见功能描述。
 */
std::string CStatement::columnBlob(int column) const {
    if (m_pStmt == nullptr) return std::string();
    const void* data = sqlite3_column_blob(m_pStmt, column);
    if (data == nullptr) return std::string();
    const int bytes = sqlite3_column_bytes(m_pStmt, column);
    return std::string(static_cast<const char*>(data), static_cast<size_t>(bytes));
}

/*
 * 功能描述:  执行 columnIsNull 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [in]  column  参数。
 * return:    返回值，含义见功能描述。
 */
bool CStatement::columnIsNull(int column) const {
    if (m_pStmt == nullptr) return true;
    return sqlite3_column_type(m_pStmt, column) == SQLITE_NULL;
}

/*
 * 功能描述:  执行 reset 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * return:    void
 */
void CStatement::reset() {
    if (m_pStmt == nullptr) return;
    sqlite3_reset(m_pStmt);
    m_nFailed = false;
    m_strError.clear();
}

/* namespace dp */
}
