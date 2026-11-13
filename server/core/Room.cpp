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
 * 功能描述:  执行 add 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [in]  s  参数。
 * return:    返回值，含义见功能描述。
 */
bool CRoom::add(CSession* s) {
    if (s == nullptr) return false;
    m_uEmptySince = 0;
    return m_uMembers.emplace(s->id(), s).second;
}

/*
 * 功能描述:  执行 remove 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [in]  sessionId  参数。
 * return:    返回值，含义见功能描述。
 */
bool CRoom::remove(uint64_t sessionId) {
    return m_uMembers.erase(sessionId) > 0;
}

/*
 * 功能描述:  执行 broadcast 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [in]  CSession  参数。
 * return:    void
 */
void CRoom::broadcast(const std::function<void(CSession&)>& send, uint64_t exceptId) const {
    for (const auto& kv : m_uMembers) {
        CSession* s = kv.second;
        if (s == nullptr || s->id() == exceptId) continue;
        if (s->state() == SessionState_EN::Closing) continue;
        send(*s);
    }
}

/*
 * 功能描述:  执行 appendHistory 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [in]  entry  参数。
 * param [in]  maxEntries  参数。
 * param [in]  maxBytes  参数。
 * return:    void
 */
void CRoom::appendHistory(HistoryEntry_S entry, size_t maxEntries, size_t maxBytes) {
    /* 语音条目的 audio 也要计入字节预算，否则一条 40 KB 的语音能把缓冲撑爆 */
    const auto costOf = [](const HistoryEntry_S& e) {
        return e.payloadBytes() + e.name.size() + kEntryOverhead;
    };

    m_uHistoryBytes += costOf(entry);
    m_stHistory.push_back(std::move(entry));

    /* 双重上限：条数优先，其次按字节淘汰最旧的。 */
    while (m_stHistory.size() > maxEntries) {
        m_uHistoryBytes -= costOf(m_stHistory.front());
        m_stHistory.pop_front();
    }
    while (m_uHistoryBytes > maxBytes && m_stHistory.size() > 1) {
        m_uHistoryBytes -= costOf(m_stHistory.front());
        m_stHistory.pop_front();
    }
}

/* namespace dp */
}
