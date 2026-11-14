/**
 * @file       Room.cpp
 * @brief      CRoom 实现文件，包含相关接口的具体实现。
 * @author     JaTin
 * @date       2026-11-13
 * @note       修改记录：2026-11-14 JaTin 统一为 Doxygen 风格注释。
 */

/**
 * @brief  server/CRoom.cpp。
 */
#include "server/Room.h"

namespace dp {
namespace {

/**
 * @brief  每条消息除正文外的固定开销估算（seq/from/name/ts/JSON 标点）。
 * @note   取值 48。
 */
constexpr size_t kEntryOverhead = 48;

/**
 * @brief  namespace。
 */
}

/**
 * @brief      添加 add；失败时返回 false。
 * @param[in] pS             目标会话对象；类型 CSession*。
 * @return     成功返回 true；参数非法或执行失败时返回 false。
 * @author     JaTin
 */
bool CRoom::add(CSession* pS) {
    if (pS == nullptr) return false;
    m_uEmptySince = 0;
    return m_uMembers.emplace(pS->id(), pS).second;
}

/**
 * @brief      移除 remove。
 * @param[in] uSessionId     输入数据；类型 uint64_t。
 * @return     成功返回 true；参数非法或执行失败时返回 false。
 * @author     JaTin
 */
bool CRoom::remove(uint64_t uSessionId) {
    return m_uMembers.erase(uSessionId) > 0;
}

/**
 * @brief      广播 broadcast；实现上向目标会话或整个房间下发数据。
 * @param[in] fnSend         输入数据；类型 const std::function<void(CSession&)>&。
 * @param[in] uExceptId      输入数据；类型 uint64_t。
 * @return     无返回值；处理结果通过成员状态或信号对外体现。
 * @author     JaTin
 */
void CRoom::broadcast(const std::function<void(CSession&)>& fnSend, uint64_t uExceptId) const {
    for (const auto& stKv : m_uMembers) {
        CSession* pS = stKv.second;
        if (pS == nullptr || pS->id() == uExceptId) continue;
        if (pS->state() == SessionState_EN::Closing) continue;
        fnSend(*pS);
    }
}

/**
 * @brief      追加历史消息。
 * @param[in] stEntry        条目；类型 HistoryEntry_S。
 * @param[in] uMaxEntries    输入数据；类型 size_t。
 * @param[in] uMaxBytes      输入数据；类型 size_t。
 * @return     无返回值；处理结果通过成员状态或信号对外体现。
 * @author     JaTin
 */
void CRoom::appendHistory(HistoryEntry_S stEntry, size_t uMaxEntries, size_t uMaxBytes) {
    /**
     * @brief  语音条目的 audio 也要计入字节预算，否则一条 40 KB 的语音能把缓冲撑爆。
     */
    const auto stCostOf = [](const HistoryEntry_S& stE) {
        return stE.payloadBytes() + stE.name.size() + kEntryOverhead;
    };

    m_uHistoryBytes += stCostOf(stEntry);
    m_stHistory.push_back(std::move(stEntry));

    /**
     * @brief  双重上限：条数优先，其次按字节淘汰最旧的。
     */
    while (m_stHistory.size() > uMaxEntries) {
        m_uHistoryBytes -= stCostOf(m_stHistory.front());
        m_stHistory.pop_front();
    }
    while (m_uHistoryBytes > uMaxBytes && m_stHistory.size() > 1) {
        m_uHistoryBytes -= stCostOf(m_stHistory.front());
        m_stHistory.pop_front();
    }
}

/**
 * @brief  namespace dp。
 */
}
