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
 * 功能描述:  析构 CDatabase 对象，释放运行期持有的全部资源。
 * 作者:      JaTin
 * return:    无返回值（构造函数或析构函数）。
 */
CDatabase::~CDatabase() {
    close();
}

/*
 * 功能描述:  获取open；失败时返回 false。
 * 作者:      JaTin
 * param [in]   strPath          文件路径，类型 const std::string&。
 * param [in]   pError           错误信息，类型 std::string*。
 * return:    成功返回 true；参数非法或执行失败时返回 false。
 */
bool CDatabase::open(const std::string& strPath, std::string* pError) {
    close();

    const int nRc = sqlite3_open_v2(strPath.c_str(), &db_,
                                   SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE
                                       | SQLITE_OPEN_FULLMUTEX,
                                   nullptr);
    if (nRc != SQLITE_OK) {
        if (pError != nullptr) {
            *pError = db_ != nullptr ? sqlite3_errmsg(db_) : "sqlite3_open_v2 failed";
        }
        close();
        return false;
    }

    /* WAL：写入不阻塞读；busy_timeout 防止偶发的多进程访问直接失败。 */
    std::string strPragmaError;
    if (!exec("PRAGMA journal_mode=WAL;", &strPragmaError)
        || !exec("PRAGMA synchronous=NORMAL;", &strPragmaError)
        || !exec("PRAGMA foreign_keys=ON;", &strPragmaError)
        || !exec("PRAGMA busy_timeout=3000;", &strPragmaError)) {
        if (pError != nullptr) *pError = strPragmaError;
        close();
        return false;
    }
    return true;
}

/*
 * 功能描述:  关闭close。
 * 作者:      JaTin
 * return:    无返回值；处理结果通过成员状态或信号对外体现。
 */
void CDatabase::close() {
    if (db_ == nullptr) return;
    sqlite3_close(db_);
    db_ = nullptr;
}

/*
 * 功能描述:  执行exec；实现上通过 SQLite 完成持久化读写；失败时返回 false。
 * 作者:      JaTin
 * param [in]   strSql           输入数据，类型 const std::string&。
 * param [in]   pError           错误信息，类型 std::string*。
 * return:    成功返回 true；参数非法或执行失败时返回 false。
 */
bool CDatabase::exec(const std::string& strSql, std::string* pError) {
    if (db_ == nullptr) {
        if (pError != nullptr) *pError = "database not open";
        return false;
    }

    char* strMessage = nullptr;
    const int nRc = sqlite3_exec(db_, strSql.c_str(), nullptr, nullptr, &strMessage);
    if (nRc != SQLITE_OK) {
        if (pError != nullptr) {
            *pError = strMessage != nullptr ? strMessage : "sqlite3_exec failed";
        }
        if (strMessage != nullptr) sqlite3_free(strMessage);
        return false;
    }
    if (strMessage != nullptr) sqlite3_free(strMessage);
    return true;
}

/*
 * 功能描述:  获取last 错误。
 * 作者:      JaTin
 * return:    返回构造好的字符串；无内容时返回空字符串。
 */
std::string CDatabase::lastError() const {
    if (db_ == nullptr) return "database not open";
    return sqlite3_errmsg(db_);
}

/* ---------------------------------------------------------------- CStatement */

/*
 * 功能描述:  构造 CStatement 对象，完成成员初始化与依赖注入。
 * 作者:      JaTin
 * param [out]  stDb             数据库，类型 CDatabase&。
 * param [in]   strSql           输入数据，类型 const std::string&。
 * return:    无返回值（构造函数或析构函数）。
 */
CStatement::CStatement(CDatabase& stDb, const std::string& strSql) {
    if (stDb.handle() == nullptr) {
        m_strError = "database not open";
        return;
    }

    const int nRc = sqlite3_prepare_v2(stDb.handle(), strSql.c_str(),
                                      static_cast<int>(strSql.size()), &m_pStmt, nullptr);
    if (nRc != SQLITE_OK) {
        m_strError = sqlite3_errmsg(stDb.handle());
        m_pStmt = nullptr;
    }
}

/*
 * 功能描述:  析构 CStatement 对象，释放运行期持有的全部资源。
 * 作者:      JaTin
 * return:    无返回值（构造函数或析构函数）。
 */
CStatement::~CStatement() {
    if (m_pStmt != nullptr) sqlite3_finalize(m_pStmt);
}

/*
 * 功能描述:  绑定文本。
 * 作者:      JaTin
 * param [in]   nIndex           位置索引，类型 int。
 * param [in]   strValue         待设置的数值，类型 const std::string&。
 * return:    无返回值；处理结果通过成员状态或信号对外体现。
 */
