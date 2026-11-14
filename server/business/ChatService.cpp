/**
 * @file       ChatService.cpp
 * @brief      文本消息业务的实现。
 * @author     JaTin
 * @date       2026-11-13
 * @note       修改记录：2026-11-14 JaTin 统一为 Doxygen 风格注释。
 */

#include "server/business/ChatService.h"

#include "server/core/Logger.h"
#include "server/core/TimeUtil.h"
#include "server/core/Validation.h"

namespace dp {

/**
 * @brief      返回本业务服务所处理的协议操作码列表。
 * @note       本函数无输入参数。
 * @return     返回 vector<OpCode_EN> ChatService:: 类型的处理结果。
 * @author     JaTin
 */
std::vector<OpCode_EN> ChatService::opcodes() const {
    return {OpCode_EN::Chat};
}

/**
 * @brief      按操作码分发并处理一条业务请求。
 * @param[out] stS            目标会话对象；类型 CSession&。
 * @param[in] stJ            输入数据；类型 const Json&。
 * @return     无返回值；处理结果通过成员状态或信号对外体现。
 * @author     JaTin
 */
void ChatService::handle(CSession& stS, OpCode_EN /*op*/, const Json& stJ) {
    if (!stS.loggedIn()) {
        m_stCtx.sendError(stS, "not_logged_in", "login first");
        return;
    }
    if (stS.room().empty()) {
        m_stCtx.sendError(stS, "not_in_room", "join a room before sending messages");
        return;
    }
    if (!stJ.contains("text") || !stJ["text"].is_string()) {
        m_stCtx.sendError(stS, "bad_payload", "field 'text' (string) is required");
        return;
    }
    const std::string strText = stJ["text"].get<std::string>();
    if (!validText(strText)) {
        m_stCtx.sendError(stS, "bad_text", "text must be 1..4000 bytes");
        return;
    }

    const std::string strRoomId = stS.room();
    const uint64_t uSeq = m_stCtx.nextSeq();
    const int64_t  lTs  = static_cast<int64_t>(nowMs());

    Json stPush = Json::object();
    stPush["seq"]  = uSeq;
    stPush["from"] = stS.id();
    stPush["name"] = stS.name();
    stPush["room"] = strRoomId;
    stPush["text"] = strText;
    stPush["ts"]   = lTs;

    /**
     * @brief  先落历史再广播：后进房的人一定能看到这条消息，不会出现「广播了但没存」的窗口。
     */
    HistoryEntry_S stEntry;
    stEntry.seq  = uSeq;
    stEntry.from = stS.id();
    stEntry.name = stS.name();
    stEntry.text = strText;
    stEntry.kind = HistoryKind_EN::Text;
    stEntry.ts   = lTs;

    /**
     * @brief  持久化：先落库（进程重启不丢），再进内存缓冲（快速回放）。 落库失败不影响聊天，只记日志——消息服务不该被存储问题拖垮。
     */
    std::string strDbError;
    if (!m_stCtx.messages().append(strRoomId, stEntry, stS.accountId(), &strDbError)) {
        LOG_WARN("写入聊天记录失败：%s", strDbError.c_str());
    }

    CRoom* pRoom = m_stCtx.rooms().find(strRoomId);
    if (pRoom != nullptr) {
        const ServerConfig_S& stCfg = m_stCtx.config();
        pRoom->appendHistory(std::move(stEntry), stCfg.historyEntries, stCfg.historyBytes);
    }

    /**
     * @brief  回显给发送者本人：客户端无需本地插入即可保证消息顺序与服务端一致。
     */
    m_stCtx.broadcastRoom(strRoomId, OpCode_EN::ChatPush, stPush, 0);

    LOG_DEBUG("session#%llu chat %zu B in room '%s'",
              static_cast<unsigned long long>(stS.id()), strText.size(), strRoomId.c_str());
}

/**
 * @brief  namespace dp。
 */
}
