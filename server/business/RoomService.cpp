/**
 * @file  RoomService.cpp
 * @brief 房间领域业务的实现。
 */

#include "server/business/RoomService.h"

#include <algorithm>
#include <vector>

#include "common/Base64.h"
#include "server/core/Logger.h"
#include "server/core/TimeUtil.h"
#include "server/core/Validation.h"

namespace dp {

std::vector<OpCode> RoomService::opcodes() const {
    return {OpCode::JoinRoom, OpCode::LeaveRoom, OpCode::ListRooms};
}

void RoomService::handle(Session& session, OpCode op, const Json& payload) {
    switch (op) {
        case OpCode::JoinRoom:  onJoinRoom(session, payload);  break;
        case OpCode::LeaveRoom: onLeaveRoom(session, payload); break;
        case OpCode::ListRooms: onListRooms(session, payload); break;
        default:                                               break;
    }
}

Json RoomService::membersJson(const Room& room) {
    Json members = Json::array();
    for (const auto& kv : room.members()) {
        Json m = Json::object();
        m["id"] = kv.second->id();
        m["name"] = kv.second->name();
        members.push_back(std::move(m));
    }
    return members;
}

void RoomService::onJoinRoom(Session& s, const Json& j) {
    if (!s.loggedIn()) {
        ctx_.sendError(s, "not_logged_in", "login first");
        return;
    }
    if (!j.contains("room") || !j["room"].is_string()) {
        ctx_.sendError(s, "bad_payload", "field 'room' (string) is required");
        return;
    }
    const std::string roomId = trim(j["room"].get<std::string>());
    if (!validRoomId(roomId)) {
        ctx_.sendError(s, "bad_room", "room id must be 1..32 chars of [A-Za-z0-9_-]");
        return;
    }

    // 已在目标房间：只回成员快照，不重复广播。
    if (s.room() == roomId) {
        Room* room = ctx_.rooms().find(roomId);
        Json state = Json::object();
        state["room"] = roomId;
        state["created"] = false;
        state["members"] = (room != nullptr) ? membersJson(*room) : Json::array();
        ctx_.sendTo(s, OpCode::RoomState, state);
        if (room != nullptr) {
            sendHistory(s, *room);
        }
        return;
    }

    // 切换房间：先离开旧房间并通知旧房友。
    if (!s.room().empty()) leaveRoom(s, true);

    // 房间不存在（或已空）则按房间号创建——服务端不做「房间白名单」限制。
    // 空房可能仍在滞留期内，此时历史消息保留，created 仍报 true（房间里确实没人）。
    Room* existing = ctx_.rooms().find(roomId);
    const bool created = (existing == nullptr || existing->empty());
    Room& room = ctx_.rooms().getOrCreate(roomId);
    room.add(&s);
    s.setRoom(roomId);

    Json state = Json::object();
    state["room"] = roomId;
    state["created"] = created;
    state["members"] = membersJson(room);
    ctx_.sendTo(s, OpCode::RoomState, state);

    // 先给进房者补历史，再广播 join，客户端看到的消息顺序才完整。
    sendHistory(s, room);

    Json sys = Json::object();
    sys["event"] = "join";
    sys["room"] = roomId;
    sys["userId"] = s.id();
    sys["name"] = s.name();
    sys["text"] = s.name() + (created ? " 创建并加入了房间 " : " 加入了房间 ") + roomId;
    sys["ts"] = nowMs();
    ctx_.broadcastRoom(roomId, OpCode::SystemMsg, sys, s.id());

    LOG_INFO("session#%llu ('%s') joined room '%s' (%s, members=%zu)",
             static_cast<unsigned long long>(s.id()), s.name().c_str(),
             roomId.c_str(), created ? "created" : "existing", room.size());
}

void RoomService::onLeaveRoom(Session& s, const Json& /*j*/) {
    if (!s.loggedIn()) {
        ctx_.sendError(s, "not_logged_in", "login first");
        return;
    }
    if (s.room().empty()) {
        ctx_.sendError(s, "not_in_room", "you are not in a room");
        return;
    }

    const std::string roomId = s.room();
    leaveRoom(s, true);

    Json sys = Json::object();
    sys["event"] = "left";
    sys["room"] = roomId;
    sys["text"] = "已离开房间 " + roomId;
    sys["ts"] = nowMs();
    ctx_.sendTo(s, OpCode::SystemMsg, sys);
}

void RoomService::onListRooms(Session& s, const Json& /*j*/) {
    if (!s.loggedIn()) {
        ctx_.sendError(s, "not_logged_in", "login first");
        return;
    }

    Json list = Json::array();
    for (const auto& kv : ctx_.rooms().all()) {
        if (kv.second->empty()) continue;   // 滞留中的空房不对外展示
        Json r = Json::object();
        r["id"] = kv.first;
        r["count"] = kv.second->size();
        list.push_back(std::move(r));
    }

    Json resp = Json::object();
    resp["rooms"] = list;
    ctx_.sendTo(s, OpCode::RoomList, resp);
}

void RoomService::leaveRoom(Session& s, bool notifyOthers) {
    if (s.room().empty()) return;

    const std::string roomId = s.room();
    const std::string name = s.name();
    const uint64_t sid = s.id();

    s.clearRoom();

    Room* room = ctx_.rooms().find(roomId);
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
        ctx_.broadcastRoom(roomId, OpCode::SystemMsg, sys, sid);
    }

