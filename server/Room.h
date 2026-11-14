/**
 * @file       Room.h
 * @brief      CRoom 头文件，声明相关类型与接口。
 * @author     JaTin
 * @date       2026-11-13
 * @note       修改记录：2026-11-14 JaTin 统一为 Doxygen 风格注释。
 */

/**
 * @brief  server/CRoom.h。
 */

/**
 * @brief  房间：一组 CSession 的集合 + 最近消息环形缓冲。 房间由「房间号」字符串唯一标识；成员走空后不立即销毁，而是滞留一段时间 （保留历史消息），超时才回收——见 CServer::handleTimer()。
 */
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <string>

#include "server/Session.h"

namespace dp {

/**
 * @brief   历史条目类型。语音条目带音频数据，文本条目只有 text。
 * @author  JaTin
 */
enum class HistoryKind_EN : uint8_t {
    /**
     * @brief  文本。
     */
    Text  = 0,
    /**
     * @brief  语音消息。
     */
    Voice = 1,
};

/**
 * @brief   一条历史消息。seq 为服务端全局单调序号，用于客户端去重与补漏。
 * @author  JaTin
 */
struct HistoryEntry_S {
    /**
     * @brief  全局消息序号。 初值 = 0。
     * @note   类型 uint64_t；类/结构体成员变量。
     */
    uint64_t    seq = 0;
    /**
     * @brief  from。 初值 = 0。
     * @note   类型 uint64_t；类/结构体成员变量。
     */
    uint64_t    from = 0;
    /**
     * @brief  名称或昵称。
     * @note   类型 std::string；类/结构体成员变量。
     */
    std::string name;
    /**
     * @brief  文本消息正文；语音消息为空
     * @note   类型 std::string；类/结构体成员变量。
     */
    std::string text;
    /**
     * @brief  语音消息的 DPV1 容器（二进制）；文本消息为空
     * @note   类型 std::string；类/结构体成员变量。
     */
    std::string audio;
    /**
     * @brief  语音时长，单位毫秒。 初值 = 0。
     * @note   类型 uint32_t；类/结构体成员变量。
     */
    uint32_t    durationMs = 0;
    /**
     * @brief  消息类型。 初值 = HistoryKind_EN::Text。
     * @note   类型 HistoryKind_EN；类/结构体成员变量。
     */
    HistoryKind_EN kind = HistoryKind_EN::Text;
    /**
     * @brief  Unix 毫秒
     * @note   类型 int64_t；类/结构体成员变量。
     */
    int64_t     ts = 0;

    size_t payloadBytes() const { return text.size() + audio.size(); }
};

/**
 * @brief   C房间。
 * @author  JaTin
 */
class CRoom {
public:
    explicit CRoom(std::string id) : id_(std::move(id)) {}

    const std::string& id() const { return id_; }

    /**
     * @brief      添加 add。
     * @param[in] s              目标会话对象；类型 CSession*。
     * @return     成功返回 true；参数非法或执行失败时返回 false。
     * @author     JaTin
     */
    bool   add(CSession* s);
    /**
     * @brief      移除 remove。
     * @param[in] sessionId      输入参数；类型 uint64_t。
     * @return     成功返回 true；参数非法或执行失败时返回 false。
     * @author     JaTin
     */
    bool   remove(uint64_t sessionId);

    bool   empty() const { return m_uMembers.empty(); }
    size_t size() const { return m_uMembers.size(); }
    bool   contains(uint64_t sessionId) const { return m_uMembers.count(sessionId) > 0; }

    /**
     * @brief  按 sessionId 有序，便于日志与成员列表输出稳定。
     */
    const std::map<uint64_t, CSession*>& members() const { return m_uMembers; }

    /**
     * @brief      向房间内除 exceptId 之外的所有成员投递；跳过正在关闭的会话。
     * @param[in] send           输入参数；类型 const std::function<void(CSession&)>&。
     * @param[in] exceptId       输入参数；类型 uint64_t。
     * @return     无返回值；处理结果通过成员状态或信号对外体现。
     * @author     JaTin
     */
    void broadcast(const std::function<void(CSession&)>& send, uint64_t exceptId = 0) const;

    /**
     * @brief      ---- 历史消息（内存环形缓冲，按条数与字节数双重上限）----。
     * @param[in] entry          条目；类型 HistoryEntry_S。
     * @param[in] maxEntries     输入参数；类型 size_t。
     * @param[in] maxBytes       输入参数；类型 size_t。
     * @return     无返回值；处理结果通过成员状态或信号对外体现。
     * @author     JaTin
     */
    void appendHistory(HistoryEntry_S entry, size_t maxEntries, size_t maxBytes);
    const std::deque<HistoryEntry_S>& history() const { return m_stHistory; }

    /**
     * @brief  ---- 空房滞留 ----。
     */
    bool    isLingering() const { return m_uEmptySince != 0; }
    int64_t emptySince() const { return m_uEmptySince; }
    void    markEmpty(int64_t nowMs) { m_uEmptySince = nowMs; }
    void    markAlive() { m_uEmptySince = 0; }

private:
    /**
     * @brief  编号。
     * @note   类型 std::string；类/结构体成员变量。
     */
    std::string                 id_;
    /**
     * @brief  初始成员列表。
     * @note   类型 std::map<uint64_t, CSession*>；类/结构体成员变量。
     */
    std::map<uint64_t, CSession*> m_uMembers;
    /**
     * @brief  历史消息。
     * @note   类型 std::deque<HistoryEntry_S>；类/结构体成员变量。
     */
    std::deque<HistoryEntry_S>    m_stHistory;
    /**
     * @brief  历史消息字节数。 初值 = 0。
     * @note   类型 size_t；类/结构体成员变量。
     */
    size_t                      m_uHistoryBytes = 0;
    /**
     * @brief  EmptySince。 初值 = 0。
     * @note   类型 int64_t；类/结构体成员变量。
     */
    int64_t                     m_uEmptySince = 0;
};

/**
 * @brief  namespace dp。
 */
}
