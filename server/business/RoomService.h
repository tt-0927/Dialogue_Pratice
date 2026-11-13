/**
 * @file  RoomService.h
 * @brief 房间领域业务：进房、离房、房间列表、历史消息下发。
 */

#pragma once

#include <string>

#include "server/core/Service.h"

namespace dp {

/**
 * @brief 处理 `JoinRoom` / `LeaveRoom` / `ListRooms` 三类请求，并负责历史回放。
 *
 * @details 房间不需要预建：`JoinRoom` 遇到不存在的房间号就直接创建，
 *          并通过 `RoomState.created` 告知客户端。
 *
 *          历史消息按「文本批量一帧、每条语音单独一帧」下发——一条语音
 *          Base64 后约 30–40 KB，多条塞进同一帧会撞上 64 KiB 的单帧上限。
 *          客户端本就按 seq 增量合并，收到多个 `HistoryResp` 是预期行为。
 */
class RoomService final : public Service {
public:
    using Service::Service;

    /**
     * @brief 声明负责的 opcode。
     * @retval 含 JoinRoom、LeaveRoom、ListRooms 的列表。
     */
    std::vector<OpCode> opcodes() const override;

    /**
     * @brief 按 opcode 分派到具体处理函数。
     * @param session 发起请求的会话。
     * @param op      请求类型。
     * @param payload 已解析的 JSON 负载。
     */
    void handle(Session& session, OpCode op, const Json& payload) override;

    /**
     * @brief 让会话退出当前房间。
     * @param session      会话。
     * @param notifyOthers 是否向剩余成员广播离开提示。
     * @details 同时承担「连接断开时的清理」职责，由 Reactor 的会话关闭钩子调用，
     *          因此必须是 public 且可重入安全（房间为空时直接返回）。
     */
    void leaveRoom(Session& session, bool notifyOthers);

private:
    /**
     * @brief 处理进房（房间不存在则创建）。
     * @param session 会话，必须已登录。
     * @param j       负载：`{"room":"1001"}`。
     */
    void onJoinRoom(Session& session, const Json& j);

    /**
     * @brief 处理主动离房。
     * @param session 会话。
     * @param j       负载（忽略）。
     */
    void onLeaveRoom(Session& session, const Json& j);

    /**
     * @brief 处理房间列表查询。
     * @param session 会话。
     * @param j       负载（忽略）。
     * @details 滞留中的空房间不对外展示。
     */
    void onListRooms(Session& session, const Json& j);

    /**
     * @brief 向刚进房的会话回放历史消息。
     * @param session 目标会话。
     * @param room    房间。
     */
    void sendHistory(Session& session, Room& room);

    /**
     * @brief 把历史条目转成下发用的 JSON。
     * @param entry     历史条目。
     * @param withAudio 是否附带 Base64 音频；文本批量帧传 false 以节省体积。
     * @retval 单条消息的 JSON 对象。
     */
    static Json entryToJson(const HistoryEntry& entry, bool withAudio);

    /**
     * @brief 构造房间成员快照数组。
     * @param room 房间。
     * @retval `[{"id":..,"name":..}, ...]`。
     */
    static Json membersJson(const Room& room);
};

}  // namespace dp
