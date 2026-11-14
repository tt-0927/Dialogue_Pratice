/**
 * @file       Reactor.h
 * @brief      单线程 epoll 事件循环：负责全部网络 I/O 与连接生命周期。
 * @author     JaTin
 * @date       2026-11-13
 * @note       修改记录：2026-11-14 JaTin 统一为 Doxygen 风格注释。
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
 * @brief   epoll（边沿触发）驱动的事件循环。
 * @details 处理流程： `epoll_wait` → 按事件类型分派（accept / signal / 读 / 写 / 关闭） → 解帧 → CServiceRegistry 分发 → 批次结束后统一回收会话。 所有状态只在事件循环线程访问，因此无需加锁。
 * @author  JaTin
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
     * @brief      构造事件循环。
     * @param[in] ctx            共享上下文，必须比本对象活得久；类型 CServerContext&。
     * @return     构造/析构函数无返回值。
     * @author     JaTin
     */
    explicit CReactor(CServerContext& ctx);

    /**
     * @brief      构造本对象，完成成员初始化与依赖注入。
     * @note       本函数无输入参数。
     * @return     构造/析构函数无返回值。
     * @author     JaTin
     */
    ~CReactor();

    CReactor(const CReactor&) = delete;
    CReactor& operator=(const CReactor&) = delete;

    /**
     * @brief      建立监听 socket、epoll 与信号 fd。
     * @note       本函数无输入参数。
     * @return     true  三套 fd 全部就绪，可以调用 run()；false 任一环节失败，错误原因已写入日志。
     * @author     JaTin
     */
    bool start();

    /**
     * @brief      进入事件循环，直到收到 SIGINT/SIGTERM 或 stop()。
     * @details    阻塞式调用，正常情况下只有收到终止信号才会返回。
     * @note       本函数无输入参数。
     * @return     无返回值；处理结果通过成员状态或信号对外体现。
     * @author     JaTin
     */
    void run();

    /**
     * @brief  请求退出事件循环（下一个 epoll_wait 周期生效）。
     */
    void stop() noexcept { m_nRunning = false; }

    /**
     * @brief  事件循环是否在运行。
     */
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

    /**
     * @brief  ---- 出站接口：由 CServerContext 转发调用 ----。
     * @details 向单个会话发送一帧，并在需要时挂上 EPOLLOUT。
     */
    void sendTo(CSession& session, OpCode_EN op, const Json& payload);

    /**
     * @brief      发送错误帧。
     * @param[out] session        目标会话；类型 CSession&。
     * @param[in] code           错误码；类型 const std::string&。
     * @param[in] text           错误说明；类型 const std::string&。
     * @return     无返回值；处理结果通过成员状态或信号对外体现。
     * @author     JaTin
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
                       /**
                        * @brief  except编号。 初值 = 0)。
                        * @note   类型 uint64_t；类/结构体成员变量。
                        */
                       uint64_t exceptId = 0);

    /**
     * @brief      标记会话待关闭，实际回收发生在当前事件批次之后。
     * @param[out] session        目标会话；类型 CSession&。
     * @param[in] reason         日志原因；类型 const std::string&。
     * @return     无返回值；处理结果通过成员状态或信号对外体现。
     * @author     JaTin
     */
    void requestClose(CSession& session, const std::string& reason);

    /**
     * @brief      根据待发数据量调整该会话的 epoll 关注事件。
     * @param[out] session        目标会话；类型 CSession&。
     * @return     无返回值；处理结果通过成员状态或信号对外体现。
     * @author     JaTin
     */
    void updateInterest(CSession& session);

