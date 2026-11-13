/*
 * 文件名:    ServiceRegistry.cpp
 * 创建日期:  2026-11-13
 * 作者:      JaTin
 * 功能描述:  CServiceRegistry 的实现。
 * 修改记录:  2026-11-13 JaTin 按 C/C++ 编码规范整改注释与命名。
 */

#include "server/core/ServiceRegistry.h"

#include "server/core/Logger.h"

namespace dp {

/*
 * 功能描述:  执行 instance 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * return:    返回值，含义见功能描述。
 */
CServiceRegistry& CServiceRegistry::instance() {
    static CServiceRegistry registry;
    return registry;
}

/*
 * 功能描述:  执行 registerService 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [in]  service  参数。
 * return:    void
 */
void CServiceRegistry::registerService(std::unique_ptr<CService> service) {
    if (service == nullptr) {
        LOG_WARN("registerService: null service ignored");
        return;
    }

    const std::vector<OpCode_EN> ops = service->opcodes();
    if (ops.empty()) {
        LOG_WARN("registerService: service declares no opcode, ignored");
        return;
    }

    /* 先整体检查冲突，避免「注册了一半才发现重复」导致部分 opcode 生效。 */
    for (OpCode_EN op : ops) {
        if (m_aIndex.find(op) != m_aIndex.end()) {
            LOG_ERROR("registerService: opcode %s already registered, "
                      "the whole service is rejected", opCodeName(op));
            return;
        }
    }

    CService* raw = service.get();
    for (OpCode_EN op : ops) {
        m_aIndex.emplace(op, raw);
    }
    m_aOwned.push_back(std::move(service));

    LOG_DEBUG("service registered (%zu instance(s), %zu opcode mapping(s))",
              m_aOwned.size(), m_aIndex.size());
}

/*
 * 功能描述:  执行 dispatch 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [out]  session  参数。
 * param [in]  op  参数。
 * param [in]  payload  参数。
 * return:    返回值，含义见功能描述。
 */
bool CServiceRegistry::dispatch(CSession& session, OpCode_EN op, const Json& payload) {
    auto it = m_aIndex.find(op);
    if (it == m_aIndex.end()) return false;

    it->second->handle(session, op, payload);
    return true;
}

/*
 * 功能描述:  执行 clear 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * return:    void
 */
void CServiceRegistry::clear() {
    m_aIndex.clear();
    m_aOwned.clear();
}

/* namespace dp */
}
