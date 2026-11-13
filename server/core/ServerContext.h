/*
 * 文件名:    ServerContext.h
 * 创建日期:  2026-11-13
 * 作者:      JaTin
 * 功能描述:  业务服务共享的运行时上下文。
 * 修改记录:  2026-11-13 JaTin 按 C/C++ 编码规范整改注释与命名。
 */

#pragma once

#include <cstdint>
#include <string>
#include <utility>

#include "common/Protocol.h"
#include "server/core/MessageRepository.h"
#include "server/core/RoomManager.h"
#include "server/core/UserRepository.h"
#include "server/core/ServerConfig.h"
#include "server/core/SessionManager.h"

namespace dp {

class CReactor;

/**
 * @brief 服务端运行时上下文。
 *
 * @details 生命周期由 CServer 门面独占持有，并在启动时 `attachReactor()` 绑定事件循环。
 *          业务服务通过引用持有它，因此它必须先于所有 CService 构造、后于所有 CService 析构。
 *
 * @note 线程模型：单线程 reactor，所有方法都只能在事件循环线程调用，不加锁。
 */
class CServerContext {
public:
    /**
     * @brief 用配置构造上下文。
     * @param cfg 服务端参数，构造后可通过 config() 读取（不提供写入接口）。
     */
    explicit CServerContext(ServerConfig_S cfg);

    ~CServerContext();

    CServerContext(const CServerContext&) = delete;
    CServerContext& operator=(const CServerContext&) = delete;

    /**
     * @brief 绑定事件循环。
     * @param reactor 事件循环实例，必须比本对象活得久。
     * @details 必须在首次分发请求之前调用，否则所有出站调用都是空操作。
     */
    void attachReactor(CReactor* reactor) noexcept { reactor_ = reactor; }

    /* 只读配置。 */
    const ServerConfig_S& config() const noexcept { return m_stCfg; }

    /* 会话表，可增删查。 */
    CSessionManager& sessions() noexcept { return m_stSessions; }

    /* 房间表，可增删查。 */
    CRoomManager& rooms() noexcept { return rooms_; }

    /* 用户库（持久化账号）。 */
    CUserRepository& users() noexcept { return m_stUsers; }

    /* 聊天记录库（持久化历史）。 */
    CMessageRepository& messages() noexcept { return m_stMessages; }

    /* ---- 下发给客户端的 LLM 配置 ---- */

    /**
     * @brief 设置待下发的 LLM 配置。
     * @param cfg 已解析的 JSON 对象（结构由客户端定义，服务端只透传）。
     */
    void setLlmConfig(Json cfg) noexcept { m_stLlmConfig = std::move(cfg); }

    /* 是否已加载 LLM 配置。 */
    bool hasLlmConfig() const noexcept { return !m_stLlmConfig.is_null(); }

    /* 待下发的 LLM 配置（未加载时为 null）。 */
    const Json& llmConfig() const noexcept { return m_stLlmConfig; }

    /**
     * @brief 分配下一个全局消息序号。
     * @retval 严格单调递增的序号，从 1 开始，服务端重启后归位。
     * @details 客户端按该序号去重与补漏，因此必须全局唯一而非按房间独立。
     */
    uint64_t nextSeq() noexcept { return m_uNextSeq++; }

    /**
     * @brief 向单个会话发送一帧。
     * @param session 目标会话。
     * @param op      协议操作码。
     * @param payload JSON 负载。
     */
    void sendTo(CSession& session, OpCode_EN op, const Json& payload);

    /**
     * @brief 发送标准错误帧（`{"code":..,"text":..}`）。
     * @param session 目标会话。
     * @param code    机器可读的错误码，例如 `bad_name`。
     * @param text    人类可读的说明。
     */
    void sendError(CSession& session, const std::string& code, const std::string& text);

    /**
     * @brief 向房间内所有成员广播。
     * @param roomId   房间号；房间不存在时静默返回。
     * @param op       协议操作码。
     * @param payload  JSON 负载。
     * @param exceptId 要排除的会话 id，0 表示不排除（用于「回显给发送者本人」）。
     */
    void broadcastRoom(const std::string& roomId, OpCode_EN op, const Json& payload,
                       uint64_t exceptId = 0);

    /**
     * @brief 请求关闭会话。
     * @param session 目标会话。
     * @param reason  日志用的原因描述。
     * @details 只做标记，真正的回收在事件批次结束后由 CReactor 统一执行，
     *          避免在遍历过程中销毁 CSession 导致迭代器失效。
     */
    void requestClose(CSession& session, const std::string& reason);

private:
    /* < 启动参数（只读） */
    ServerConfig_S   m_stCfg;
    /* < 连接表 */
    CSessionManager m_stSessions;
    /* < 房间表 */
    CRoomManager    rooms_;
    /* < 用户库 */
    CUserRepository    m_stUsers;
    /* < 聊天记录库 */
    CMessageRepository m_stMessages;
    /* < 全局消息序号 */
    uint64_t       m_uNextSeq = 1;
    /* < 登录时下发给客户端的 LLM 配置 */
    Json           m_stLlmConfig;
    /* < 事件循环，由 CServer 注入 */
    CReactor*       reactor_ = nullptr;
};

/* namespace dp */
}
