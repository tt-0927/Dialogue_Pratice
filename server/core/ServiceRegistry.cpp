/**
 * @file  ServiceRegistry.cpp
 * @brief ServiceRegistry 的实现。
 */

#include "server/core/ServiceRegistry.h"

#include "server/core/Logger.h"

namespace dp {

ServiceRegistry& ServiceRegistry::instance() {
    static ServiceRegistry registry;
    return registry;
}

void ServiceRegistry::registerService(std::unique_ptr<Service> service) {
    if (service == nullptr) {
        LOG_WARN("registerService: null service ignored");
        return;
    }

    const std::vector<OpCode> ops = service->opcodes();
    if (ops.empty()) {
        LOG_WARN("registerService: service declares no opcode, ignored");
        return;
    }

    // 先整体检查冲突，避免「注册了一半才发现重复」导致部分 opcode 生效。
    for (OpCode op : ops) {
        if (index_.find(op) != index_.end()) {
            LOG_ERROR("registerService: opcode %s already registered, "
                      "the whole service is rejected", opCodeName(op));
            return;
        }
    }

    Service* raw = service.get();
    for (OpCode op : ops) {
        index_.emplace(op, raw);
    }
    owned_.push_back(std::move(service));

    LOG_DEBUG("service registered (%zu instance(s), %zu opcode mapping(s))",
              owned_.size(), index_.size());
}

bool ServiceRegistry::dispatch(Session& session, OpCode op, const Json& payload) {
    auto it = index_.find(op);
    if (it == index_.end()) return false;

    it->second->handle(session, op, payload);
    return true;
}

void ServiceRegistry::clear() {
    index_.clear();
    owned_.clear();
}

}  // namespace dp
