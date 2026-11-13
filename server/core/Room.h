/*
 * 文件名:    Room.h
 * 创建日期:  2026-11-13
 * 作者:      JaTin
 * 功能描述:  房间：一组 CSession 的集合 + 最近消息环形缓冲。
 * 修改记录:  2026-11-13 JaTin 按 C/C++ 编码规范整改注释与命名。
 */

#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <string>

#include "server/core/Session.h"

namespace dp {

/**
 * @brief 历史条目类型。
 * @details 语音条目带音频数据，文本条目只有 text，两者不会同时有值。
 */
enum class HistoryKind_EN : uint8_t {
    /* < 文本消息 */
    Text  = 0,
    /* < 语音消息 */
    Voice = 1,
};

/**
 * @brief 一条历史消息。
 *
 * @details `seq` 是服务端全局单调序号（不是房间内序号），客户端据此去重与补漏。
 *          历史回放时客户端可能分多个帧收到，靠 seq 合并。
 */
struct HistoryEntry_S {
    /* < 全局消息序号 */
    uint64_t    seq = 0;
    /* < 发送者会话 id */
    uint64_t    from = 0;
    /* < 发送者昵称（快照，改名后历史仍显示当时的名字） */
    std::string name;
    /* < 文本正文；语音消息为空 */
    std::string text;
    /* < 语音的 DPV1 容器（二进制）；文本消息为空 */
    std::string audio;
    /* < 语音时长（毫秒），文本消息为 0 */
    uint32_t    durationMs = 0;
    /* < 条目类型 */
    HistoryKind_EN kind = HistoryKind_EN::Text;
    /* < Unix 毫秒时间戳 */
    int64_t     ts = 0;

    /**
     * @brief 本条目的内容字节数。
     * @retval 正文与音频长度之和，用于历史缓冲的字节预算计算。
     */
    size_t payloadBytes() const { return text.size() + audio.size(); }
};

/**
 * @brief 一个聊天房间。
 *
 * @details 成员表按 sessionId 有序存放，保证日志与成员列表输出稳定。
 *          历史消息是内存环形缓冲，按条数与字节数双重上限淘汰最旧条目。
 */
class CRoom {
public:
    /**
     * @brief 构造房间。
     * @param id 房间号。
     */
    explicit CRoom(std::string id) : id_(std::move(id)) {}

    /* 房间号。 */
    const std::string& id() const { return id_; }

    /**
     * @brief 加入成员。
     * @param s 会话指针，生命周期由 CSessionManager 保证长于本房间。
     * @retval true  加入成功。
     * @retval false 已在房间内。
     */
    bool add(CSession* s);

    /**
     * @brief 移除成员。
     * @param sessionId 会话 id。
     * @retval true  找到并移除。
     * @retval false 不在房间内。
     */
    bool remove(uint64_t sessionId);

    /* 房间是否已无成员。 */
    bool empty() const { return m_uMembers.empty(); }

    /* 当前成员数。 */
    size_t size() const { return m_uMembers.size(); }

    /* 指定会话是否在房间内。 */
    bool contains(uint64_t sessionId) const { return m_uMembers.count(sessionId) > 0; }

    /* 成员表（按 sessionId 有序）。 */
    const std::map<uint64_t, CSession*>& members() const { return m_uMembers; }

    /**
     * @brief 向房间内所有成员投递。
     * @param send     投递动作，通常是把同一帧入队并尝试 flush。
     * @param exceptId 要跳过的会话 id，0 表示不跳过。
     * @details 跳过处于 `Closing` 的会话，避免向正在关闭的连接写数据。
     */
    void broadcast(const std::function<void(CSession&)>& send, uint64_t exceptId = 0) const;

    /* ---- 历史消息（内存环形缓冲，按条数与字节数双重上限）---- */

    /**
     * @brief 追加一条历史消息并按上限淘汰最旧条目。
     * @param entry      历史条目（移动入容器）。
     * @param maxEntries 条数上限。
     * @param maxBytes   字节上限；语音的 audio 也计入，否则一条 40 KB 的语音就能撑爆缓冲。
     */
    void appendHistory(HistoryEntry_S entry, size_t maxEntries, size_t maxBytes);

    /* 当前历史（时间正序）。 */
    const std::deque<HistoryEntry_S>& history() const { return m_stHistory; }

    /* ---- 空房滞留 ---- */

    /* 房间是否处于「已空但滞留」状态。 */
    bool isLingering() const { return m_uEmptySince != 0; }

    /* 变为空的时刻（Unix 毫秒），0 表示非空。 */
    int64_t emptySince() const { return m_uEmptySince; }

    /* 标记房间刚变空。 */
    void markEmpty(int64_t nowMs) { m_uEmptySince = nowMs; }

    /* 标记房间重新有人，取消滞留。 */
    void markAlive() { m_uEmptySince = 0; }

private:
    /* < 房间号 */
    std::string                 id_;
    /* < 成员（不持有） */
    std::map<uint64_t, CSession*> m_uMembers;
    /* < 历史消息，时间正序 */
    std::deque<HistoryEntry_S>    m_stHistory;
    /* < 历史消息累计字节 */
    size_t                      m_uHistoryBytes = 0;
    /* < 变空时刻，0 表示非空 */
    int64_t                     m_uEmptySince = 0;
};

/* namespace dp */
}
