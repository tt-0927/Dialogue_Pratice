/*
 * 文件名:    Service.h
 * 创建日期:  2026-11-13
 * 作者:      JaTin
 * 功能描述:  业务服务的抽象基类。
 * 修改记录:  2026-11-13 JaTin 按 C/C++ 编码规范整改注释与命名。
 */

#pragma once

#include <vector>

#include "common/Protocol.h"
#include "server/core/ServerContext.h"

namespace dp {

/**
 * @brief 一个业务领域的请求处理者。
 *
 * @details 生命周期由 CServiceRegistry 以 `std::unique_ptr` 独占持有，
 *          服务端启动时一次性注册，运行期不再增删。
 *
 *          派生类需实现：
 *          - opcodes()：声明本服务负责哪些请求类型
 *          - handle()：按 opcode 分派到具体的私有处理函数
 */
class CService {
public:
    /**
     * @brief 构造服务并绑定上下文。
     * @param ctx 共享上下文引用，生命周期必须长于本对象（由 CServer 保证）。
     */
    explicit CService(CServerContext& ctx) noexcept : m_stCtx(ctx) {}

    virtual ~CService() = default;

    CService(const CService&) = delete;
    CService& operator=(const CService&) = delete;

    /**
     * @brief 本服务负责的请求类型集合。
     * @retval 注册表据此建立 opcode 到服务实例的映射，允许一个服务对应多个 opcode。
     */
    virtual std::vector<OpCode_EN> opcodes() const = 0;

    /**
     * @brief 处理一条已解码的请求。
     * @param session 发起请求的会话，可读写其身份与房间状态。
     * @param op      请求类型，由注册表保证落在 opcodes() 范围内。
     * @param payload 已解析的 JSON 对象；协议层保证它不是 discarded 状态。
     */
    virtual void handle(CSession& session, OpCode_EN op, const Json& payload) = 0;

protected:
    /* < 共享上下文，派生类通过它发送响应与广播 */
    CServerContext& m_stCtx;
};

/* namespace dp */
}
