/*
 * 文件名:    VoiceService.cpp
 * 创建日期:  2026-11-13
 * 作者:      JaTin
 * 功能描述:  媒体层语音消息的实现。
 * 修改记录:  2026-11-13 JaTin 按 C/C++ 编码规范整改注释与命名。
 */

#include "server/media/VoiceService.h"

#include "common/Base64.h"
#include "server/core/Logger.h"
#include "server/core/TimeUtil.h"

namespace dp {

/*
 * 功能描述:  执行 opcodes 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * return:    返回值，含义见功能描述。
 */
std::vector<OpCode_EN> CVoiceService::opcodes() const {
    return {OpCode_EN::Voice};
}

/*
 * 功能描述:  执行 handle 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [out]  s  参数。
 * param [in]  op  参数。
 * param [in]  j  参数。
 * return:    void
 */
void CVoiceService::handle(CSession& s, OpCode_EN /*op*/, const Json& j) {
    if (!s.loggedIn()) {
        m_stCtx.sendError(s, "not_logged_in", "login first");
        return;
    }
    if (s.room().empty()) {
        m_stCtx.sendError(s, "not_in_room", "join a room before sending voice");
        return;
    }
    if (!j.contains("opus") || !j["opus"].is_string()) {
        m_stCtx.sendError(s, "bad_payload", "field 'opus' (string) is required");
        return;
    }

    const uint32_t durationMs = j.value("durationMs", 0u);
    if (durationMs == 0 || durationMs > kMaxVoiceDurationMs) {
        m_stCtx.sendError(s, "bad_voice",
                       "durationMs must be 1.." + std::to_string(kMaxVoiceDurationMs));
        return;
    }

    const std::string& base64 = j["opus"].get_ref<const std::string&>();
    std::string audio = base64Decode(base64);
    if (audio.size() < 8 || audio.size() > kMaxVoiceBytes) {
        m_stCtx.sendError(s, "bad_voice", "voice payload size out of range");
        return;
    }

    const std::string roomId = s.room();
    const uint64_t seq = m_stCtx.nextSeq();
    const int64_t  ts  = static_cast<int64_t>(nowMs());

    Json push = Json::object();
    push["seq"]        = seq;
    push["from"]       = s.id();
    push["name"]       = s.name();
    push["room"]       = roomId;
    push["durationMs"] = durationMs;
    /* 原样转发，省一次重新编码 */
    push["opus"]       = base64;
    push["ts"]         = ts;

    /* 与文本一致：先落历史再广播，回显给发送者本人。 */
    HistoryEntry_S entry;
    entry.seq        = seq;
    entry.from       = s.id();
    entry.name       = s.name();
    entry.audio      = std::move(audio);
    entry.durationMs = durationMs;
    entry.kind       = HistoryKind_EN::Voice;
    entry.ts         = ts;

    /* 持久化：先落库再进内存缓冲（同文本消息的处理） */
    std::string dbError;
    if (!m_stCtx.messages().append(roomId, entry, s.accountId(), &dbError)) {
        LOG_WARN("写入语音记录失败：%s", dbError.c_str());
    }

    CRoom* room = m_stCtx.rooms().find(roomId);
    if (room != nullptr) {
        const ServerConfig_S& cfg = m_stCtx.config();
        room->appendHistory(std::move(entry), cfg.historyEntries, cfg.historyBytes);
    }

    m_stCtx.broadcastRoom(roomId, OpCode_EN::VoicePush, push, 0);

    LOG_INFO("session#%llu voice %ums in room '%s'",
             static_cast<unsigned long long>(s.id()), durationMs, roomId.c_str());
}

/* namespace dp */
}
