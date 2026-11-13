/*
 * 文件名:    ChatService.cpp
 * 创建日期:  2026-11-13
 * 作者:      JaTin
 * 功能描述:  文本消息业务的实现。
 * 修改记录:  2026-11-13 JaTin 按 C/C++ 编码规范整改注释与命名。
 */

#include "server/business/ChatService.h"

#include "server/core/Logger.h"
#include "server/core/TimeUtil.h"
#include "server/core/Validation.h"

namespace dp {

/*
 * 功能描述:  执行 opcodes 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * return:    返回值，含义见功能描述。
 */
std::vector<OpCode_EN> ChatService::opcodes() const {
    return {OpCode_EN::Chat};
}

/*
 * 功能描述:  执行 handle 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [out]  s  参数。
 * param [in]  op  参数。
 * param [in]  j  参数。
 * return:    void
 */
void ChatService::handle(CSession& s, OpCode_EN /*op*/, const Json& j) {
    if (!s.loggedIn()) {
        m_stCtx.sendError(s, "not_logged_in", "login first");
        return;
    }
    if (s.room().empty()) {
        m_stCtx.sendError(s, "not_in_room", "join a room before sending messages");
        return;
    }
    if (!j.contains("text") || !j["text"].is_string()) {
        m_stCtx.sendError(s, "bad_payload", "field 'text' (string) is required");
        return;
    }
    const std::string text = j["text"].get<std::string>();
    if (!validText(text)) {
        m_stCtx.sendError(s, "bad_text", "text must be 1..4000 bytes");
        return;
    }

    const std::string roomId = s.room();
    const uint64_t seq = m_stCtx.nextSeq();
    const int64_t  ts  = static_cast<int64_t>(nowMs());

    Json push = Json::object();
    push["seq"]  = seq;
    push["from"] = s.id();
    push["name"] = s.name();
    push["room"] = roomId;
    push["text"] = text;
    push["ts"]   = ts;

    /* 先落历史再广播：后进房的人一定能看到这条消息，不会出现「广播了但没存」的窗口。 */
    HistoryEntry_S entry;
    entry.seq  = seq;
    entry.from = s.id();
    entry.name = s.name();
    entry.text = text;
    entry.kind = HistoryKind_EN::Text;
    entry.ts   = ts;

    /* 持久化：先落库（进程重启不丢），再进内存缓冲（快速回放）。 */
    /* 落库失败不影响聊天，只记日志——消息服务不该被存储问题拖垮。 */
    std::string dbError;
    if (!m_stCtx.messages().append(roomId, entry, s.accountId(), &dbError)) {
        LOG_WARN("写入聊天记录失败：%s", dbError.c_str());
    }

    CRoom* room = m_stCtx.rooms().find(roomId);
    if (room != nullptr) {
        const ServerConfig_S& cfg = m_stCtx.config();
        room->appendHistory(std::move(entry), cfg.historyEntries, cfg.historyBytes);
    }

    /* 回显给发送者本人：客户端无需本地插入即可保证消息顺序与服务端一致。 */
    m_stCtx.broadcastRoom(roomId, OpCode_EN::ChatPush, push, 0);

    LOG_DEBUG("session#%llu chat %zu B in room '%s'",
              static_cast<unsigned long long>(s.id()), text.size(), roomId.c_str());
}

/* namespace dp */
}
