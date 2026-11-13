/**
 * @file  Room.h
 * @brief 房间：一组 Session 的集合 + 最近消息环形缓冲。
 * @details 房间由「房间号」字符串唯一标识。成员走空后不立即销毁，而是滞留一段时间
 *          （保留历史消息），超时才回收——见 Reactor::handleTimer()。
 *
 *          本类只做数据容器，不含业务规则：进房校验在 RoomService，
 *          消息序号分配在 ServerContext。
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
enum class HistoryKind : uint8_t {
    Text  = 0,   ///< 文本消息
    Voice = 1,   ///< 语音消息
};

/**
 * @brief 一条历史消息。
 *
 * @details `seq` 是服务端全局单调序号（不是房间内序号），客户端据此去重与补漏。
 *          历史回放时客户端可能分多个帧收到，靠 seq 合并。
 */
struct HistoryEntry {
    uint64_t    seq = 0;        ///< 全局消息序号
    uint64_t    from = 0;       ///< 发送者会话 id
    std::string name;           ///< 发送者昵称（快照，改名后历史仍显示当时的名字）
    std::string text;           ///< 文本正文；语音消息为空
    std::string audio;          ///< 语音的 DPV1 容器（二进制）；文本消息为空
    uint32_t    durationMs = 0; ///< 语音时长（毫秒），文本消息为 0
    HistoryKind kind = HistoryKind::Text; ///< 条目类型
    int64_t     ts = 0;         ///< Unix 毫秒时间戳

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
class Room {
public:
    /**
     * @brief 构造房间。
     * @param id 房间号。
     */
    explicit Room(std::string id) : id_(std::move(id)) {}

    /// @brief 房间号。
    const std::string& id() const { return id_; }

    /**
     * @brief 加入成员。
     * @param s 会话指针，生命周期由 SessionManager 保证长于本房间。
     * @retval true  加入成功。
     * @retval false 已在房间内。
     */
    bool add(Session* s);

    /**
     * @brief 移除成员。
     * @param sessionId 会话 id。
     * @retval true  找到并移除。
     * @retval false 不在房间内。
     */
    bool remove(uint64_t sessionId);

    /// @brief 房间是否已无成员。
    bool empty() const { return members_.empty(); }

    /// @brief 当前成员数。
    size_t size() const { return members_.size(); }

    /// @brief 指定会话是否在房间内。
    bool contains(uint64_t sessionId) const { return members_.count(sessionId) > 0; }

    /// @brief 成员表（按 sessionId 有序）。
    const std::map<uint64_t, Session*>& members() const { return members_; }

    /**
     * @brief 向房间内所有成员投递。
     * @param send     投递动作，通常是把同一帧入队并尝试 flush。
     * @param exceptId 要跳过的会话 id，0 表示不跳过。
     * @details 跳过处于 `Closing` 的会话，避免向正在关闭的连接写数据。
     */
    void broadcast(const std::function<void(Session&)>& send, uint64_t exceptId = 0) const;

    // ---- 历史消息（内存环形缓冲，按条数与字节数双重上限）----

    /**
     * @brief 追加一条历史消息并按上限淘汰最旧条目。
     * @param entry      历史条目（移动入容器）。
     * @param maxEntries 条数上限。
     * @param maxBytes   字节上限；语音的 audio 也计入，否则一条 40 KB 的语音就能撑爆缓冲。
     */
    void appendHistory(HistoryEntry entry, size_t maxEntries, size_t maxBytes);

    /// @brief 当前历史（时间正序）。
    const std::deque<HistoryEntry>& history() const { return history_; }

    // ---- 空房滞留 ----

    /// @brief 房间是否处于「已空但滞留」状态。
    bool isLingering() const { return emptySince_ != 0; }

    /// @brief 变为空的时刻（Unix 毫秒），0 表示非空。
    int64_t emptySince() const { return emptySince_; }

    /// @brief 标记房间刚变空。
    void markEmpty(int64_t nowMs) { emptySince_ = nowMs; }

    /// @brief 标记房间重新有人，取消滞留。
    void markAlive() { emptySince_ = 0; }

private:
    std::string                 id_;              ///< 房间号
    std::map<uint64_t, Session*> members_;        ///< 成员（不持有）
    std::deque<HistoryEntry>    history_;         ///< 历史消息，时间正序
    size_t                      historyBytes_ = 0;///< 历史消息累计字节
    int64_t                     emptySince_ = 0;  ///< 变空时刻，0 表示非空
};

}  // namespace dp
