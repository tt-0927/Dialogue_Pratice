/**
 * @file       RoomService.h
 * @brief      房间领域业务：进房、离房、房间列表、历史消息下发。
 * @author     JaTin
 * @date       2026-11-13
 * @note       修改记录：2026-11-14 JaTin 统一为 Doxygen 风格注释。
 */

#pragma once

#include <string>

#include "server/core/Service.h"

namespace dp {

/**
 * @brief   处理 `JoinRoom` / `LeaveRoom` / `ListRooms` 三类请求，并负责历史回放。
 * @details 房间不需要预建：`JoinRoom` 遇到不存在的房间号就直接创建， 并通过 `RoomState.created` 告知客户端。 历史消息按「文本批量一帧、每条语音单独一帧」下发——一条语音 Base64 后约 30–40 KB，多条塞进同一帧会撞上 64 KiB 的单帧上限。 客户端本就按 seq 增量合并，收到多个 `HistoryResp` 是预期行为。
 * @author  JaTin
 */
class CRoomService final : public CService {
public:
    using CService::CService;

    /**
     * @brief      声明负责的 opcode。
     * @note       本函数无输入参数。
     * @return     含 JoinRoom、LeaveRoom、ListRooms 的列表。
     * @author     JaTin
     */
    std::vector<OpCode_EN> opcodes() const override;

    /**
     * @brief      按 opcode 分派到具体处理函数。
     * @param[out] session        发起请求的会话；类型 CSession&。
     * @param[in] op             请求类型；类型 OpCode_EN。
     * @param[in] payload        已解析的 JSON 负载；类型 const Json&。
     * @return     无返回值；处理结果通过成员状态或信号对外体现。
     * @author     JaTin
     */
    void handle(CSession& session, OpCode_EN op, const Json& payload) override;

    /**
     * @brief      让会话退出当前房间。
     * @details    同时承担「连接断开时的清理」职责，由 CReactor 的会话关闭钩子调用， 因此必须是 public 且可重入安全（房间为空时直接返回）。
     * @param[out] session        会话；类型 CSession&。
     * @param[in] notifyOthers   是否向剩余成员广播离开提示；类型 bool。
     * @return     无返回值；处理结果通过成员状态或信号对外体现。
     * @author     JaTin
     */
    void leaveRoom(CSession& session, bool notifyOthers);

private:
    /**
     * @brief      处理进房（房间不存在则创建）。
     * @param[out] session        会话，必须已登录；类型 CSession&。
     * @param[in] j              负载：`{"room":"1001"}`；类型 const Json&。
     * @return     无返回值；处理结果通过成员状态或信号对外体现。
     * @author     JaTin
     */
    void onJoinRoom(CSession& session, const Json& j);

    /**
     * @brief      处理主动离房。
     * @param[out] session        会话；类型 CSession&。
     * @param[in] j              负载（忽略）；类型 const Json&。
     * @return     无返回值；处理结果通过成员状态或信号对外体现。
     * @author     JaTin
     */
    void onLeaveRoom(CSession& session, const Json& j);

    /**
     * @brief      处理房间列表查询。
     * @details    滞留中的空房间不对外展示。
     * @param[out] session        会话；类型 CSession&。
     * @param[in] j              负载（忽略）；类型 const Json&。
     * @return     无返回值；处理结果通过成员状态或信号对外体现。
     * @author     JaTin
     */
    void onListRooms(CSession& session, const Json& j);

    /**
     * @brief      向刚进房的会话回放历史消息。
     * @param[out] session        目标会话；类型 CSession&。
     * @param[out] room           房间；类型 CRoom&。
     * @return     无返回值；处理结果通过成员状态或信号对外体现。
     * @author     JaTin
     */
    void sendHistory(CSession& session, CRoom& room);

    /**
     * @brief      把历史条目转成下发用的 JSON。
     * @param[in] entry          历史条目；类型 const HistoryEntry_S&。
     * @param[in] withAudio      是否附带 Base64 音频；文本批量帧传 false 以节省体积；类型 bool。
     * @return     单条消息的 JSON 对象。
     * @author     JaTin
     */
    static Json entryToJson(const HistoryEntry_S& entry, bool withAudio);

    /**
     * @brief      构造房间成员快照数组。
     * @param[in] room           房间；类型 const CRoom&。
     * @return     `[{"id":..,"name":..}, ...]`。
     * @author     JaTin
     */
    static Json membersJson(const CRoom& room);
};

/**
 * @brief  namespace dp。
 */
}