void CStatement::bindText(int nIndex, const std::string& strValue) {
    if (m_pStmt == nullptr) return;
    /* SQLITE_TRANSIENT：让 sqlite 自己拷贝，调用方不必保证生命周期 */
    sqlite3_bind_text(m_pStmt, nIndex, strValue.c_str(), static_cast<int>(strValue.size()),
                      SQLITE_TRANSIENT);
}

/*
 * 功能描述:  绑定int64。
 * 作者:      JaTin
 * param [in]   nIndex           位置索引，类型 int。
 * param [in]   strValue         待设置的数值，类型 int64_t。
 * return:    无返回值；处理结果通过成员状态或信号对外体现。
 */
void CStatement::bindInt64(int nIndex, int64_t strValue) {
    if (m_pStmt == nullptr) return;
    sqlite3_bind_int64(m_pStmt, nIndex, strValue);
}

/*
 * 功能描述:  绑定blob。
 * 作者:      JaTin
 * param [in]   nIndex           位置索引，类型 int。
 * param [in]   strValue         待设置的数值，类型 const std::string&。
 * return:    无返回值；处理结果通过成员状态或信号对外体现。
 */
void CStatement::bindBlob(int nIndex, const std::string& strValue) {
    if (m_pStmt == nullptr) return;
    if (strValue.empty()) {
        sqlite3_bind_zeroblob(m_pStmt, nIndex, 0);
        return;
    }
    sqlite3_bind_blob(m_pStmt, nIndex, strValue.data(), static_cast<int>(strValue.size()),
                      SQLITE_TRANSIENT);
}

/*
 * 功能描述:  获取step；实现上通过 SQLite 完成持久化读写；失败时返回 false。
 * 作者:      JaTin
 * return:    成功返回 true；参数非法或执行失败时返回 false。
 */
bool CStatement::step() {
    if (m_pStmt == nullptr) {
        m_nFailed = true;
        return false;
    }

    const int nRc = sqlite3_step(m_pStmt);
    if (nRc == SQLITE_ROW) return true;
    if (nRc == SQLITE_DONE) return false;

    m_nFailed = true;
    m_strError = sqlite3_errstr(nRc);
    return false;
}

/*
 * 功能描述:  获取columnint64。
 * 作者:      JaTin
 * param [in]   nColumn          输入数据，类型 int。
 * return:    返回计数或长度；无有效数据时返回 0。
 */
int64_t CStatement::columnInt64(int nColumn) const {
    if (m_pStmt == nullptr) return 0;
    return sqlite3_column_int64(m_pStmt, nColumn);
}

/*
 * 功能描述:  获取column 文本。
 * 作者:      JaTin
 * param [in]   nColumn          输入数据，类型 int。
 * return:    返回构造好的字符串；无内容时返回空字符串。
 */
std::string CStatement::columnText(int nColumn) const {
    if (m_pStmt == nullptr) return std::string();
    const unsigned char* text = sqlite3_column_text(m_pStmt, nColumn);
    if (text == nullptr) return std::string();
    const int nBytes = sqlite3_column_bytes(m_pStmt, nColumn);
    return std::string(reinterpret_cast<const char*>(text), static_cast<size_t>(nBytes));
}

/*
 * 功能描述:  获取columnblob。
 * 作者:      JaTin
 * param [in]   nColumn          输入数据，类型 int。
 * return:    返回构造好的字符串；无内容时返回空字符串。
 */
std::string CStatement::columnBlob(int nColumn) const {
    if (m_pStmt == nullptr) return std::string();
    const void* vData = sqlite3_column_blob(m_pStmt, nColumn);
    if (vData == nullptr) return std::string();
    const int nBytes = sqlite3_column_bytes(m_pStmt, nColumn);
    return std::string(static_cast<const char*>(vData), static_cast<size_t>(nBytes));
}

/*
 * 功能描述:  判断columnnull。
 * 作者:      JaTin
 * param [in]   nColumn          输入数据，类型 int。
 * return:    成功返回 true；参数非法或执行失败时返回 false。
 */
bool CStatement::columnIsNull(int nColumn) const {
    if (m_pStmt == nullptr) return true;
    return sqlite3_column_type(m_pStmt, nColumn) == SQLITE_NULL;
}

/*
 * 功能描述:  重置reset。
 * 作者:      JaTin
 * return:    无返回值；处理结果通过成员状态或信号对外体现。
 */
void CStatement::reset() {
    if (m_pStmt == nullptr) return;
    sqlite3_reset(m_pStmt);
    m_nFailed = false;
    m_strError.clear();
}

/* namespace dp */
}
