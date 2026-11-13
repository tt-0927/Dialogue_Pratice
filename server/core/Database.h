/**
 * @file  Database.h
 * @brief SQLite 的轻量 RAII 封装（连接 + 预处理语句）。
 * @details 只暴露业务层需要的最小接口，把 sqlite3 的类型挡在实现文件里，
 *          避免每个包含本头文件的翻译单元都拖进 68 万行的 sqlite3.h。
 *
 * @note 服务端是单线程 reactor，所有访问都发生在事件循环线程，
 *       因此不需要连接池或额外加锁。
 */

#pragma once

#include <cstdint>
#include <string>

struct sqlite3;
struct sqlite3_stmt;

namespace dp {

/**
 * @brief 一个 SQLite 数据库连接。
 *
 * @details 打开时自动设置：
 *          - `journal_mode=WAL`（写入不阻塞读，崩溃恢复更快）
 *          - `foreign_keys=ON`
 *          - `busy_timeout=3000`（万一有多进程访问，避免立即报 SQLITE_BUSY）
 */
class Database {
public:
    Database() = default;
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    /**
     * @brief 打开（不存在则创建）数据库文件。
     * @param path  文件路径，父目录必须已存在。
     * @param error 失败时写入原因，可为空。
     * @retval true  打开成功。
     * @retval false 打开失败，`error` 有描述。
     */
    bool open(const std::string& path, std::string* error);

    /// @brief 关闭连接（幂等）。
    void close();

    /// @brief 是否已打开。
    bool isOpen() const { return db_ != nullptr; }

    /**
     * @brief 执行一段 SQL（建表、PRAGMA 等，不支持参数绑定）。
     * @param sql   一条或多条语句。
     * @param error 失败时写入原因。
     * @retval true 全部执行成功。
     */
    bool exec(const std::string& sql, std::string* error);

    /// @brief 最近一次错误的可读描述。
    std::string lastError() const;

    /// @brief 底层句柄，仅供 Statement 使用。
    sqlite3* handle() const { return db_; }

private:
    sqlite3* db_ = nullptr;
};

/**
 * @brief 一条预处理语句。
 *
 * @details 构造时即编译 SQL；析构自动 finalize。
 *          所有 `bind*` / `column*` 的下标都是 1-based（沿用 sqlite 约定）。
 */
class Statement {
public:
    /**
     * @brief 编译一条 SQL。
     * @param db  已打开的数据库。
     * @param sql 带 `?` 占位符的语句。
     */
    Statement(Database& db, const std::string& sql);

    ~Statement();

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    /// @brief 编译是否成功。
    bool ok() const { return stmt_ != nullptr; }

    /// @brief 编译或执行阶段的错误描述。
    std::string error() const { return error_; }

    /// @brief 绑定文本（sqlite 会复制一份，调用后原串可释放）。
    void bindText(int index, const std::string& value);

    /// @brief 绑定 64 位整数。
    void bindInt64(int index, int64_t value);

    /// @brief 绑定二进制块。
    void bindBlob(int index, const std::string& value);

    /**
     * @brief 推进到下一行。
     * @retval true  当前行有数据。
     * @retval false 已结束，或执行出错（用 failed() 区分）。
     */
    bool step();

    /// @brief 上一次 step() 是否因错误而返回 false。
    bool failed() const { return failed_; }

    /// @brief 读取整数列。
    int64_t columnInt64(int column) const;

    /// @brief 读取文本列。
    std::string columnText(int column) const;

    /// @brief 读取二进制列。
    std::string columnBlob(int column) const;

    /// @brief 该列是否为 NULL。
    bool columnIsNull(int column) const;

    /// @brief 复位以便重复执行（保留绑定值）。
    void reset();

private:
    sqlite3_stmt* stmt_ = nullptr;   ///< 已编译语句
    bool          failed_ = false;   ///< step() 是否出错
    std::string   error_;            ///< 错误描述
};

}  // namespace dp
