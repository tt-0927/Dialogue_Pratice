/**
 * @file  Service.h
 * @brief 业务服务的抽象基类。
 * @details 一个 Service 负责一个业务领域下的若干 opcode（例如「用户」领域负责
 *          登录、改资料、心跳）。服务只依赖 ServerContext，不接触 epoll / socket，
 *          因此可以脱离事件循环单独构造与测试。
 */

#pragma once

#include <vector>

#include "common/Protocol.h"
#include "server/core/ServerContext.h"

namespace dp {

/**
 * @brief 一个业务领域的请求处理者。
 *
 * @details 生命周期由 ServiceRegistry 以 `std::unique_ptr` 独占持有，
 *          服务端启动时一次性注册，运行期不再增删。
 *
 *          派生类需实现：
 *          - opcodes()：声明本服务负责哪些请求类型
 *          - handle()：按 opcode 分派到具体的私有处理函数
 */
class Service {
public:
    /**
     * @brief 构造服务并绑定上下文。
     * @param ctx 共享上下文引用，生命周期必须长于本对象（由 Server 保证）。
     */
    explicit Service(ServerContext& ctx) noexcept : ctx_(ctx) {}

    virtual ~Service() = default;

    Service(const Service&) = delete;
    Service& operator=(const Service&) = delete;

    /**
     * @brief 本服务负责的请求类型集合。
     * @retval 注册表据此建立 opcode 到服务实例的映射，允许一个服务对应多个 opcode。
     */
    virtual std::vector<OpCode> opcodes() const = 0;

    /**
     * @brief 处理一条已解码的请求。
     * @param session 发起请求的会话，可读写其身份与房间状态。
     * @param op      请求类型，由注册表保证落在 opcodes() 范围内。
     * @param payload 已解析的 JSON 对象；协议层保证它不是 discarded 状态。
     */
    virtual void handle(Session& session, OpCode op, const Json& payload) = 0;

protected:
    ServerContext& ctx_;   ///< 共享上下文，派生类通过它发送响应与广播
};

}  // namespace dp
