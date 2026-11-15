/**
 * @file       UserRepository.h
 * @brief      用户表的读写。
 * @author     JaTin
 * @date       2026-11-13
 * @note       修改记录：2026-11-14 JaTin 统一为 Doxygen 风格注释；
 *             2026-11-14 JaTin 增加头像字段与资料持久化接口。
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "server/core/Database.h"

namespace dp {

/**
 * @brief   一条用户记录（不含密码字段）。
 * @author  JaTin
 */
struct UserRecord_S {
    /**
     * @brief  持久用户 id
     * @note   类型 int64_t；类/结构体成员变量。
     */
    int64_t     id = 0;
    /**
     * @brief  登录名，唯一
     * @note   类型 std::string；类/结构体成员变量。
     */
    std::string username;
    /**
     * @brief  显示昵称
     * @note   类型 std::string；类/结构体成员变量。
     */
    std::string nickname;
    /**
     * @brief  年龄，0 表示未填写
     * @note   类型 int；类/结构体成员变量。
     */
    int         age = 0;
    /**
     * @brief  个人简介
     * @note   类型 std::string；类/结构体成员变量。
     */
    std::string description;
    /**
     * @brief  JPEG 头像二进制；空串表示使用昵称首字头像。
     * @note   类型 std::string；类/结构体成员变量。
     */
    std::string m_strAvatar;
    /**
     * @brief  是否允许登录
     * @note   类型 bool；类/结构体成员变量。
     */
    bool        enabled = true;
};

/**
 * @brief   用户表的访问对象。
 * @details 建表语句在 `ensureSchema()` 里，可重复调用。 所有查询都用参数绑定，不做字符串拼接，避免注入。
 * @author  JaTin
 */
class CUserRepository {
public:
    CUserRepository() = default;
    CUserRepository(const CUserRepository&) = delete;
    CUserRepository& operator=(const CUserRepository&) = delete;

    /**
     * @brief      打开用户数据库。
     * @param[in] path           文件路径（不存在则创建）；类型 const std::string&。
     * @param[in] error          失败原因；类型 std::string*。
     * @return     true 打开成功。
     * @author     JaTin
     */
    bool open(const std::string& path, std::string* error);

    /**
     * @brief      建表与索引（幂等）。
     * @param[in] error          失败原因；类型 std::string*。
     * @return     true 表结构已就绪。
     * @author     JaTin
     */
    bool ensureSchema(std::string* error);

    /**
     * @brief  是否已打开。
     */
    bool isOpen() const { return db_.isOpen(); }

    /**
     * @brief      按昵称查找（供「只发昵称」的老客户端兼容使用）。
     * @param[in] nickname       昵称；类型 const std::string&。
     * @return     找到时返回记录；未找到或重名时返回空。
     * @author     JaTin
     */
    std::optional<UserRecord_S> findByNickname(const std::string& nickname);

    /**
     * @brief      按登录名查找。
     * @param[in] username       登录名；类型 const std::string&。
     * @return     找到时返回记录。
     * @author     JaTin
     */
    std::optional<UserRecord_S> findByUsername(const std::string& username);

    /**
     * @brief 校验账号密码。
     * @param username 登录名。
     * @param password 明文密码。
     * @param out      校验通过时写入用户记录，可为空。
     * @retval true  账号存在、密码正确且账号未禁用。
     * @retval false 任一条件不满足。
     */
    bool verifyCredentials(const std::string& username, const std::string& password,
                           UserRecord_S* out);

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
     * @brief      持久化昵称、年龄、简介与头像，登录名和密码保持不变。
     * @param[in] user           完整资料快照，id 必须是持久账号编号；类型 const UserRecord_S&。
     * @param[out] error         更新失败时写入数据库错误原因；类型 std::string*。
     * @return     成功更新一行返回 true，否则返回 false。
     * @author     JaTin
     */
    bool updateProfile(const UserRecord_S& user, std::string* error);

    /**
     * @brief      记录一次登录时间。
     * @param[in] userId         用户 id；类型 int64_t。
     * @return     true 更新成功。
     * @author     JaTin
     */
    bool touchLogin(int64_t userId);

    /**
     * @brief      用户总数。
     * @note       本函数无输入参数。
     * @return     返回计数或长度；无有效数据时返回 0。
     * @author     JaTin
     */
    size_t count();

private:
    /**
     * @brief      从当前结果行读出一条用户记录。
     * @param[out] stmt           已 step 到目标行的语句；类型 CStatement&。
     * @param[in] out            输出记录；类型 UserRecord_S*。
     * @return     true 读取成功。
     * @author     JaTin
     */
    static bool readRow(CStatement& stmt, UserRecord_S* out);

    /**
     * @brief  users.db 连接
     * @note   类型 CDatabase；类/结构体成员变量。
     */
    CDatabase db_;
};

/**
 * @brief  namespace dp。
 */
}
