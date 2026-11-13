// server/Room.h
//
// 房间：一组 Session 的集合 + 最近消息环形缓冲。
// 房间由「房间号」字符串唯一标识；成员走空后不立即销毁，而是滞留一段时间
// （保留历史消息），超时才回收——见 Server::handleTimer()。
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <string>

#include "server/Session.h"

namespace dp {

/// 历史条目类型。语音条目带音频数据，文本条目只有 text。
enum class HistoryKind : uint8_t {
    Text  = 0,
    Voice = 1,
};

/// 一条历史消息。seq 为服务端全局单调序号，用于客户端去重与补漏。
struct HistoryEntry {
    uint64_t    seq = 0;
    uint64_t    from = 0;
    std::string name;
    std::string text;          // 文本消息正文；语音消息为空
    std::string audio;         // 语音消息的 DPV1 容器（二进制）；文本消息为空
    uint32_t    durationMs = 0;
    HistoryKind kind = HistoryKind::Text;
    int64_t     ts = 0;        // Unix 毫秒

    size_t payloadBytes() const { return text.size() + audio.size(); }
};

class Room {
public:
    explicit Room(std::string id) : id_(std::move(id)) {}

    const std::string& id() const { return id_; }

    bool   add(Session* s);
    bool   remove(uint64_t sessionId);

    bool   empty() const { return members_.empty(); }
    size_t size() const { return members_.size(); }
    bool   contains(uint64_t sessionId) const { return members_.count(sessionId) > 0; }

    /// 按 sessionId 有序，便于日志与成员列表输出稳定。
    const std::map<uint64_t, Session*>& members() const { return members_; }

    /// 向房间内除 exceptId 之外的所有成员投递；跳过正在关闭的会话。
    void broadcast(const std::function<void(Session&)>& send, uint64_t exceptId = 0) const;

    // ---- 历史消息（内存环形缓冲，按条数与字节数双重上限）----
    void appendHistory(HistoryEntry entry, size_t maxEntries, size_t maxBytes);
    const std::deque<HistoryEntry>& history() const { return history_; }

    // ---- 空房滞留 ----
    bool    isLingering() const { return emptySince_ != 0; }
    int64_t emptySince() const { return emptySince_; }
    void    markEmpty(int64_t nowMs) { emptySince_ = nowMs; }
    void    markAlive() { emptySince_ = 0; }

private:
    std::string                 id_;
    std::map<uint64_t, Session*> members_;
    std::deque<HistoryEntry>    history_;
    size_t                      historyBytes_ = 0;
    int64_t                     emptySince_ = 0;
};

}  // namespace dp
