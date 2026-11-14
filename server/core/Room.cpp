/*
 * 文件名:    Room.cpp
 * 创建日期:  2026-11-13
 * 作者:      JaTin
 * 功能描述:  CRoom 实现文件，包含相关接口的具体实现。
 * 修改记录:  2026-11-13 JaTin 按 C/C++ 编码规范整改注释与命名。
 */

/* server/CRoom.cpp */
#include "server/core/Room.h"

namespace dp {
namespace {

/* 每条消息除正文外的固定开销估算（seq/from/name/ts/JSON 标点）。 */
constexpr size_t kEntryOverhead = 48;

/* namespace */
}

/*
 * 功能描述:  添加add；失败时返回 false。
 * 作者:      JaTin
 * param [in]   pS               目标会话对象，类型 CSession*。
 * return:    成功返回 true；参数非法或执行失败时返回 false。
 */
bool CRoom::add(CSession* pS) {
    if (pS == nullptr) return false;
    m_uEmptySince = 0;
    return m_uMembers.emplace(pS->id(), pS).second;
}

/*
 * 功能描述:  移除remove。
 * 作者:      JaTin
 * param [in]   uSessionId       输入数据，类型 uint64_t。
 * return:    成功返回 true；参数非法或执行失败时返回 false。
 */
bool CRoom::remove(uint64_t uSessionId) {
    return m_uMembers.erase(uSessionId) > 0;
}

/*
 * 功能描述:  广播broadcast；实现上向目标会话或整个房间下发数据。
 * 作者:      JaTin
 * param [in]   fnSend           输入数据，类型 const std::function<void(CSession&)>&。
 * param [in]   uExceptId        输入数据，类型 uint64_t。
 * return:    无返回值；处理结果通过成员状态或信号对外体现。
 */
void CRoom::broadcast(const std::function<void(CSession&)>& fnSend, uint64_t uExceptId) const {
    for (const auto& stKv : m_uMembers) {
        CSession* pS = stKv.second;
        if (pS == nullptr || pS->id() == uExceptId) continue;
        if (pS->state() == SessionState_EN::Closing) continue;
        fnSend(*pS);
    }
}

/*
 * 功能描述:  追加历史消息。
 * 作者:      JaTin
 * param [in]   stEntry          条目，类型 HistoryEntry_S。
 * param [in]   uMaxEntries      输入数据，类型 size_t。
 * param [in]   uMaxBytes        输入数据，类型 size_t。
 * return:    无返回值；处理结果通过成员状态或信号对外体现。
 */
void CRoom::appendHistory(HistoryEntry_S stEntry, size_t uMaxEntries, size_t uMaxBytes) {
    /* 语音条目的 audio 也要计入字节预算，否则一条 40 KB 的语音能把缓冲撑爆 */
    const auto stCostOf = [](const HistoryEntry_S& stE) {
        return stE.payloadBytes() + stE.name.size() + kEntryOverhead;
    };

    m_uHistoryBytes += stCostOf(stEntry);
    m_stHistory.push_back(std::move(stEntry));

    /* 双重上限：条数优先，其次按字节淘汰最旧的。 */
    while (m_stHistory.size() > uMaxEntries) {
        m_uHistoryBytes -= stCostOf(m_stHistory.front());
        m_stHistory.pop_front();
    }
    while (m_uHistoryBytes > uMaxBytes && m_stHistory.size() > 1) {
        m_uHistoryBytes -= stCostOf(m_stHistory.front());
        m_stHistory.pop_front();
    }
}

/* namespace dp */
}
