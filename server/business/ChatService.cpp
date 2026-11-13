/**
 * @file  ChatService.cpp
 * @brief 文本消息业务的实现。
 */

#include "server/business/ChatService.h"

#include "server/core/Logger.h"
#include "server/core/TimeUtil.h"
#include "server/core/Validation.h"

namespace dp {

std::vector<OpCode> ChatService::opcodes() const {
    return {OpCode::Chat};
}

void ChatService::handle(Session& s, OpCode /*op*/, const Json& j) {
    if (!s.loggedIn()) {
        ctx_.sendError(s, "not_logged_in", "login first");
        return;
    }
    if (s.room().empty()) {
        ctx_.sendError(s, "not_in_room", "join a room before sending messages");
        return;
    }
    if (!j.contains("text") || !j["text"].is_string()) {
        ctx_.sendError(s, "bad_payload", "field 'text' (string) is required");
        return;
    }
    const std::string text = j["text"].get<std::string>();
    if (!validText(text)) {
        ctx_.sendError(s, "bad_text", "text must be 1..4000 bytes");
        return;
    }

    const std::string roomId = s.room();
    const uint64_t seq = ctx_.nextSeq();
    const int64_t  ts  = static_cast<int64_t>(nowMs());

    Json push = Json::object();
    push["seq"]  = seq;
    push["from"] = s.id();
    push["name"] = s.name();
    push["room"] = roomId;
    push["text"] = text;
    push["ts"]   = ts;

    // 先落历史再广播：后进房的人一定能看到这条消息，不会出现「广播了但没存」的窗口。
    HistoryEntry entry;
    entry.seq  = seq;
    entry.from = s.id();
    entry.name = s.name();
    entry.text = text;
    entry.kind = HistoryKind::Text;
    entry.ts   = ts;

    // 持久化：先落库（进程重启不丢），再进内存缓冲（快速回放）。
    // 落库失败不影响聊天，只记日志——消息服务不该被存储问题拖垮。
    std::string dbError;
    if (!ctx_.messages().append(roomId, entry, s.accountId(), &dbError)) {
        LOG_WARN("写入聊天记录失败：%s", dbError.c_str());
    }

    Room* room = ctx_.rooms().find(roomId);
    if (room != nullptr) {
        const ServerConfig& cfg = ctx_.config();
        room->appendHistory(std::move(entry), cfg.historyEntries, cfg.historyBytes);
    }

    // 回显给发送者本人：客户端无需本地插入即可保证消息顺序与服务端一致。
    ctx_.broadcastRoom(roomId, OpCode::ChatPush, push, 0);

    LOG_DEBUG("session#%llu chat %zu B in room '%s'",
              static_cast<unsigned long long>(s.id()), text.size(), roomId.c_str());
}

}  // namespace dp
