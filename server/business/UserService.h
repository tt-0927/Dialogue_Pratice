/*
 * 文件名:    UserService.h
 * 创建日期:  2026-11-13
 * 作者:      JaTin
 * 功能描述:  用户领域业务：登录、资料更新、心跳。
 * 修改记录:  2026-11-13 JaTin 按 C/C++ 编码规范整改注释与命名。
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
class CUserService final : public CService {
public:
    using CService::CService;

    /**
     * @brief 声明负责的 opcode。
     * @retval 含 Login、UpdateProfile、Ping 的列表。
     */
    std::vector<OpCode_EN> opcodes() const override;

    /**
     * @brief 按 opcode 分派到具体处理函数。
     * @param session 发起请求的会话。
     * @param op      请求类型。
     * @param payload 已解析的 JSON 负载。
     */
    void handle(CSession& session, OpCode_EN op, const Json& payload) override;

private:
    /**
     * @brief 处理登录。
     * @param session 会话，成功后状态变为 LoggedIn。
     * @param j       负载：`{"name":..,"age":..,"desc":..}`。
     */
    void onLogin(CSession& session, const Json& j);

    /**
     * @brief 处理资料更新，只覆盖请求里出现的字段。
     * @param session 会话。
     * @param j       负载，字段均可选。
     * @details 改名后会向同房间广播 `rename` 系统消息，
     *          否则其他成员的成员列表仍显示旧名字。
     */
    void onUpdateProfile(CSession& session, const Json& j);

    /**
     * @brief 处理自助注册。
     * @param session 会话（必须未登录）。
     * @param j       负载：`{"username":..,"password":..,"nickname":..,"age":..,"desc":..}`。
     * @details 只有 `username` 与 `password` 必填，其余可选。
     *          注册成功只返回新账号信息，**不自动登录**——客户端需要再用新密码登录一次，
     *          这样「注册」和「登录」两条路径完全独立，排查问题更简单。
     */
    void onRegister(CSession& session, const Json& j);

    /**
     * @brief 处理心跳。
     * @param session 会话。
     * @param j       负载（忽略），响应里回服务端时间戳。
     */
    void onPing(CSession& session, const Json& j);
};

/* namespace dp */
}
