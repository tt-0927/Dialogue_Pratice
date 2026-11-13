/**
 * @file  VoiceService.cpp
 * @brief 媒体层语音消息的实现。
 */

#include "server/media/VoiceService.h"

#include "common/Base64.h"
#include "server/core/Logger.h"
#include "server/core/TimeUtil.h"

namespace dp {

std::vector<OpCode> VoiceService::opcodes() const {
    return {OpCode::Voice};
}

void VoiceService::handle(Session& s, OpCode /*op*/, const Json& j) {
    if (!s.loggedIn()) {
        ctx_.sendError(s, "not_logged_in", "login first");
        return;
    }
    if (s.room().empty()) {
        ctx_.sendError(s, "not_in_room", "join a room before sending voice");
        return;
    }
    if (!j.contains("opus") || !j["opus"].is_string()) {
        ctx_.sendError(s, "bad_payload", "field 'opus' (string) is required");
        return;
    }

    const uint32_t durationMs = j.value("durationMs", 0u);
    if (durationMs == 0 || durationMs > kMaxVoiceDurationMs) {
        ctx_.sendError(s, "bad_voice",
                       "durationMs must be 1.." + std::to_string(kMaxVoiceDurationMs));
        return;
    }

    const std::string& base64 = j["opus"].get_ref<const std::string&>();
    std::string audio = base64Decode(base64);
    if (audio.size() < 8 || audio.size() > kMaxVoiceBytes) {
        ctx_.sendError(s, "bad_voice", "voice payload size out of range");
        return;
    }

    const std::string roomId = s.room();
    const uint64_t seq = ctx_.nextSeq();
    const int64_t  ts  = static_cast<int64_t>(nowMs());

    Json push = Json::object();
    push["seq"]        = seq;
    push["from"]       = s.id();
    push["name"]       = s.name();
    push["room"]       = roomId;
    push["durationMs"] = durationMs;
    push["opus"]       = base64;   // 原样转发，省一次重新编码
    push["ts"]         = ts;

    // 与文本一致：先落历史再广播，回显给发送者本人。
    HistoryEntry entry;
    entry.seq        = seq;
    entry.from       = s.id();
    entry.name       = s.name();
    entry.audio      = std::move(audio);
    entry.durationMs = durationMs;
    entry.kind       = HistoryKind::Voice;
    entry.ts         = ts;

    // 持久化：先落库再进内存缓冲（同文本消息的处理）
    std::string dbError;
    if (!ctx_.messages().append(roomId, entry, s.accountId(), &dbError)) {
        LOG_WARN("写入语音记录失败：%s", dbError.c_str());
    }

    Room* room = ctx_.rooms().find(roomId);
    if (room != nullptr) {
        const ServerConfig& cfg = ctx_.config();
        room->appendHistory(std::move(entry), cfg.historyEntries, cfg.historyBytes);
    }

    ctx_.broadcastRoom(roomId, OpCode::VoicePush, push, 0);

    LOG_INFO("session#%llu voice %ums in room '%s'",
             static_cast<unsigned long long>(s.id()), durationMs, roomId.c_str());
}

}  // namespace dp
