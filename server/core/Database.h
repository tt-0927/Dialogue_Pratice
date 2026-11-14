/**
 * @file       Database.h
 * @brief      SQLite 的轻量 RAII 封装（连接 + 预处理语句）。
 * @author     JaTin
 * @date       2026-11-13
 * @note       修改记录：2026-11-14 JaTin 统一为 Doxygen 风格注释。
 */

#pragma once

#include <cstdint>
#include <string>

struct sqlite3;
struct sqlite3_stmt;

namespace dp {

/**
 * @brief   一个 SQLite 数据库连接。
 * @details 打开时自动设置： - `journal_mode=WAL`（写入不阻塞读，崩溃恢复更快） - `foreign_keys=ON` - `busy_timeout=3000`（万一有多进程访问，避免立即报 SQLITE_BUSY）
 * @author  JaTin
 */
class CDatabase {
public:
    CDatabase() = default;
    /**
     * @brief      构造本对象，完成成员初始化与依赖注入。
     * @note       本函数无输入参数。
     * @return     构造/析构函数无返回值。
     * @author     JaTin
     */
    ~CDatabase();

    CDatabase(const CDatabase&) = delete;
    CDatabase& operator=(const CDatabase&) = delete;

    /**
     * @brief      打开（不存在则创建）数据库文件。
     * @param[in] path           文件路径，父目录必须已存在；类型 const std::string&。
     * @param[in] error          失败时写入原因，可为空；类型 std::string*。
     * @return     true  打开成功；false 打开失败，`error` 有描述。
     * @author     JaTin
     */
    bool open(const std::string& path, std::string* error);

    /**
     * @brief      关闭连接（幂等）。
     * @note       本函数无输入参数。
     * @return     无返回值；处理结果通过成员状态或信号对外体现。
     * @author     JaTin
     */
    void close();

    /**
     * @brief  是否已打开。
     */
    bool isOpen() const { return db_ != nullptr; }

    /**
     * @brief      执行一段 SQL（建表、PRAGMA 等，不支持参数绑定）。
     * @param[in] sql            一条或多条语句；类型 const std::string&。
     * @param[in] error          失败时写入原因；类型 std::string*。
     * @return     true 全部执行成功。
     * @author     JaTin
     */
    bool exec(const std::string& sql, std::string* error);

    /**
     * @brief      最近一次错误的可读描述。
     * @note       本函数无输入参数。
     * @return     返回构造好的字符串；无内容时返回空字符串。
     * @author     JaTin
     */
    std::string lastError() const;

    /**
     * @brief  底层句柄，仅供 CStatement 使用。
     */
    sqlite3* handle() const { return db_; }

private:
    /**
     * @brief  数据库。 初值 = nullptr。
     * @note   类型 sqlite3*；类/结构体成员变量。
     */
    sqlite3* db_ = nullptr;
};

/**
 * @brief   一条预处理语句。
 * @details 构造时即编译 SQL；析构自动 finalize。 所有 `bind*` / `column*` 的下标都是 1-based（沿用 sqlite 约定）。
 * @author  JaTin
 */
class CStatement {
public:
    /**
     * @brief      编译一条 SQL。
     * @param[in] db             已打开的数据库；类型 CDatabase&。
     * @param[in] sql            带 `?` 占位符的语句；类型 const std::string&。
     * @return     构造/析构函数无返回值。
     * @author     JaTin
     */
    CStatement(CDatabase& db, const std::string& sql);

    /**
     * @brief      构造本对象，完成成员初始化与依赖注入。
     * @note       本函数无输入参数。
     * @return     构造/析构函数无返回值。
     * @author     JaTin
     */
    ~CStatement();

    CStatement(const CStatement&) = delete;
    CStatement& operator=(const CStatement&) = delete;

    /**
     * @brief  编译是否成功。
     */
    bool ok() const { return m_pStmt != nullptr; }

    /**
     * @brief  编译或执行阶段的错误描述。
     */
    std::string error() const { return m_strError; }

    /**
     * @brief      绑定文本（sqlite 会复制一份，调用后原串可释放）。
     * @param[in] index          位置索引；类型 int。
     * @param[in] value          待设置的数值；类型 const std::string&。
     * @return     无返回值；处理结果通过成员状态或信号对外体现。
     * @author     JaTin
     */
    void bindText(int index, const std::string& value);

    /**
     * @brief      绑定 64 位整数。
     * @param[in] index          位置索引；类型 int。
     * @param[in] value          待设置的数值；类型 int64_t。
     * @return     无返回值；处理结果通过成员状态或信号对外体现。
     * @author     JaTin
     */
    void bindInt64(int index, int64_t value);

    /**
     * @brief      绑定二进制块。
     * @param[in] index          位置索引；类型 int。
     * @param[in] value          待设置的数值；类型 const std::string&。
     * @return     无返回值；处理结果通过成员状态或信号对外体现。
     * @author     JaTin
     */
    void bindBlob(int index, const std::string& value);

    /**
     * @brief      推进到下一行。
     * @note       本函数无输入参数。
     * @return     true  当前行有数据；false 已结束，或执行出错（用 failed() 区分）。
     * @author     JaTin
     */
    bool step();

    /**
     * @brief  上一次 step() 是否因错误而返回 false。
     */
    bool failed() const { return m_nFailed; }

    /**
     * @brief      读取整数列。
     * @param[in] column         输入参数；类型 int。
     * @return     返回计数或长度；无有效数据时返回 0。
     * @author     JaTin
     */
    int64_t columnInt64(int column) const;

    /**
     * @brief      读取文本列。
     * @param[in] column         输入参数；类型 int。
     * @return     返回构造好的字符串；无内容时返回空字符串。
     * @author     JaTin
     */
    std::string columnText(int column) const;

    /**
     * @brief      读取二进制列。
     * @param[in] column         输入参数；类型 int。
     * @return     返回构造好的字符串；无内容时返回空字符串。
     * @author     JaTin
     */
    std::string columnBlob(int column) const;

    /**
     * @brief      该列是否为 NULL。
     * @param[in] column         输入参数；类型 int。
     * @return     成功返回 true；参数非法或执行失败时返回 false。
     * @author     JaTin
     */
    bool columnIsNull(int column) const;

    /**
     * @brief      复位以便重复执行（保留绑定值）。
     * @note       本函数无输入参数。
     * @return     无返回值；处理结果通过成员状态或信号对外体现。
     * @author     JaTin
     */
    void reset();

private:
    /**
     * @brief  已编译语句
     * @note   类型 sqlite3_stmt*；类/结构体成员变量。
     */
    sqlite3_stmt* m_pStmt = nullptr;
    /**
     * @brief  step() 是否出错
     * @note   类型 bool；类/结构体成员变量。
     */
    bool          m_nFailed = false;
    /**
     * @brief  错误描述
     * @note   类型 std::string；类/结构体成员变量。
     */
    std::string   m_strError;
};

/**
 * @brief  namespace dp。
 */
}
