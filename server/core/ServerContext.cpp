/**
 * @file  ServerContext.cpp
 * @brief ServerContext 的实现：把出站请求转交给事件循环。
 */

#include "server/core/ServerContext.h"

#include <utility>

#include "server/core/Reactor.h"

namespace dp {

ServerContext::ServerContext(ServerConfig cfg) : cfg_(std::move(cfg)) {}

ServerContext::~ServerContext() = default;

void ServerContext::sendTo(Session& session, OpCode op, const Json& payload) {
    if (reactor_ == nullptr) return;
    reactor_->sendTo(session, op, payload);
}

void ServerContext::sendError(Session& session, const std::string& code,
                              const std::string& text) {
    if (reactor_ == nullptr) return;
    reactor_->sendError(session, code, text);
}

void ServerContext::broadcastRoom(const std::string& roomId, OpCode op, const Json& payload,
                                  uint64_t exceptId) {
    if (reactor_ == nullptr) return;
    reactor_->broadcastRoom(roomId, op, payload, exceptId);
}

void ServerContext::requestClose(Session& session, const std::string& reason) {
    if (reactor_ == nullptr) return;
    reactor_->requestClose(session, reason);
}

}  // namespace dp
