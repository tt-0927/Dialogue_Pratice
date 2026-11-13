/*
 * 文件名:    ServerContext.cpp
 * 创建日期:  2026-11-13
 * 作者:      JaTin
 * 功能描述:  CServerContext 的实现：把出站请求转交给事件循环。
 * 修改记录:  2026-11-13 JaTin 按 C/C++ 编码规范整改注释与命名。
 */

#include "server/core/ServerContext.h"

#include <utility>

#include "server/core/Reactor.h"

namespace dp {

CServerContext::CServerContext(ServerConfig_S cfg) : m_stCfg(std::move(cfg)) {}

CServerContext::~CServerContext() = default;

/*
 * 功能描述:  执行 sendTo 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [out]  session  参数。
 * param [in]  op  参数。
 * param [in]  payload  参数。
 * return:    void
 */
void CServerContext::sendTo(CSession& session, OpCode_EN op, const Json& payload) {
    if (reactor_ == nullptr) return;
    reactor_->sendTo(session, op, payload);
}

void CServerContext::sendError(CSession& session, const std::string& code,
                              const std::string& text) {
    if (reactor_ == nullptr) return;
    reactor_->sendError(session, code, text);
}

void CServerContext::broadcastRoom(const std::string& roomId, OpCode_EN op, const Json& payload,
                                  uint64_t exceptId) {
    if (reactor_ == nullptr) return;
    reactor_->broadcastRoom(roomId, op, payload, exceptId);
}

/*
 * 功能描述:  执行 requestClose 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [out]  session  参数。
 * param [in]  reason  参数。
 * return:    void
 */
void CServerContext::requestClose(CSession& session, const std::string& reason) {
    if (reactor_ == nullptr) return;
    reactor_->requestClose(session, reason);
}

/* namespace dp */
}
