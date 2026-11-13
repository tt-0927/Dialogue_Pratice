/**
 * @file  UserService.h
 * @brief 用户领域业务：登录、资料更新、心跳。
 * @details 只处理「会话自身的身份状态」，不涉及房间与消息。
 */

#pragma once

#include "server/core/Service.h"

namespace dp {

/**
 * @brief 处理 `Login` / `UpdateProfile` / `Ping` 三类请求。
 *
 * @details 校验规则全部走 `core/Validation.h`，与房间服务保持同一套口径。
 *          登录成功后立即下发欢迎消息，让客户端有明确的「可以开始用」信号。
 */
class UserService final : public Service {
public:
    using Service::Service;

    /**
     * @brief 声明负责的 opcode。
     * @retval 含 Login、UpdateProfile、Ping 的列表。
     */
    std::vector<OpCode> opcodes() const override;

    /**
     * @brief 按 opcode 分派到具体处理函数。
     * @param session 发起请求的会话。
     * @param op      请求类型。
     * @param payload 已解析的 JSON 负载。
     */
    void handle(Session& session, OpCode op, const Json& payload) override;

private:
    /**
     * @brief 处理登录。
     * @param session 会话，成功后状态变为 LoggedIn。
     * @param j       负载：`{"name":..,"age":..,"desc":..}`。
     */
    void onLogin(Session& session, const Json& j);

    /**
     * @brief 处理资料更新，只覆盖请求里出现的字段。
     * @param session 会话。
     * @param j       负载，字段均可选。
     * @details 改名后会向同房间广播 `rename` 系统消息，
     *          否则其他成员的成员列表仍显示旧名字。
     */
    void onUpdateProfile(Session& session, const Json& j);

    /**
     * @brief 处理自助注册。
     * @param session 会话（必须未登录）。
     * @param j       负载：`{"username":..,"password":..,"nickname":..,"age":..,"desc":..}`。
     * @details 只有 `username` 与 `password` 必填，其余可选。
     *          注册成功只返回新账号信息，**不自动登录**——客户端需要再用新密码登录一次，
     *          这样「注册」和「登录」两条路径完全独立，排查问题更简单。
     */
    void onRegister(Session& session, const Json& j);

    /**
     * @brief 处理心跳。
     * @param session 会话。
     * @param j       负载（忽略），响应里回服务端时间戳。
     */
    void onPing(Session& session, const Json& j);
};

}  // namespace dp
