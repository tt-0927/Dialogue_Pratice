/**
 * @file       Service.h
 * @brief      业务服务的抽象基类。
 * @author     JaTin
 * @date       2026-11-13
 * @note       修改记录：2026-11-14 JaTin 统一为 Doxygen 风格注释。
 */

#pragma once

#include <vector>

#include "common/Protocol.h"
#include "server/core/ServerContext.h"

namespace dp {

/**
 * @brief   一个业务领域的请求处理者。
 * @details 生命周期由 CServiceRegistry 以 `std::unique_ptr` 独占持有， 服务端启动时一次性注册，运行期不再增删。 派生类需实现： - opcodes()：声明本服务负责哪些请求类型 - handle()：按 opcode 分派到具体的私有处理函数
 * @author  JaTin
 */
class CService {
public:
    /**
     * @brief 构造服务并绑定上下文。
     * @param ctx 共享上下文引用，生命周期必须长于本对象（由 CServer 保证）。
     */
    explicit CService(CServerContext& ctx) noexcept : m_stCtx(ctx) {}

    /**
     * @brief      析构 CService 对象，释放运行期持有的全部资源。
     * @note       本函数无输入参数。
     * @return     构造/析构函数无返回值。
     * @author     JaTin
     */
    virtual ~CService() = default;

    CService(const CService&) = delete;
    CService& operator=(const CService&) = delete;

    /**
     * @brief      本服务负责的请求类型集合。
     * @note       本函数无输入参数。
     * @return     注册表据此建立 opcode 到服务实例的映射，允许一个服务对应多个 opcode。
     * @author     JaTin
     */
    virtual std::vector<OpCode_EN> opcodes() const = 0;

    /**
     * @brief      处理一条已解码的请求。
     * @param[out] session        发起请求的会话，可读写其身份与房间状态；类型 CSession&。
     * @param[in] op             请求类型，由注册表保证落在 opcodes() 范围内；类型 OpCode_EN。
     * @param[in] payload        已解析的 JSON 对象；协议层保证它不是 discarded 状态；类型 const Json&。
     * @return     无返回值；处理结果通过成员状态或信号对外体现。
     * @author     JaTin
     */
    virtual void handle(CSession& session, OpCode_EN op, const Json& payload) = 0;

protected:
    /**
     * @brief  共享上下文，派生类通过它发送响应与广播
     * @note   类型 CServerContext&；类/结构体成员变量。
     */
    CServerContext& m_stCtx;
};

/**
 * @brief  namespace dp。
 */
}