private:
    /**
     * @brief  监听 fd 的伪会话 id
     * @note   取值 0。
     */
    static constexpr uint64_t kListenId = 0;
    /**
     * @brief  信号 fd 的伪会话 id
     * @note   取值 ~0ULL。
     */
    static constexpr uint64_t kSignalId = ~0ULL;

    /**
     * @brief      建立并配置信号文件描述符。
     * @note       本函数无输入参数。
     * @return     成功返回 true；参数非法或执行失败时返回 false。
     * @author     JaTin
     */
    bool setupSignalFd();
    /**
     * @brief      建立并配置 listen 套接字。
     * @note       本函数无输入参数。
     * @return     成功返回 true；参数非法或执行失败时返回 false。
     * @author     JaTin
     */
    bool setupListenSocket();
    /**
     * @brief      建立并配置 epoll。
     * @note       本函数无输入参数。
     * @return     成功返回 true；参数非法或执行失败时返回 false。
     * @author     JaTin
     */
    bool setupEpoll();
    /**
     * @brief      关闭并释放 all。
     * @note       本函数无输入参数。
     * @return     无返回值；处理结果通过成员状态或信号对外体现。
     * @author     JaTin
     */
    void shutdownAll();

    /**
     * @brief      处理 accept相关的数据并返回处理结果。
     * @note       本函数无输入参数。
     * @return     无返回值；处理结果通过成员状态或信号对外体现。
     * @author     JaTin
     */
    void handleAccept();
    /**
     * @brief      处理信号。
     * @note       本函数无输入参数。
     * @return     无返回值；处理结果通过成员状态或信号对外体现。
     * @author     JaTin
     */
    void handleSignal();
    /**
     * @brief      处理 readable相关的数据并返回处理结果。
     * @param[out] session        目标会话对象；类型 CSession&。
     * @return     无返回值；处理结果通过成员状态或信号对外体现。
     * @author     JaTin
     */
    void handleReadable(CSession& session);
    /**
     * @brief      处理 writable相关的数据并返回处理结果。
     * @param[out] session        目标会话对象；类型 CSession&。
     * @return     无返回值；处理结果通过成员状态或信号对外体现。
     * @author     JaTin
     */
    void handleWritable(CSession& session);
    /**
     * @brief      处理定时器。
     * @note       本函数无输入参数。
     * @return     无返回值；处理结果通过成员状态或信号对外体现。
     * @author     JaTin
     */
    void handleTimer();
    /**
     * @brief      执行 reap 会话集合。
     * @note       本函数无输入参数。
     * @return     无返回值；处理结果通过成员状态或信号对外体现。
     * @author     JaTin
     */
    void reapSessions();

    /**
     * @brief      解析一帧并交给对应服务处理。
     * @param[out] session        来源会话；类型 CSession&。
     * @param[in] frame          已解出的完整帧；类型 const Frame_S&。
     * @return     无返回值；处理结果通过成员状态或信号对外体现。
     * @author     JaTin
     */
    void dispatch(CSession& session, const Frame_S& frame);

    /**
     * @brief  共享上下文
     * @note   类型 CServerContext&；类/结构体成员变量。
     */
    CServerContext& m_stCtx;
    /**
     * @brief  监听 socket
     * @note   类型 int；类/结构体成员变量。
     */
    int            m_nListenFd = -1;
    /**
     * @brief  epoll 实例
     * @note   类型 int；类/结构体成员变量。
     */
    int            m_nEpollFd  = -1;
    /**
     * @brief  signalfd，统一从循环里收信号
     * @note   类型 int；类/结构体成员变量。
     */
    int            m_nSignalFd = -1;
    /**
     * @brief  循环运行标志
     * @note   类型 bool；类/结构体成员变量。
     */
    bool           m_nRunning  = false;

    /**
     * @brief  待回收的会话 id
     * @note   类型 std::vector<uint64_t>；类/结构体成员变量。
     */
    std::vector<uint64_t> m_uPendingClose;
    /**
     * @brief  下次打印统计的时间点
     * @note   类型 uint64_t；类/结构体成员变量。
     */
    uint64_t              m_uNextStatsAt = 0;
    /**
     * @brief  会话销毁前的业务清理回调
     * @note   类型 SessionCloseHook；类/结构体成员变量。
     */
    SessionCloseHook      m_stCloseHook;
    /**
     * @brief  < 每秒一次的维护钩子。
     */
    std::function<void()> secondHook_;
};

/**
 * @brief  namespace dp。
 */
}
