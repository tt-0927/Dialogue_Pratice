/*
 * 文件名:    UserRepository.cpp
 * 创建日期:  2026-11-13
 * 作者:      JaTin
 * 功能描述:  用户表访问的实现。
 * 修改记录:  2026-11-13 JaTin 按 C/C++ 编码规范整改注释与命名。
 */

#include "server/core/UserRepository.h"

#include "server/core/Crypto.h"
#include "server/core/TimeUtil.h"

namespace dp {
namespace {

/* 建表语句：字段含义见 README 的「用户库」一节。 */
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

/* 查询列顺序必须与 readRow 一致。 */
constexpr const char* kSelectColumns =
    "id, username, nickname, age, description, enabled";

/* namespace */
}

/*
 * 功能描述:  获取open。
 * 作者:      JaTin
 * param [in]   strPath          文件路径，类型 const std::string&。
 * param [in]   pError           错误信息，类型 std::string*。
 * return:    成功返回 true；参数非法或执行失败时返回 false。
 */
bool CUserRepository::open(const std::string& strPath, std::string* pError) {
    return db_.open(strPath, pError);
}

/*
 * 功能描述:  确保schema。
 * 作者:      JaTin
 * param [in]   pError           错误信息，类型 std::string*。
 * return:    成功返回 true；参数非法或执行失败时返回 false。
 */
bool CUserRepository::ensureSchema(std::string* pError) {
    return db_.exec(kSchema, pError);
}

/*
 * 功能描述:  读取row；失败时返回 false。
 * 作者:      JaTin
 * param [out]  stStmt           输入数据，类型 CStatement&。
 * param [in]   pOut             输入数据，类型 UserRecord_S*。
 * return:    成功返回 true；参数非法或执行失败时返回 false。
 */
bool CUserRepository::readRow(CStatement& stStmt, UserRecord_S* pOut) {
    if (pOut == nullptr) return false;
    pOut->id = stStmt.columnInt64(0);
    pOut->username = stStmt.columnText(1);
    pOut->nickname = stStmt.columnText(2);
    pOut->age = static_cast<int>(stStmt.columnInt64(3));
    pOut->description = stStmt.columnText(4);
    pOut->enabled = stStmt.columnInt64(5) != 0;
    return true;
}

/*
 * 功能描述:  查找by 昵称。
 * 作者:      JaTin
 * param [in]   strNickname      用户昵称，类型 const std::string&。
 * return:    返回 std::optional<UserRecord_S> 类型的处理结果，具体含义见功能描述。
 */
std::optional<UserRecord_S> CUserRepository::findByNickname(const std::string& strNickname) {
    if (!db_.isOpen()) return std::nullopt;

    /* COLLATE NOCASE：用户填昵称时大小写随意，不该因此匹配不到账号。 */
    /* 注意 SQLite 的 NOCASE 只对 ASCII 生效，中文昵称仍是精确匹配。 */
    CStatement stStmt(db_, std::string("SELECT ") + kSelectColumns
                            + " FROM users WHERE nickname = ? COLLATE NOCASE"
                              " AND enabled = 1 LIMIT 1");
    if (!stStmt.ok()) return std::nullopt;

    stStmt.bindText(1, strNickname);
    if (!stStmt.step()) return std::nullopt;

    UserRecord_S stRecord;
    if (!readRow(stStmt, &stRecord)) return std::nullopt;
    return stRecord;
}

/*
 * 功能描述:  查找byusername。
 * 作者:      JaTin
 * param [in]   strUsername      登录用户名，类型 const std::string&。
 * return:    返回 std::optional<UserRecord_S> 类型的处理结果，具体含义见功能描述。
 */
std::optional<UserRecord_S> CUserRepository::findByUsername(const std::string& strUsername) {
    if (!db_.isOpen()) return std::nullopt;

    CStatement stStmt(db_, std::string("SELECT ") + kSelectColumns
                            + " FROM users WHERE username = ? LIMIT 1");
    if (!stStmt.ok()) return std::nullopt;

    stStmt.bindText(1, strUsername);
    if (!stStmt.step()) return std::nullopt;

    UserRecord_S stRecord;
    if (!readRow(stStmt, &stRecord)) return std::nullopt;
    return stRecord;
}

/*
 * 功能描述:  校验credentials；失败时返回 false。
 * 作者:      JaTin
 * param [in]   strUsername      登录用户名，类型 const std::string&。
 * param [in]   strPassword      登录密码，类型 const std::string&。
 * param [in]   pOut             输入数据，类型 UserRecord_S*。
 * return:    成功返回 true；参数非法或执行失败时返回 false。
 */
bool CUserRepository::verifyCredentials(const std::string& strUsername,
                                       const std::string& strPassword,
                                       UserRecord_S* pOut) {
    if (!db_.isOpen()) return false;

    CStatement stStmt(db_, "SELECT id, username, nickname, age, description, enabled, "
                        "password_hash, salt FROM users WHERE username = ? LIMIT 1");
    if (!stStmt.ok()) return false;

    stStmt.bindText(1, strUsername);
    /* 账号不存在 */
    if (!stStmt.step()) return false;

    UserRecord_S stRecord;
    readRow(stStmt, &stRecord);
    if (!stRecord.enabled) return false;

    const std::string strExpectedHash = stStmt.columnText(6);
    const std::string strSalt = stStmt.columnText(7);
    if (!verifyPassword(strSalt, strPassword, strExpectedHash)) return false;

    if (pOut != nullptr) *pOut = stRecord;
    return true;
}

/*
 * 功能描述:  创建用户；失败时返回 false。
 * 作者:      JaTin
 * param [in]   strUsername      登录用户名，类型 const std::string&。
 * param [in]   strPassword      登录密码，类型 const std::string&。
 * param [in]   strNickname      用户昵称，类型 const std::string&。
 * param [in]   nAge             年龄（0 到 150），类型 int。
 * param [in]   strDescription   输入数据，类型 const std::string&。
 * param [in]   pError           错误信息，类型 std::string*。
 * return:    成功返回 true；参数非法或执行失败时返回 false。
 */
bool CUserRepository::createUser(const std::string& strUsername, const std::string& strPassword,
                                const std::string& strNickname, int nAge,
                                const std::string& strDescription, std::string* pError) {
    if (!db_.isOpen()) {
        if (pError != nullptr) *pError = "database not open";
        return false;
    }

    const std::string strSalt = randomSalt();
    const std::string strHash = hashPassword(strSalt, strPassword);

    CStatement stStmt(db_, "INSERT INTO users "
                        "(username, password_hash, salt, nickname, age, description, "
                        " enabled, created_at) VALUES (?, ?, ?, ?, ?, ?, 1, ?)");
    if (!stStmt.ok()) {
        if (pError != nullptr) *pError = stStmt.error();
        return false;
    }

    stStmt.bindText(1, strUsername);
    stStmt.bindText(2, strHash);
    stStmt.bindText(3, strSalt);
    stStmt.bindText(4, strNickname);
    stStmt.bindInt64(5, nAge);
    stStmt.bindText(6, strDescription);
    stStmt.bindInt64(7, static_cast<int64_t>(nowMs()));

    stStmt.step();
    if (stStmt.failed()) {
        if (pError != nullptr) *pError = stStmt.error();
        return false;
    }
    return true;
}

/*
 * 功能描述:  更新访问时间login；失败时返回 false。
 * 作者:      JaTin
 * param [in]   lUserId          用户编号，类型 int64_t。
 * return:    成功返回 true；参数非法或执行失败时返回 false。
 */
bool CUserRepository::touchLogin(int64_t lUserId) {
    if (!db_.isOpen()) return false;

    CStatement stStmt(db_, "UPDATE users SET last_login_at = ? WHERE id = ?");
    if (!stStmt.ok()) return false;

    stStmt.bindInt64(1, static_cast<int64_t>(nowMs()));
    stStmt.bindInt64(2, lUserId);
    stStmt.step();
    return !stStmt.failed();
}

/*
 * 功能描述:  统计count。
 * 作者:      JaTin
 * return:    返回计数或长度；无有效数据时返回 0。
 */
size_t CUserRepository::count() {
    if (!db_.isOpen()) return 0;

    CStatement stStmt(db_, "SELECT COUNT(*) FROM users");
    if (!stStmt.ok() || !stStmt.step()) return 0;
    return static_cast<size_t>(stStmt.columnInt64(0));
}

/* namespace dp */
}
