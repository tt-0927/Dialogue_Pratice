/**
 * @file  ServerContext.h
 * @brief 业务服务共享的运行时上下文。
 * @details 把「状态」与「出站能力」集中到一处，使业务服务不必感知 epoll、socket
 *          与写缓冲的细节。这是 core 层与 business 层之间唯一的耦合面。
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

class Reactor;

/**
 * @brief 服务端运行时上下文。
 *
 * @details 生命周期由 Server 门面独占持有，并在启动时 `attachReactor()` 绑定事件循环。
 *          业务服务通过引用持有它，因此它必须先于所有 Service 构造、后于所有 Service 析构。
 *
 * @note 线程模型：单线程 reactor，所有方法都只能在事件循环线程调用，不加锁。
 */
class ServerContext {
public:
    /**
     * @brief 用配置构造上下文。
     * @param cfg 服务端参数，构造后可通过 config() 读取（不提供写入接口）。
     */
    explicit ServerContext(ServerConfig cfg);

    ~ServerContext();

    ServerContext(const ServerContext&) = delete;
    ServerContext& operator=(const ServerContext&) = delete;

    /**
     * @brief 绑定事件循环。
     * @param reactor 事件循环实例，必须比本对象活得久。
     * @details 必须在首次分发请求之前调用，否则所有出站调用都是空操作。
     */
    void attachReactor(Reactor* reactor) noexcept { reactor_ = reactor; }

    /// @brief 只读配置。
    const ServerConfig& config() const noexcept { return cfg_; }

    /// @brief 会话表，可增删查。
    SessionManager& sessions() noexcept { return sessions_; }

    /// @brief 房间表，可增删查。
    RoomManager& rooms() noexcept { return rooms_; }

    /// @brief 用户库（持久化账号）。
    UserRepository& users() noexcept { return users_; }

    /// @brief 聊天记录库（持久化历史）。
    MessageRepository& messages() noexcept { return messages_; }

    // ---- 下发给客户端的 LLM 配置 ----

    /**
     * @brief 设置待下发的 LLM 配置。
     * @param cfg 已解析的 JSON 对象（结构由客户端定义，服务端只透传）。
     */
    void setLlmConfig(Json cfg) noexcept { llmConfig_ = std::move(cfg); }

    /// @brief 是否已加载 LLM 配置。
    bool hasLlmConfig() const noexcept { return !llmConfig_.is_null(); }

    /// @brief 待下发的 LLM 配置（未加载时为 null）。
    const Json& llmConfig() const noexcept { return llmConfig_; }

    /**
     * @brief 分配下一个全局消息序号。
     * @retval 严格单调递增的序号，从 1 开始，服务端重启后归位。
     * @details 客户端按该序号去重与补漏，因此必须全局唯一而非按房间独立。
     */
    uint64_t nextSeq() noexcept { return nextSeq_++; }

    /**
     * @brief 向单个会话发送一帧。
     * @param session 目标会话。
     * @param op      协议操作码。
     * @param payload JSON 负载。
     */
    void sendTo(Session& session, OpCode op, const Json& payload);

    /**
     * @brief 发送标准错误帧（`{"code":..,"text":..}`）。
     * @param session 目标会话。
     * @param code    机器可读的错误码，例如 `bad_name`。
     * @param text    人类可读的说明。
     */
    void sendError(Session& session, const std::string& code, const std::string& text);

    /**
     * @brief 向房间内所有成员广播。
     * @param roomId   房间号；房间不存在时静默返回。
     * @param op       协议操作码。
     * @param payload  JSON 负载。
     * @param exceptId 要排除的会话 id，0 表示不排除（用于「回显给发送者本人」）。
     */
    void broadcastRoom(const std::string& roomId, OpCode op, const Json& payload,
                       uint64_t exceptId = 0);

    /**
     * @brief 请求关闭会话。
     * @param session 目标会话。
     * @param reason  日志用的原因描述。
     * @details 只做标记，真正的回收在事件批次结束后由 Reactor 统一执行，
     *          避免在遍历过程中销毁 Session 导致迭代器失效。
     */
    void requestClose(Session& session, const std::string& reason);

private:
    ServerConfig   cfg_;         ///< 启动参数（只读）
    SessionManager sessions_;    ///< 连接表
    RoomManager    rooms_;       ///< 房间表
    UserRepository    users_;    ///< 用户库
    MessageRepository messages_; ///< 聊天记录库
    uint64_t       nextSeq_ = 1; ///< 全局消息序号
    Json           llmConfig_;   ///< 登录时下发给客户端的 LLM 配置
    Reactor*       reactor_ = nullptr; ///< 事件循环，由 Server 注入
};

}  // namespace dp
