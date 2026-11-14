/**
 * @file       VoiceService.cpp
 * @brief      媒体层语音消息的实现。
 * @author     JaTin
 * @date       2026-11-13
 * @note       修改记录：2026-11-14 JaTin 统一为 Doxygen 风格注释。
 */

#include "server/media/VoiceService.h"

#include "common/Base64.h"
#include "server/core/Logger.h"
#include "server/core/TimeUtil.h"

namespace dp {

/**
 * @brief      返回本业务服务所处理的协议操作码列表。
 * @note       本函数无输入参数。
 * @return     返回 vector<OpCode_EN> CVoiceService:: 类型的处理结果。
 * @author     JaTin
 */
std::vector<OpCode_EN> CVoiceService::opcodes() const {
    return {OpCode_EN::Voice};
}

/**
 * @brief      按操作码分发并处理一条业务请求。
 * @param[out] stS            目标会话对象；类型 CSession&。
 * @param[in] stJ            输入数据；类型 const Json&。
 * @return     无返回值；处理结果通过成员状态或信号对外体现。
 * @author     JaTin
 */
void CVoiceService::handle(CSession& stS, OpCode_EN /*op*/, const Json& stJ) {
    if (!stS.loggedIn()) {
        m_stCtx.sendError(stS, "not_logged_in", "login first");
        return;
    }
    if (stS.room().empty()) {
        m_stCtx.sendError(stS, "not_in_room", "join a room before sending voice");
        return;
    }
    if (!stJ.contains("opus") || !stJ["opus"].is_string()) {
        m_stCtx.sendError(stS, "bad_payload", "field 'opus' (string) is required");
        return;
    }

    const uint32_t uDurationMs = stJ.value("durationMs", 0u);
    if (uDurationMs == 0 || uDurationMs > kMaxVoiceDurationMs) {
        m_stCtx.sendError(stS, "bad_voice",
                       "durationMs must be 1.." + std::to_string(kMaxVoiceDurationMs));
        return;
    }

    const std::string& strBase64 = stJ["opus"].get_ref<const std::string&>();
    std::string strAudio = base64Decode(strBase64);
    if (strAudio.size() < 8 || strAudio.size() > kMaxVoiceBytes) {
        m_stCtx.sendError(stS, "bad_voice", "voice payload size out of range");
        return;
    }

    const std::string strRoomId = stS.room();
    const uint64_t uSeq = m_stCtx.nextSeq();
    const int64_t  lTs  = static_cast<int64_t>(nowMs());

    Json stPush = Json::object();
    stPush["seq"]        = uSeq;
    stPush["from"]       = stS.id();
    stPush["name"]       = stS.name();
    stPush["room"]       = strRoomId;
    stPush["durationMs"] = uDurationMs;
    /**
     * @brief  原样转发，省一次重新编码。
     */
    stPush["opus"]       = strBase64;
    stPush["ts"]         = lTs;

    /**
     * @brief  与文本一致：先落历史再广播，回显给发送者本人。
     */
    HistoryEntry_S stEntry;
    stEntry.seq        = uSeq;
    stEntry.from       = stS.id();
    stEntry.name       = stS.name();
    stEntry.audio      = std::move(strAudio);
    stEntry.durationMs = uDurationMs;
    stEntry.kind       = HistoryKind_EN::Voice;
    stEntry.ts         = lTs;

    /**
     * @brief  持久化：先落库再进内存缓冲（同文本消息的处理）。
     */
    std::string strDbError;
    if (!m_stCtx.messages().append(strRoomId, stEntry, stS.accountId(), &strDbError)) {
        LOG_WARN("写入语音记录失败：%s", strDbError.c_str());
    }

    CRoom* pRoom = m_stCtx.rooms().find(strRoomId);
    if (pRoom != nullptr) {
        const ServerConfig_S& stCfg = m_stCtx.config();
        pRoom->appendHistory(std::move(stEntry), stCfg.historyEntries, stCfg.historyBytes);
    }

    m_stCtx.broadcastRoom(strRoomId, OpCode_EN::VoicePush, stPush, 0);

    LOG_INFO("session#%llu voice %ums in room '%s'",
             static_cast<unsigned long long>(stS.id()), uDurationMs, strRoomId.c_str());
}

/**
 * @brief  namespace dp。
 */
}
