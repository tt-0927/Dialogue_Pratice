/**
 * @file  UserRepository.h
 * @brief 用户表的读写。
 * @details 用户数据与聊天数据分库存放：本类只碰 `users.db`。
 *          密码以「随机盐 + SHA-256」存储，不保存明文。
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "server/core/Database.h"

namespace dp {

/**
 * @brief 一条用户记录（不含密码字段）。
 */
struct UserRecord {
    int64_t     id = 0;         ///< 持久用户 id
    std::string username;       ///< 登录名，唯一
    std::string nickname;       ///< 显示昵称
    int         age = 0;        ///< 年龄，0 表示未填写
    std::string description;    ///< 个人简介
    bool        enabled = true; ///< 是否允许登录
};

/**
 * @brief 用户表的访问对象。
 *
 * @details 建表语句在 `ensureSchema()` 里，可重复调用。
 *          所有查询都用参数绑定，不做字符串拼接，避免注入。
 */
class UserRepository {
public:
    UserRepository() = default;
    UserRepository(const UserRepository&) = delete;
    UserRepository& operator=(const UserRepository&) = delete;

    /**
     * @brief 打开用户数据库。
     * @param path  文件路径（不存在则创建）。
     * @param error 失败原因。
     * @retval true 打开成功。
     */
    bool open(const std::string& path, std::string* error);

    /**
     * @brief 建表与索引（幂等）。
     * @param error 失败原因。
     * @retval true 表结构已就绪。
     */
    bool ensureSchema(std::string* error);

    /// @brief 是否已打开。
    bool isOpen() const { return db_.isOpen(); }

    /**
     * @brief 按昵称查找（供「只发昵称」的老客户端兼容使用）。
     * @param nickname 昵称。
     * @retval 找到时返回记录；未找到或重名时返回空。
     */
    std::optional<UserRecord> findByNickname(const std::string& nickname);

    /**
     * @brief 按登录名查找。
     * @param username 登录名。
     * @retval 找到时返回记录。
     */
    std::optional<UserRecord> findByUsername(const std::string& username);

    /**
     * @brief 校验账号密码。
     * @param username 登录名。
     * @param password 明文密码。
     * @param out      校验通过时写入用户记录，可为空。
     * @retval true  账号存在、密码正确且账号未禁用。
     * @retval false 任一条件不满足。
     */
    bool verifyCredentials(const std::string& username, const std::string& password,
                           UserRecord* out);

    /**
     * @brief 创建用户。
     * @param username    登录名（唯一）。
     * @param password    明文密码，内部加盐哈希后存储。
     * @param nickname    显示昵称。
     * @param age         年龄，0 表示未填写。
     * @param description 个人简介。
     * @param error       失败原因（如用户名已存在）。
     * @retval true 创建成功。
     */
    bool createUser(const std::string& username, const std::string& password,
                    const std::string& nickname, int age, const std::string& description,
                    std::string* error);

    /**
     * @brief 记录一次登录时间。
     * @param userId 用户 id。
     * @retval true 更新成功。
     */
    bool touchLogin(int64_t userId);

    /// @brief 用户总数。
    size_t count();

private:
    /**
     * @brief 从当前结果行读出一条用户记录。
     * @param stmt 已 step 到目标行的语句。
     * @param out  输出记录。
     * @retval true 读取成功。
     */
    static bool readRow(Statement& stmt, UserRecord* out);

    Database db_;   ///< users.db 连接
};

}  // namespace dp