    if (room->empty()) {
        const int ttl = ctx_.config().roomTtlSec;
        if (ttl <= 0) {
            ctx_.rooms().removeIfEmpty(roomId);
            LOG_INFO("room '%s' removed (empty)", roomId.c_str());
        } else {
            // 滞留：保留房间与历史消息，方便断线重连的人回来还能看到上下文。
            room->markEmpty(static_cast<int64_t>(nowMs()));
            LOG_INFO("room '%s' is empty now, kept %ds for history", roomId.c_str(), ttl);
        }
    }
}

Json RoomService::entryToJson(const HistoryEntry& entry, bool withAudio) {
    Json m = Json::object();
    m["seq"] = entry.seq;
    m["from"] = entry.from;
    m["name"] = entry.name;
    m["ts"] = entry.ts;

    if (entry.kind == HistoryKind::Voice) {
        m["kind"] = "voice";
        m["durationMs"] = entry.durationMs;
        if (withAudio) m["opus"] = base64Encode(entry.audio);
    } else {
        m["kind"] = "text";
        m["text"] = entry.text;
    }
    return m;
}

void RoomService::sendHistory(Session& s, Room& room) {
    const ServerConfig& cfg = ctx_.config();

    // 历史来源：优先内存环形缓冲（快），为空时回落到持久层。
    // 内存为空只发生在服务端刚重启时——此时数据库是唯一的真相来源，
    // 不补回来的话「聊天记录库」就变成只写不读，等于白存。
    const auto& inMemory = room.history();

    std::vector<HistoryEntry> fromDb;
    std::vector<const HistoryEntry*> entries;

    if (!inMemory.empty()) {
        entries.reserve(inMemory.size());
        for (const HistoryEntry& entry : inMemory) entries.push_back(&entry);
    } else {
        fromDb = ctx_.messages().loadRecent(room.id(), static_cast<int>(cfg.historySendLimit));
        entries.reserve(fromDb.size());
        for (const HistoryEntry& entry : fromDb) entries.push_back(&entry);
        if (!fromDb.empty()) {
            LOG_INFO("房间 '%s' 内存无历史，从数据库补回 %zu 条",
                     room.id().c_str(), fromDb.size());
        }
    }

    if (entries.empty()) return;

    // 语音条目一条就占 30~40 KB（Base64 后），如果和文本混在一个帧里，
    // 几条语音就能撞上单帧 64 KiB 上限。所以：文本批量成帧，语音每条单独成帧。
    Json textBatch = Json::array();
    std::vector<const HistoryEntry*> voices;
    size_t bytes = 0;
    size_t count = 0;

    // 从新到旧挑选，凑够条数/字节上限
    for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
        const HistoryEntry& entry = **it;
        if (count >= cfg.historySendLimit) break;

        if (entry.kind == HistoryKind::Voice) {
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
        ctx_.sendTo(s, OpCode::HistoryResp, resp);
    }

    // voices 里是「新 -> 旧」，反转成时间正序
    std::reverse(voices.begin(), voices.end());
    for (const HistoryEntry* entry : voices) {
        Json one = Json::array();
        one.push_back(entryToJson(*entry, true));
        Json resp = Json::object();
        resp["room"] = room.id();
        resp["messages"] = one;
        ctx_.sendTo(s, OpCode::HistoryResp, resp);
    }

    LOG_DEBUG("session#%llu got %zu text + %zu voice history of room '%s'",
              static_cast<unsigned long long>(s.id()), textBatch.size(), voices.size(),
              room.id().c_str());
}

}  // namespace dp
