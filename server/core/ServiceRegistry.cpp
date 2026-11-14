/**
 * @file       ServiceRegistry.cpp
 * @brief      CServiceRegistry 的实现。
 * @author     JaTin
 * @date       2026-11-13
 * @note       修改记录：2026-11-14 JaTin 统一为 Doxygen 风格注释。
 */

#include "server/core/ServiceRegistry.h"

#include "server/core/Logger.h"

namespace dp {

/**
 * @brief      获取本类的进程内单例对象。
 * @note       本函数无输入参数。
 * @return     返回 CServiceRegistry& CServiceRegistry:: 类型的对象引用或指针，调用方按接口约定使用。
 * @author     JaTin
 */
CServiceRegistry& CServiceRegistry::instance() {
    static CServiceRegistry stRegistry;
    return stRegistry;
}

/**
 * @brief      注册业务服务；实现上异常路径记录错误或警告日志。
 * @param[in] stService      业务服务；类型 std::unique_ptr<CService>。
 * @return     无返回值；处理结果通过成员状态或信号对外体现。
 * @author     JaTin
 */
void CServiceRegistry::registerService(std::unique_ptr<CService> stService) {
    if (stService == nullptr) {
        LOG_WARN("registerService: null service ignored");
        return;
    }

    const std::vector<OpCode_EN> aOps = stService->opcodes();
    if (aOps.empty()) {
        LOG_WARN("registerService: service declares no opcode, ignored");
        return;
    }

    /**
     * @brief  先整体检查冲突，避免「注册了一半才发现重复」导致部分 opcode 生效。
     */
    for (OpCode_EN enOp : aOps) {
        if (m_aIndex.find(enOp) != m_aIndex.end()) {
            LOG_ERROR("registerService: opcode %s already registered, "
                      "the whole service is rejected", opCodeName(enOp));
            return;
        }
    }

    CService* pRaw = stService.get();
    for (OpCode_EN enOp : aOps) {
        m_aIndex.emplace(enOp, pRaw);
    }
    m_aOwned.push_back(std::move(stService));

    LOG_DEBUG("service registered (%zu instance(s), %zu opcode mapping(s))",
              m_aOwned.size(), m_aIndex.size());
}

/**
 * @brief      分发 dispatch；失败时返回 false。
 * @param[out] stSession      目标会话对象；类型 CSession&。
 * @param[in] enOp           协议操作码；类型 OpCode_EN。
 * @param[in] stPayload      协议载荷内容；类型 const Json&。
 * @return     成功返回 true；参数非法或执行失败时返回 false。
 * @author     JaTin
 */
bool CServiceRegistry::dispatch(CSession& stSession, OpCode_EN enOp, const Json& stPayload) {
    auto stIt = m_aIndex.find(enOp);
    if (stIt == m_aIndex.end()) return false;

    stIt->second->handle(stSession, enOp, stPayload);
    return true;
}

/**
 * @brief      清空 clear。
 * @note       本函数无输入参数。
 * @return     无返回值；处理结果通过成员状态或信号对外体现。
 * @author     JaTin
 */
void CServiceRegistry::clear() {
    m_aIndex.clear();
    m_aOwned.clear();
}

/**
 * @brief  namespace dp。
 */
}
