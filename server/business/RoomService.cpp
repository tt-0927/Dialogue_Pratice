/**
 * @file       RoomService.cpp
 * @brief      房间领域业务的实现。
 * @author     JaTin
 * @date       2026-11-13
 * @note       修改记录：2026-11-14 JaTin 统一为 Doxygen 风格注释。
 */

#include "server/business/RoomService.h"

#include <algorithm>
#include <vector>

#include "common/Base64.h"
#include "server/core/Logger.h"
#include "server/core/TimeUtil.h"
#include "server/core/Validation.h"

namespace dp {

/**
 * @brief      返回本业务服务所处理的协议操作码列表。
 * @note       本函数无输入参数。
 * @return     返回 vector<OpCode_EN> CRoomService:: 类型的处理结果。
 * @author     JaTin
 */
std::vector<OpCode_EN> CRoomService::opcodes() const {
    return {OpCode_EN::JoinRoom, OpCode_EN::LeaveRoom, OpCode_EN::ListRooms};
}

/**
 * @brief      按操作码分发并处理一条业务请求。
 * @param[out] stSession      目标会话对象；类型 CSession&。
 * @param[in] enOp           协议操作码；类型 OpCode_EN。
 * @param[in] stPayload      协议载荷内容；类型 const Json&。
 * @return     无返回值；处理结果通过成员状态或信号对外体现。
 * @author     JaTin
 */
void CRoomService::handle(CSession& stSession, OpCode_EN enOp, const Json& stPayload) {
    switch (enOp) {
        case OpCode_EN::JoinRoom:  onJoinRoom(stSession, stPayload);  break;
        case OpCode_EN::LeaveRoom: onLeaveRoom(stSession, stPayload); break;
        case OpCode_EN::ListRooms: onListRooms(stSession, stPayload); break;
        default:                                               break;
    }
}

/**
 * @brief      获取成员列表 JSON 对象。
 * @param[in] stRoom         房间号；类型 const CRoom&。
 * @return     返回 Json CRoomService:: 类型的处理结果。
 * @author     JaTin
 */
Json CRoomService::membersJson(const CRoom& stRoom) {
    Json stMembers = Json::array();
    for (const auto& stKv : stRoom.members()) {
        Json stM = Json::object();
        stM["id"] = stKv.second->id();
        stM["name"] = stKv.second->name();
        stMembers.push_back(std::move(stM));
    }
    return stMembers;
}

/**
 * @brief      加入 on 房间；实现上向目标会话或整个房间下发数据。
 * @param[out] stS            目标会话对象；类型 CSession&。
 * @param[in] stJ            输入数据；类型 const Json&。
 * @return     无返回值；处理结果通过成员状态或信号对外体现。
 * @author     JaTin
 */
void CRoomService::onJoinRoom(CSession& stS, const Json& stJ) {
    if (!stS.loggedIn()) {
        m_stCtx.sendError(stS, "not_logged_in", "login first");
        return;
    }
    if (!stJ.contains("room") || !stJ["room"].is_string()) {
        m_stCtx.sendError(stS, "bad_payload", "field 'room' (string) is required");
        return;
    }
    const std::string strRoomId = trim(stJ["room"].get<std::string>());
    if (!validRoomId(strRoomId)) {
        m_stCtx.sendError(stS, "bad_room", "room id must be 1..32 chars of [A-Za-z0-9_-]");
        return;
    }

    /**
     * @brief  已在目标房间：只回成员快照，不重复广播。
     */
    if (stS.room() == strRoomId) {
        CRoom* stRoom = m_stCtx.rooms().find(strRoomId);
        Json stState = Json::object();
        stState["room"] = strRoomId;
        stState["created"] = false;
        stState["members"] = (stRoom != nullptr) ? membersJson(*stRoom) : Json::array();
        m_stCtx.sendTo(stS, OpCode_EN::RoomState, stState);
        if (stRoom != nullptr) {
            sendHistory(stS, *stRoom);
        }
        return;
    }

    /**
     * @brief  切换房间：先离开旧房间并通知旧房友。
     */
    if (!stS.room().empty()) leaveRoom(stS, true);

    /**
     * @brief  房间不存在（或已空）则按房间号创建——服务端不做「房间白名单」限制。 空房可能仍在滞留期内，此时历史消息保留，created 仍报 true（房间里确实没人）。
     */
    CRoom* pExisting = m_stCtx.rooms().find(strRoomId);
    const bool nCreated = (pExisting == nullptr || pExisting->empty());
    CRoom& stRoom = m_stCtx.rooms().getOrCreate(strRoomId);
    stRoom.add(&stS);
    stS.setRoom(strRoomId);

    Json stState = Json::object();
    stState["room"] = strRoomId;
    stState["created"] = nCreated;
    stState["members"] = membersJson(stRoom);
    m_stCtx.sendTo(stS, OpCode_EN::RoomState, stState);

    /**
     * @brief  先给进房者补历史，再广播 join，客户端看到的消息顺序才完整。
     */
    sendHistory(stS, stRoom);

    Json stSys = Json::object();
    stSys["event"] = "join";
    stSys["room"] = strRoomId;
    stSys["userId"] = stS.id();
    stSys["name"] = stS.name();
    stSys["text"] = stS.name() + (nCreated ? " 创建并加入了房间 " : " 加入了房间 ") + strRoomId;
    stSys["ts"] = nowMs();
    m_stCtx.broadcastRoom(strRoomId, OpCode_EN::SystemMsg, stSys, stS.id());

    LOG_INFO("session#%llu ('%s') joined room '%s' (%s, members=%zu)",
             static_cast<unsigned long long>(stS.id()), stS.name().c_str(),
             strRoomId.c_str(), nCreated ? "created" : "existing", stRoom.size());
}

/**
 * @brief      退出 on 房间；实现上向目标会话或整个房间下发数据。
 * @param[out] stS            目标会话对象；类型 CSession&。
 * @return     无返回值；处理结果通过成员状态或信号对外体现。
 * @author     JaTin
 */
void CRoomService::onLeaveRoom(CSession& stS, const Json& /*j*/) {
    if (!stS.loggedIn()) {
        m_stCtx.sendError(stS, "not_logged_in", "login first");
        return;
    }
    if (stS.room().empty()) {
        m_stCtx.sendError(stS, "not_in_room", "you are not in a room");
        return;
    }

    const std::string strRoomId = stS.room();
    leaveRoom(stS, true);

    Json stSys = Json::object();
    stSys["event"] = "left";
    stSys["room"] = strRoomId;
    stSys["text"] = "已离开房间 " + strRoomId;
    stSys["ts"] = nowMs();
    m_stCtx.sendTo(stS, OpCode_EN::SystemMsg, stSys);
}

/**
 * @brief      执行 on 列表房间列表；实现上向目标会话或整个房间下发数据。
 * @param[out] stS            目标会话对象；类型 CSession&。
 * @return     无返回值；处理结果通过成员状态或信号对外体现。
 * @author     JaTin
 */
void CRoomService::onListRooms(CSession& stS, const Json& /*j*/) {
    if (!stS.loggedIn()) {
        m_stCtx.sendError(stS, "not_logged_in", "login first");
        return;
    }

    Json stList = Json::array();
    for (const auto& stKv : m_stCtx.rooms().all()) {
        /**
         * @brief  滞留中的空房不对外展示。
         */
        if (stKv.second->empty()) continue;
        Json stR = Json::object();
        stR["id"] = stKv.first;
        stR["count"] = stKv.second->size();
        stList.push_back(std::move(stR));
    }

    Json stResp = Json::object();
    stResp["rooms"] = stList;
    m_stCtx.sendTo(stS, OpCode_EN::RoomList, stResp);
}

/**
 * @brief      退出房间。
 * @param[out] stS            目标会话对象；类型 CSession&。
 * @param[in] nNotifyOthers  输入数据；类型 bool。
 * @return     无返回值；处理结果通过成员状态或信号对外体现。
 * @author     JaTin
 */
void CRoomService::leaveRoom(CSession& stS, bool nNotifyOthers) {
    if (stS.room().empty()) return;

    const std::string strRoomId = stS.room();
    const std::string strName = stS.name();
    const uint64_t uSid = stS.id();

    stS.clearRoom();

    CRoom* stRoom = m_stCtx.rooms().find(strRoomId);
    if (stRoom == nullptr) return;

    stRoom->remove(uSid);

    if (nNotifyOthers) {
        Json stSys = Json::object();
        stSys["event"] = "leave";
        stSys["room"] = strRoomId;
        stSys["userId"] = uSid;
        stSys["name"] = strName;
        stSys["text"] = strName + " 离开了房间";
        stSys["ts"] = nowMs();
        m_stCtx.broadcastRoom(strRoomId, OpCode_EN::SystemMsg, stSys, uSid);
    }

    if (stRoom->empty()) {
        const int nTtl = m_stCtx.config().roomTtlSec;
        if (nTtl <= 0) {
            m_stCtx.rooms().removeIfEmpty(strRoomId);
            LOG_INFO("room '%s' removed (empty)", strRoomId.c_str());
        } else {
            /**
             * @brief  滞留：保留房间与历史消息，方便断线重连的人回来还能看到上下文。
             */
            stRoom->markEmpty(static_cast<int64_t>(nowMs()));
            LOG_INFO("room '%s' is empty now, kept %ds for history", strRoomId.c_str(), nTtl);
        }
    }
}

/**
 * @brief      转换为条目 JSON 对象。
 * @param[in] stEntry        条目；类型 const HistoryEntry_S&。
 * @param[in] nWithAudio     输入数据；类型 bool。
 * @return     返回 Json CRoomService:: 类型的处理结果。
 * @author     JaTin
 */
Json CRoomService::entryToJson(const HistoryEntry_S& stEntry, bool nWithAudio) {
    Json stM = Json::object();
    stM["seq"] = stEntry.seq;
    stM["from"] = stEntry.from;
    stM["name"] = stEntry.name;
    stM["ts"] = stEntry.ts;

    if (stEntry.kind == HistoryKind_EN::Voice) {
        stM["kind"] = "voice";
        stM["durationMs"] = stEntry.durationMs;
        if (nWithAudio) stM["opus"] = base64Encode(stEntry.audio);
    } else {
        stM["kind"] = "text";
        stM["text"] = stEntry.text;
    }
    return stM;
}

/**
 * @brief      发送历史消息；实现上向目标会话或整个房间下发数据。
 * @param[out] stS            目标会话对象；类型 CSession&。
 * @param[out] stRoom         房间号；类型 CRoom&。
 * @return     无返回值；处理结果通过成员状态或信号对外体现。
 * @author     JaTin
 */
void CRoomService::sendHistory(CSession& stS, CRoom& stRoom) {
    const ServerConfig_S& stCfg = m_stCtx.config();

    /**
     * @brief  历史来源：优先内存环形缓冲（快），为空时回落到持久层。 内存为空只发生在服务端刚重启时——此时数据库是唯一的真相来源， 不补回来的话「聊天记录库」就变成只写不读，等于白存。
     */
    const auto& stInMemory = stRoom.history();

    std::vector<HistoryEntry_S> aFromDb;
    std::vector<const HistoryEntry_S*> pEntries;

    if (!stInMemory.empty()) {
        pEntries.reserve(stInMemory.size());
        for (const HistoryEntry_S& stEntry : stInMemory) pEntries.push_back(&stEntry);
    } else {
        aFromDb = m_stCtx.messages().loadRecent(stRoom.id(), static_cast<int>(stCfg.historySendLimit));
        pEntries.reserve(aFromDb.size());
        for (const HistoryEntry_S& stEntry : aFromDb) pEntries.push_back(&stEntry);
        if (!aFromDb.empty()) {
            LOG_INFO("房间 '%s' 内存无历史，从数据库补回 %zu 条",
                     stRoom.id().c_str(), aFromDb.size());
        }
    }

    if (pEntries.empty()) return;

    /**
     * @brief  语音条目一条就占 30~40 KB（Base64 后），如果和文本混在一个帧里， 几条语音就能撞上单帧 64 KiB 上限。所以：文本批量成帧，语音每条单独成帧。
     */
    Json stTextBatch = Json::array();
    std::vector<const HistoryEntry_S*> pVoices;
    size_t uBytes = 0;
    size_t uCount = 0;

    /**
     * @brief  从新到旧挑选，凑够条数/字节上限。
     */
    for (auto it = pEntries.rbegin(); it != pEntries.rend(); ++it) {
        const HistoryEntry_S& stEntry = **it;
        if (uCount >= stCfg.historySendLimit) break;

        if (stEntry.kind == HistoryKind_EN::Voice) {
            pVoices.push_back(&stEntry);
            ++uCount;
            continue;
        }

        const size_t uCost = stEntry.text.size() + stEntry.name.size() + 64;
        if (uBytes + uCost > stCfg.historySendBytes) break;
        uBytes += uCost;
        ++uCount;
        stTextBatch.push_back(entryToJson(stEntry, false));
    }

    if (!stTextBatch.empty()) {
        std::reverse(stTextBatch.begin(), stTextBatch.end());
        Json stResp = Json::object();
        stResp["room"] = stRoom.id();
        stResp["messages"] = stTextBatch;
        m_stCtx.sendTo(stS, OpCode_EN::HistoryResp, stResp);
    }

    /**
     * @brief  voices 里是「新 -> 旧」，反转成时间正序。
     */
    std::reverse(pVoices.begin(), pVoices.end());
    for (const HistoryEntry_S* stEntry : pVoices) {
        Json stOne = Json::array();
        stOne.push_back(entryToJson(*stEntry, true));
        Json stResp = Json::object();
        stResp["room"] = stRoom.id();
        stResp["messages"] = stOne;
        m_stCtx.sendTo(stS, OpCode_EN::HistoryResp, stResp);
    }

    LOG_DEBUG("session#%llu got %zu text + %zu voice history of room '%s'",
              static_cast<unsigned long long>(stS.id()), stTextBatch.size(), pVoices.size(),
              stRoom.id().c_str());
}

/**
 * @brief  namespace dp。
 */
}
