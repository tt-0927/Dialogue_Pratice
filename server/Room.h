/*
 * 文件名:    Room.h
 * 创建日期:  2026-11-13
 * 作者:      JaTin
 * 功能描述:  CRoom 头文件，声明相关类型与接口。
 * 修改记录:  2026-11-13 JaTin 按 C/C++ 编码规范整改注释与命名。
 */

/* server/CRoom.h */

/* 房间：一组 CSession 的集合 + 最近消息环形缓冲。 */
/* 房间由「房间号」字符串唯一标识；成员走空后不立即销毁，而是滞留一段时间 */
/* （保留历史消息），超时才回收——见 CServer::handleTimer()。 */
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <string>

#include "server/Session.h"

namespace dp {

/* 历史条目类型。语音条目带音频数据，文本条目只有 text。 */
enum class HistoryKind_EN : uint8_t {
    Text  = 0,
    Voice = 1,
};

/* 一条历史消息。seq 为服务端全局单调序号，用于客户端去重与补漏。 */
struct HistoryEntry_S {
    uint64_t    seq = 0;
    uint64_t    from = 0;
    std::string name;
    /* 文本消息正文；语音消息为空 */
    std::string text;
    /* 语音消息的 DPV1 容器（二进制）；文本消息为空 */
    std::string audio;
    uint32_t    durationMs = 0;
    HistoryKind_EN kind = HistoryKind_EN::Text;
    /* Unix 毫秒 */
    int64_t     ts = 0;

    size_t payloadBytes() const { return text.size() + audio.size(); }
};

class CRoom {
public:
    explicit CRoom(std::string id) : id_(std::move(id)) {}

    const std::string& id() const { return id_; }

    bool   add(CSession* s);
    bool   remove(uint64_t sessionId);

    bool   empty() const { return m_uMembers.empty(); }
    size_t size() const { return m_uMembers.size(); }
    bool   contains(uint64_t sessionId) const { return m_uMembers.count(sessionId) > 0; }

    /* 按 sessionId 有序，便于日志与成员列表输出稳定。 */
    const std::map<uint64_t, CSession*>& members() const { return m_uMembers; }

    /* 向房间内除 exceptId 之外的所有成员投递；跳过正在关闭的会话。 */
    void broadcast(const std::function<void(CSession&)>& send, uint64_t exceptId = 0) const;

    /* ---- 历史消息（内存环形缓冲，按条数与字节数双重上限）---- */
    void appendHistory(HistoryEntry_S entry, size_t maxEntries, size_t maxBytes);
    const std::deque<HistoryEntry_S>& history() const { return m_stHistory; }

    /* ---- 空房滞留 ---- */
    bool    isLingering() const { return m_uEmptySince != 0; }
    int64_t emptySince() const { return m_uEmptySince; }
    void    markEmpty(int64_t nowMs) { m_uEmptySince = nowMs; }
    void    markAlive() { m_uEmptySince = 0; }

private:
    std::string                 id_;
    std::map<uint64_t, CSession*> m_uMembers;
    std::deque<HistoryEntry_S>    m_stHistory;
    size_t                      m_uHistoryBytes = 0;
    int64_t                     m_uEmptySince = 0;
};

/* namespace dp */
}
