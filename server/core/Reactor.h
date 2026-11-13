/*
 * 文件名:    Reactor.h
 * 创建日期:  2026-11-13
 * 作者:      JaTin
 * 功能描述:  单线程 epoll 事件循环：负责全部网络 I/O 与连接生命周期。
 * 修改记录:  2026-11-13 JaTin 按 C/C++ 编码规范整改注释与命名。
 */

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "common/Protocol.h"
#include "server/core/ServerContext.h"

namespace dp {

/**
 * @brief epoll（边沿触发）驱动的事件循环。
 *
 * @details 处理流程：
 *          `epoll_wait` → 按事件类型分派（accept / signal / 读 / 写 / 关闭）
 *          → 解帧 → CServiceRegistry 分发 → 批次结束后统一回收会话。
 *
 *          所有状态只在事件循环线程访问，因此无需加锁。
 */
class CReactor {
public:
    /**
     * @brief 会话即将被销毁时执行的回调。
     * @details 用于「退出房间并通知房友」这类业务清理。由 CServer 注入，
     *          使 CReactor 不必依赖 business 层。
     */
    using SessionCloseHook = std::function<void(CSession&)>;

    /**
     * @brief 构造事件循环。
     * @param ctx 共享上下文，必须比本对象活得久。
     */
    explicit CReactor(CServerContext& ctx);

    ~CReactor();

    CReactor(const CReactor&) = delete;
    CReactor& operator=(const CReactor&) = delete;

    /**
     * @brief 建立监听 socket、epoll 与信号 fd。
     * @retval true  三套 fd 全部就绪，可以调用 run()。
     * @retval false 任一环节失败，错误原因已写入日志。
     */
    bool start();

    /**
     * @brief 进入事件循环，直到收到 SIGINT/SIGTERM 或 stop()。
     * @details 阻塞式调用，正常情况下只有收到终止信号才会返回。
     */
    void run();

    /* 请求退出事件循环（下一个 epoll_wait 周期生效）。 */
    void stop() noexcept { m_nRunning = false; }

    /* 事件循环是否在运行。 */
    bool running() const noexcept { return m_nRunning; }

    /**
     * @brief 设置会话销毁前的清理回调。
     * @param hook 回调对象，可为空（为空时跳过清理）。
     */
    void setSessionCloseHook(SessionCloseHook hook) { m_stCloseHook = std::move(hook); }

    /**
     * @brief 设置每秒执行一次的钩子。
     * @param hook 回调，可为空。
     * @details 用于「每天清理一次」这类周期性维护：CReactor 只保证每秒调一次，
     *          具体多久做一次由钩子自己判断。这样 CReactor 不需要理解业务周期。
     */
    void setSecondHook(std::function<void()> hook) { secondHook_ = std::move(hook); }

    /* ---- 出站接口：由 CServerContext 转发调用 ---- */

    /**
     * @brief 向单个会话发送一帧，并在需要时挂上 EPOLLOUT。
     * @param session 目标会话。
     * @param op      操作码。
     * @param payload JSON 负载。
     */
    void sendTo(CSession& session, OpCode_EN op, const Json& payload);

    /**
     * @brief 发送错误帧。
     * @param session 目标会话。
     * @param code    错误码。
     * @param text    错误说明。
     */
    void sendError(CSession& session, const std::string& code, const std::string& text);

    /**
     * @brief 向房间广播，帧只编码一次。
     * @param roomId   房间号。
     * @param op       操作码。
     * @param payload  JSON 负载。
     * @param exceptId 排除的会话 id，0 表示不排除。
     */
    void broadcastRoom(const std::string& roomId, OpCode_EN op, const Json& payload,
                       uint64_t exceptId = 0);

    /**
     * @brief 标记会话待关闭，实际回收发生在当前事件批次之后。
     * @param session 目标会话。
     * @param reason  日志原因。
     */
    void requestClose(CSession& session, const std::string& reason);

    /**
     * @brief 根据待发数据量调整该会话的 epoll 关注事件。
     * @param session 目标会话。
     */
    void updateInterest(CSession& session);

private:
    /* < 监听 fd 的伪会话 id */
    static constexpr uint64_t kListenId = 0;
    /* < 信号 fd 的伪会话 id */
    static constexpr uint64_t kSignalId = ~0ULL;

    bool setupSignalFd();
    bool setupListenSocket();
    bool setupEpoll();
    void shutdownAll();

    void handleAccept();
    void handleSignal();
    void handleReadable(CSession& session);
    void handleWritable(CSession& session);
    void handleTimer();
    void reapSessions();

    /**
     * @brief 解析一帧并交给对应服务处理。
     * @param session 来源会话。
     * @param frame   已解出的完整帧。
     */
    void dispatch(CSession& session, const Frame_S& frame);

    /* < 共享上下文 */
    CServerContext& m_stCtx;
    /* < 监听 socket */
    int            m_nListenFd = -1;
    /* < epoll 实例 */
    int            m_nEpollFd  = -1;
    /* < signalfd，统一从循环里收信号 */
    int            m_nSignalFd = -1;
    /* < 循环运行标志 */
    bool           m_nRunning  = false;

    /* < 待回收的会话 id */
    std::vector<uint64_t> m_uPendingClose;
    /* < 下次打印统计的时间点 */
    uint64_t              m_uNextStatsAt = 0;
    /* < 会话销毁前的业务清理回调 */
    SessionCloseHook      m_stCloseHook;
    /* < 每秒一次的维护钩子 */
    std::function<void()> secondHook_;
};

/* namespace dp */
}
