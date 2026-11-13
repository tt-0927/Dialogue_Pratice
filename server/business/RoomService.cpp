/*
 * 文件名:    RoomService.cpp
 * 创建日期:  2026-11-13
 * 作者:      JaTin
 * 功能描述:  房间领域业务的实现。
 * 修改记录:  2026-11-13 JaTin 按 C/C++ 编码规范整改注释与命名。
 */

#include "server/business/RoomService.h"

#include <algorithm>
#include <vector>

#include "common/Base64.h"
#include "server/core/Logger.h"
#include "server/core/TimeUtil.h"
#include "server/core/Validation.h"

namespace dp {

/*
 * 功能描述:  执行 opcodes 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * return:    返回值，含义见功能描述。
 */
std::vector<OpCode_EN> CRoomService::opcodes() const {
    return {OpCode_EN::JoinRoom, OpCode_EN::LeaveRoom, OpCode_EN::ListRooms};
}

/*
 * 功能描述:  执行 handle 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [out]  session  参数。
 * param [in]  op  参数。
 * param [in]  payload  参数。
 * return:    void
 */
void CRoomService::handle(CSession& session, OpCode_EN op, const Json& payload) {
    switch (op) {
        case OpCode_EN::JoinRoom:  onJoinRoom(session, payload);  break;
        case OpCode_EN::LeaveRoom: onLeaveRoom(session, payload); break;
        case OpCode_EN::ListRooms: onListRooms(session, payload); break;
        default:                                               break;
    }
}

/*
 * 功能描述:  执行 membersJson 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [in]  room  参数。
 * return:    返回值，含义见功能描述。
 */
Json CRoomService::membersJson(const CRoom& room) {
    Json members = Json::array();
    for (const auto& kv : room.members()) {
        Json m = Json::object();
        m["id"] = kv.second->id();
        m["name"] = kv.second->name();
        members.push_back(std::move(m));
    }
    return members;
}

/*
 * 功能描述:  执行 onJoinRoom 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [out]  s  参数。
 * param [in]  j  参数。
 * return:    void
 */
void CRoomService::onJoinRoom(CSession& s, const Json& j) {
    if (!s.loggedIn()) {
        m_stCtx.sendError(s, "not_logged_in", "login first");
        return;
    }
    if (!j.contains("room") || !j["room"].is_string()) {
        m_stCtx.sendError(s, "bad_payload", "field 'room' (string) is required");
        return;
    }
    const std::string roomId = trim(j["room"].get<std::string>());
    if (!validRoomId(roomId)) {
        m_stCtx.sendError(s, "bad_room", "room id must be 1..32 chars of [A-Za-z0-9_-]");
        return;
    }

    /* 已在目标房间：只回成员快照，不重复广播。 */
    if (s.room() == roomId) {
        CRoom* room = m_stCtx.rooms().find(roomId);
        Json state = Json::object();
        state["room"] = roomId;
        state["created"] = false;
        state["members"] = (room != nullptr) ? membersJson(*room) : Json::array();
        m_stCtx.sendTo(s, OpCode_EN::RoomState, state);
        if (room != nullptr) {
            sendHistory(s, *room);
        }
        return;
    }

    /* 切换房间：先离开旧房间并通知旧房友。 */
    if (!s.room().empty()) leaveRoom(s, true);

    /* 房间不存在（或已空）则按房间号创建——服务端不做「房间白名单」限制。 */
    /* 空房可能仍在滞留期内，此时历史消息保留，created 仍报 true（房间里确实没人）。 */
    CRoom* existing = m_stCtx.rooms().find(roomId);
    const bool created = (existing == nullptr || existing->empty());
    CRoom& room = m_stCtx.rooms().getOrCreate(roomId);
    room.add(&s);
    s.setRoom(roomId);

    Json state = Json::object();
    state["room"] = roomId;
    state["created"] = created;
    state["members"] = membersJson(room);
    m_stCtx.sendTo(s, OpCode_EN::RoomState, state);

    /* 先给进房者补历史，再广播 join，客户端看到的消息顺序才完整。 */
    sendHistory(s, room);

    Json sys = Json::object();
    sys["event"] = "join";
    sys["room"] = roomId;
    sys["userId"] = s.id();
    sys["name"] = s.name();
    sys["text"] = s.name() + (created ? " 创建并加入了房间 " : " 加入了房间 ") + roomId;
    sys["ts"] = nowMs();
    m_stCtx.broadcastRoom(roomId, OpCode_EN::SystemMsg, sys, s.id());

    LOG_INFO("session#%llu ('%s') joined room '%s' (%s, members=%zu)",
             static_cast<unsigned long long>(s.id()), s.name().c_str(),
             roomId.c_str(), created ? "created" : "existing", room.size());
}

/*
 * 功能描述:  执行 onLeaveRoom 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [out]  s  参数。
 * param [in]  j  参数。
 * return:    void
 */
void CRoomService::onLeaveRoom(CSession& s, const Json& /*j*/) {
    if (!s.loggedIn()) {
        m_stCtx.sendError(s, "not_logged_in", "login first");
        return;
    }
    if (s.room().empty()) {
        m_stCtx.sendError(s, "not_in_room", "you are not in a room");
        return;
    }

    const std::string roomId = s.room();
    leaveRoom(s, true);

    Json sys = Json::object();
    sys["event"] = "left";
    sys["room"] = roomId;
    sys["text"] = "已离开房间 " + roomId;
    sys["ts"] = nowMs();
    m_stCtx.sendTo(s, OpCode_EN::SystemMsg, sys);
}

/*
 * 功能描述:  执行 onListRooms 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [out]  s  参数。
 * param [in]  j  参数。
 * return:    void
 */
void CRoomService::onListRooms(CSession& s, const Json& /*j*/) {
    if (!s.loggedIn()) {
        m_stCtx.sendError(s, "not_logged_in", "login first");
        return;
    }

    Json list = Json::array();
    for (const auto& kv : m_stCtx.rooms().all()) {
        /* 滞留中的空房不对外展示 */
        if (kv.second->empty()) continue;
        Json r = Json::object();
        r["id"] = kv.first;
        r["count"] = kv.second->size();
        list.push_back(std::move(r));
    }

    Json resp = Json::object();
    resp["rooms"] = list;
    m_stCtx.sendTo(s, OpCode_EN::RoomList, resp);
}

/*
 * 功能描述:  执行 leaveRoom 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [out]  s  参数。
 * param [in]  notifyOthers  参数。
 * return:    void
 */
void CRoomService::leaveRoom(CSession& s, bool notifyOthers) {
    if (s.room().empty()) return;

    const std::string roomId = s.room();
    const std::string name = s.name();
    const uint64_t sid = s.id();

    s.clearRoom();

    CRoom* room = m_stCtx.rooms().find(roomId);
    if (room == nullptr) return;

    room->remove(sid);

    if (notifyOthers) {
        Json sys = Json::object();
        sys["event"] = "leave";
        sys["room"] = roomId;
        sys["userId"] = sid;
        sys["name"] = name;
        sys["text"] = name + " 离开了房间";
        sys["ts"] = nowMs();
        m_stCtx.broadcastRoom(roomId, OpCode_EN::SystemMsg, sys, sid);
    }

    if (room->empty()) {
        const int ttl = m_stCtx.config().roomTtlSec;
        if (ttl <= 0) {
            m_stCtx.rooms().removeIfEmpty(roomId);
            LOG_INFO("room '%s' removed (empty)", roomId.c_str());
        } else {
            /* 滞留：保留房间与历史消息，方便断线重连的人回来还能看到上下文。 */
            room->markEmpty(static_cast<int64_t>(nowMs()));
            LOG_INFO("room '%s' is empty now, kept %ds for history", roomId.c_str(), ttl);
        }
    }
}

/*
 * 功能描述:  执行 entryToJson 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [in]  entry  参数。
 * param [in]  withAudio  参数。
 * return:    返回值，含义见功能描述。
 */
Json CRoomService::entryToJson(const HistoryEntry_S& entry, bool withAudio) {
    Json m = Json::object();
    m["seq"] = entry.seq;
    m["from"] = entry.from;
    m["name"] = entry.name;
    m["ts"] = entry.ts;

    if (entry.kind == HistoryKind_EN::Voice) {
        m["kind"] = "voice";
        m["durationMs"] = entry.durationMs;
        if (withAudio) m["opus"] = base64Encode(entry.audio);
    } else {
        m["kind"] = "text";
        m["text"] = entry.text;
    }
    return m;
}

/*
 * 功能描述:  执行 sendHistory 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [out]  s  参数。
 * param [out]  room  参数。
 * return:    void
 */
void CRoomService::sendHistory(CSession& s, CRoom& room) {
    const ServerConfig_S& cfg = m_stCtx.config();

    /* 历史来源：优先内存环形缓冲（快），为空时回落到持久层。 */
    /* 内存为空只发生在服务端刚重启时——此时数据库是唯一的真相来源， */
    /* 不补回来的话「聊天记录库」就变成只写不读，等于白存。 */
    const auto& inMemory = room.history();

    std::vector<HistoryEntry_S> fromDb;
    std::vector<const HistoryEntry_S*> entries;

    if (!inMemory.empty()) {
        entries.reserve(inMemory.size());
        for (const HistoryEntry_S& entry : inMemory) entries.push_back(&entry);
    } else {
        fromDb = m_stCtx.messages().loadRecent(room.id(), static_cast<int>(cfg.historySendLimit));
        entries.reserve(fromDb.size());
        for (const HistoryEntry_S& entry : fromDb) entries.push_back(&entry);
        if (!fromDb.empty()) {
            LOG_INFO("房间 '%s' 内存无历史，从数据库补回 %zu 条",
                     room.id().c_str(), fromDb.size());
        }
    }

    if (entries.empty()) return;

    /* 语音条目一条就占 30~40 KB（Base64 后），如果和文本混在一个帧里， */
    /* 几条语音就能撞上单帧 64 KiB 上限。所以：文本批量成帧，语音每条单独成帧。 */
    Json textBatch = Json::array();
    std::vector<const HistoryEntry_S*> voices;
    size_t bytes = 0;
    size_t count = 0;

    /* 从新到旧挑选，凑够条数/字节上限 */
    for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
        const HistoryEntry_S& entry = **it;
        if (count >= cfg.historySendLimit) break;

        if (entry.kind == HistoryKind_EN::Voice) {
            voices.push_back(&entry);
            ++count;
            continue;
        }

        const size_t cost = entry.text.size() + entry.name.size() + 64;
        if (bytes + cost > cfg.historySendBytes) break;
        bytes += cost;
        ++count;
        textBatch.push_back(entryToJson(entry, false));
    }

    if (!textBatch.empty()) {
        std::reverse(textBatch.begin(), textBatch.end());
        Json resp = Json::object();
        resp["room"] = room.id();
        resp["messages"] = textBatch;
        m_stCtx.sendTo(s, OpCode_EN::HistoryResp, resp);
    }

    /* voices 里是「新 -> 旧」，反转成时间正序 */
    std::reverse(voices.begin(), voices.end());
    for (const HistoryEntry_S* entry : voices) {
        Json one = Json::array();
        one.push_back(entryToJson(*entry, true));
        Json resp = Json::object();
        resp["room"] = room.id();
        resp["messages"] = one;
        m_stCtx.sendTo(s, OpCode_EN::HistoryResp, resp);
    }

    LOG_DEBUG("session#%llu got %zu text + %zu voice history of room '%s'",
              static_cast<unsigned long long>(s.id()), textBatch.size(), voices.size(),
              room.id().c_str());
}

/* namespace dp */
}
