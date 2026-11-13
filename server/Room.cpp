// server/Room.cpp
#include "server/Room.h"

namespace dp {
namespace {

/// 每条消息除正文外的固定开销估算（seq/from/name/ts/JSON 标点）。
constexpr size_t kEntryOverhead = 48;

}  // namespace

bool Room::add(Session* s) {
    if (s == nullptr) return false;
    emptySince_ = 0;
    return members_.emplace(s->id(), s).second;
}

bool Room::remove(uint64_t sessionId) {
    return members_.erase(sessionId) > 0;
}

void Room::broadcast(const std::function<void(Session&)>& send, uint64_t exceptId) const {
    for (const auto& kv : members_) {
        Session* s = kv.second;
        if (s == nullptr || s->id() == exceptId) continue;
        if (s->state() == SessionState::Closing) continue;
        send(*s);
    }
}

void Room::appendHistory(HistoryEntry entry, size_t maxEntries, size_t maxBytes) {
    // 语音条目的 audio 也要计入字节预算，否则一条 40 KB 的语音能把缓冲撑爆
    const auto costOf = [](const HistoryEntry& e) {
        return e.payloadBytes() + e.name.size() + kEntryOverhead;
    };

    historyBytes_ += costOf(entry);
    history_.push_back(std::move(entry));

    // 双重上限：条数优先，其次按字节淘汰最旧的。
    while (history_.size() > maxEntries) {
        historyBytes_ -= costOf(history_.front());
        history_.pop_front();
    }
    while (historyBytes_ > maxBytes && history_.size() > 1) {
        historyBytes_ -= costOf(history_.front());
        history_.pop_front();
    }
}

}  // namespace dp
