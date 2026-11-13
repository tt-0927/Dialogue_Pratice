/*
 * 文件名:    Database.h
 * 创建日期:  2026-11-13
 * 作者:      JaTin
 * 功能描述:  SQLite 的轻量 RAII 封装（连接 + 预处理语句）。
 * 修改记录:  2026-11-13 JaTin 按 C/C++ 编码规范整改注释与命名。
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
class CDatabase {
public:
    CDatabase() = default;
    ~CDatabase();

    CDatabase(const CDatabase&) = delete;
    CDatabase& operator=(const CDatabase&) = delete;

    /**
     * @brief 打开（不存在则创建）数据库文件。
     * @param path  文件路径，父目录必须已存在。
     * @param error 失败时写入原因，可为空。
     * @retval true  打开成功。
     * @retval false 打开失败，`error` 有描述。
     */
    bool open(const std::string& path, std::string* error);

    /* 关闭连接（幂等）。 */
    void close();

    /* 是否已打开。 */
    bool isOpen() const { return db_ != nullptr; }

    /**
     * @brief 执行一段 SQL（建表、PRAGMA 等，不支持参数绑定）。
     * @param sql   一条或多条语句。
     * @param error 失败时写入原因。
     * @retval true 全部执行成功。
     */
    bool exec(const std::string& sql, std::string* error);

    /* 最近一次错误的可读描述。 */
    std::string lastError() const;

    /* 底层句柄，仅供 CStatement 使用。 */
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
class CStatement {
public:
    /**
     * @brief 编译一条 SQL。
     * @param db  已打开的数据库。
     * @param sql 带 `?` 占位符的语句。
     */
    CStatement(CDatabase& db, const std::string& sql);

    ~CStatement();

    CStatement(const CStatement&) = delete;
    CStatement& operator=(const CStatement&) = delete;

    /* 编译是否成功。 */
    bool ok() const { return m_pStmt != nullptr; }

    /* 编译或执行阶段的错误描述。 */
    std::string error() const { return m_strError; }

    /* 绑定文本（sqlite 会复制一份，调用后原串可释放）。 */
    void bindText(int index, const std::string& value);

    /* 绑定 64 位整数。 */
    void bindInt64(int index, int64_t value);

    /* 绑定二进制块。 */
    void bindBlob(int index, const std::string& value);

    /**
     * @brief 推进到下一行。
     * @retval true  当前行有数据。
     * @retval false 已结束，或执行出错（用 failed() 区分）。
     */
    bool step();

    /* 上一次 step() 是否因错误而返回 false。 */
    bool failed() const { return m_nFailed; }

    /* 读取整数列。 */
    int64_t columnInt64(int column) const;

    /* 读取文本列。 */
    std::string columnText(int column) const;

    /* 读取二进制列。 */
    std::string columnBlob(int column) const;

    /* 该列是否为 NULL。 */
    bool columnIsNull(int column) const;

    /* 复位以便重复执行（保留绑定值）。 */
    void reset();

private:
    /* < 已编译语句 */
    sqlite3_stmt* m_pStmt = nullptr;
    /* < step() 是否出错 */
    bool          m_nFailed = false;
    /* < 错误描述 */
    std::string   m_strError;
};

/* namespace dp */
}
