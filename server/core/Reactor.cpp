/*
 * 文件名:    Reactor.cpp
 * 创建日期:  2026-11-13
 * 作者:      JaTin
 * 功能描述:  CReactor 的实现：epoll 事件循环、连接生命周期与出站写缓冲管理。
 * 修改记录:  2026-11-13 JaTin 按 C/C++ 编码规范整改注释与命名。
 */

#include "server/core/Reactor.h"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <exception>
#include <utility>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <unistd.h>

#include "server/core/Logger.h"
#include "server/core/ServiceRegistry.h"
#include "server/core/TimeUtil.h"

namespace dp {
namespace {

/* < 单次 epoll_wait 最多取出的就绪事件数 */
constexpr int kMaxEvents = 256;

/* namespace */
}

/*
 * 功能描述:  构造 CReactor 对象，完成成员初始化与依赖注入。
 * 作者:      JaTin
 * param [out]  stCtx            服务端共享上下文引用，必须比本对象存活更久，类型 CServerContext& ctx) : m_stCtx(。
 * return:    无返回值（构造函数或析构函数）。
 */
CReactor::CReactor(CServerContext& stCtx) : m_stCtx(stCtx) {}

/*
 * 功能描述:  析构 CReactor 对象，释放运行期持有的全部资源。
 * 作者:      JaTin
 * return:    无返回值（构造函数或析构函数）。
 */
CReactor::~CReactor() {
    shutdownAll();
}

/* ---------------------------------------------------------------- 启动 / 关闭 */

/*
 * 功能描述:  建立并配置信号文件描述符；实现上基于 epoll 边沿触发等待就绪事件、屏蔽进程信号并交由事件循环统一处理；失败时返回 false。
 * 作者:      JaTin
 * return:    成功返回 true；参数非法或执行失败时返回 false。
 */
bool CReactor::setupSignalFd() {
    sigset_t stMask;
    sigemptyset(&stMask);
    sigaddset(&stMask, SIGINT);
    sigaddset(&stMask, SIGTERM);
    sigaddset(&stMask, SIGPIPE);

    /* 先屏蔽，再用 signalfd 统一从事件循环里取信号，避免异步打断 epoll_wait。 */
    if (::sigprocmask(SIG_BLOCK, &stMask, nullptr) < 0) {
        LOG_ERROR("sigprocmask: %s", std::strerror(errno));
        return false;
    }
    m_nSignalFd = ::signalfd(-1, &stMask, SFD_NONBLOCK | SFD_CLOEXEC);
    if (m_nSignalFd < 0) {
        LOG_ERROR("signalfd: %s", std::strerror(errno));
        return false;
    }
    return true;
}

/*
 * 功能描述:  建立并配置listen 套接字；实现上异常路径记录错误或警告日志；失败时返回 false。
 * 作者:      JaTin
 * return:    成功返回 true；参数非法或执行失败时返回 false。
 */
bool CReactor::setupListenSocket() {
    const ServerConfig_S& stCfg = m_stCtx.config();

    const int hFd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (hFd < 0) {
        LOG_ERROR("socket: %s", std::strerror(errno));
        return false;
    }

    int nOne = 1;
    ::setsockopt(hFd, SOL_SOCKET, SO_REUSEADDR, &nOne, sizeof(nOne));

    sockaddr_in stAddr{};
    stAddr.sin_family = AF_INET;
    stAddr.sin_port = htons(stCfg.port);
    if (::inet_pton(AF_INET, stCfg.bindAddr.c_str(), &stAddr.sin_addr) != 1) {
        LOG_ERROR("invalid bind address: %s", stCfg.bindAddr.c_str());
        ::close(hFd);
        return false;
    }
    if (::bind(hFd, reinterpret_cast<sockaddr*>(&stAddr), sizeof(stAddr)) < 0) {
        LOG_ERROR("bind %s:%u failed: %s", stCfg.bindAddr.c_str(), stCfg.port, std::strerror(errno));
        ::close(hFd);
        return false;
    }
    if (::listen(hFd, stCfg.backlog) < 0) {
        LOG_ERROR("listen failed: %s", std::strerror(errno));
        ::close(hFd);
        return false;
    }

    m_nListenFd = hFd;
    return true;
}

/*
 * 功能描述:  建立并配置epoll；实现上向 epoll 实例注册或注销描述符、屏蔽进程信号并交由事件循环统一处理；失败时返回 false。
 * 作者:      JaTin
 * return:    成功返回 true；参数非法或执行失败时返回 false。
 */
bool CReactor::setupEpoll() {
    m_nEpollFd = ::epoll_create1(EPOLL_CLOEXEC);
    if (m_nEpollFd < 0) {
        LOG_ERROR("epoll_create1: %s", std::strerror(errno));
        return false;
    }

    /* 监听 fd 同样用边沿触发：accept 必须循环到 EAGAIN，否则会漏掉新连接。 */
    epoll_event stEv{};
    stEv.events = EPOLLIN | EPOLLET;
    stEv.data.u64 = kListenId;
    if (::epoll_ctl(m_nEpollFd, EPOLL_CTL_ADD, m_nListenFd, &stEv) < 0) {
        LOG_ERROR("epoll_ctl ADD listen: %s", std::strerror(errno));
        return false;
    }

    epoll_event stSev{};
    stSev.events = EPOLLIN;
    stSev.data.u64 = kSignalId;
    if (::epoll_ctl(m_nEpollFd, EPOLL_CTL_ADD, m_nSignalFd, &stSev) < 0) {
        LOG_ERROR("epoll_ctl ADD signalfd: %s", std::strerror(errno));
        return false;
    }
    return true;
}

/*
 * 功能描述:  启动start；失败时返回 false。
 * 作者:      JaTin
 * return:    成功返回 true；参数非法或执行失败时返回 false。
 */
bool CReactor::start() {
    if (!setupSignalFd()) return false;
    if (!setupListenSocket()) return false;
    if (!setupEpoll()) return false;

    const ServerConfig_S& stCfg = m_stCtx.config();
    LOG_INFO("dialogue server ready | listen=%s:%u backlog=%d heartbeat=%ds maxConn=%zu",
             stCfg.bindAddr.c_str(), stCfg.port, stCfg.backlog,
             stCfg.heartbeatTimeoutSec, stCfg.maxConnections);
    LOG_INFO("history | keep=%zu msgs / %zu KiB per room, send<=%zu msgs / %zu KiB, roomTtl=%ds",
             stCfg.historyEntries, stCfg.historyBytes / 1024,
             stCfg.historySendLimit, stCfg.historySendBytes / 1024, stCfg.roomTtlSec);
    return true;
}

/*
 * 功能描述:  关闭并释放all；实现上向 epoll 实例注册或注销描述符。
 * 作者:      JaTin
 * return:    无返回值；处理结果通过成员状态或信号对外体现。
 */
void CReactor::shutdownAll() {
    CSessionManager& stSessions = m_stCtx.sessions();

    if (!stSessions.all().empty()) {
        LOG_INFO("closing %zu session(s)", stSessions.size());
    }
    for (auto& stKv : stSessions.all()) {
        CSession* stS = stKv.second.get();
        if (m_nEpollFd >= 0 && stS->fd() >= 0) {
            ::epoll_ctl(m_nEpollFd, EPOLL_CTL_DEL, stS->fd(), nullptr);
        }
    }
    /* CSessionManager 无 clear()，逐个删除以触发 fd 关闭。 */
    while (!stSessions.all().empty()) {
        stSessions.remove(stSessions.all().begin()->first);
    }

    if (m_nListenFd >= 0) { ::close(m_nListenFd); m_nListenFd = -1; }
    if (m_nSignalFd >= 0) { ::close(m_nSignalFd); m_nSignalFd = -1; }
    if (m_nEpollFd >= 0)  { ::close(m_nEpollFd);  m_nEpollFd = -1; }
}

/* ---------------------------------------------------------------- 事件循环 */

/*
 * 功能描述:  运行run；实现上基于 epoll 边沿触发等待就绪事件、异常路径记录错误或警告日志。
 * 作者:      JaTin
 * return:    无返回值；处理结果通过成员状态或信号对外体现。
 */
void CReactor::run() {
    m_nRunning = true;
    std::vector<epoll_event> aEvents(kMaxEvents);
    auto stLastTick = std::chrono::steady_clock::now();

    while (m_nRunning) {
        const int nN = ::epoll_wait(m_nEpollFd, aEvents.data(), kMaxEvents, 1000);
        if (nN < 0) {
            if (errno == EINTR) continue;
            LOG_ERROR("epoll_wait: %s", std::strerror(errno));
            break;
        }

        for (int i = 0; i < nN && m_nRunning; ++i) {
            const uint32_t uEv = aEvents[i].events;
            const uint64_t uId = aEvents[i].data.u64;

            if (uId == kListenId) {
                handleAccept();
                continue;
            }
            if (uId == kSignalId) {
                handleSignal();
                continue;
            }

            CSession* stS = m_stCtx.sessions().find(uId);
            if (stS == nullptr) continue;

            if (uEv & (EPOLLHUP | EPOLLERR)) {
                requestClose(*stS, "hangup");
                continue;
            }
            if (uEv & EPOLLIN) {
                handleReadable(*stS);
            }
            if ((uEv & EPOLLOUT) && stS->state() != SessionState_EN::Closing) {
                handleWritable(*stS);
            }
            if ((uEv & EPOLLRDHUP) && stS->state() != SessionState_EN::Closing) {
                requestClose(*stS, "peer shutdown");
            }
        }

        /* 统一回收：保证任何 handler 都不会在遍历过程中销毁 CSession。 */
        reapSessions();

        const auto stNow = std::chrono::steady_clock::now();
        if (stNow - stLastTick >= std::chrono::seconds(1)) {
            stLastTick = stNow;
            handleTimer();
            reapSessions();
        }
    }

    LOG_INFO("event loop stopped");
}

/*
 * 功能描述:  处理accept；实现上向 epoll 实例注册或注销描述符、异常路径记录错误或警告日志。
 * 作者:      JaTin
 * return:    无返回值；处理结果通过成员状态或信号对外体现。
 */
void CReactor::handleAccept() {
    CSessionManager& stSessions = m_stCtx.sessions();
    const ServerConfig_S& stCfg = m_stCtx.config();

    for (;;) {
        sockaddr_in stAddr{};
        socklen_t stAddrLen = sizeof(stAddr);
        const int hFd = ::accept4(m_nListenFd, reinterpret_cast<sockaddr*>(&stAddr), &stAddrLen,
                                 SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (hFd < 0) {
            /* ET：已取完 */
            if (errno == EAGAIN || errno == EWOULDBLOCK) return;
            if (errno == EINTR) continue;
            if (errno == EMFILE || errno == ENFILE) {
                LOG_ERROR("accept: out of file descriptors (%s)", std::strerror(errno));
                return;
            }
            LOG_WARN("accept failed: %s", std::strerror(errno));
            return;
        }

        if (stSessions.size() >= stCfg.maxConnections) {
            LOG_WARN("max connections (%zu) reached, rejecting fd=%d", stCfg.maxConnections, hFd);
            ::close(hFd);
            continue;
        }

        int nOne = 1;
        ::setsockopt(hFd, IPPROTO_TCP, TCP_NODELAY, &nOne, sizeof(nOne));
        ::setsockopt(hFd, SOL_SOCKET, SO_KEEPALIVE, &nOne, sizeof(nOne));

        char chIp[INET_ADDRSTRLEN] = {0};
        ::inet_ntop(AF_INET, &stAddr.sin_addr, chIp, sizeof(chIp));
        std::string strPeer = std::string(chIp) + ":" + std::to_string(ntohs(stAddr.sin_port));

        CSession* stS = stSessions.add(hFd, std::move(strPeer));

        epoll_event stEv{};
        stEv.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
        stEv.data.u64 = stS->id();
        if (::epoll_ctl(m_nEpollFd, EPOLL_CTL_ADD, hFd, &stEv) < 0) {
            LOG_ERROR("epoll_ctl ADD fd=%d: %s", hFd, std::strerror(errno));
            stSessions.remove(stS->id());
            continue;
        }

        LOG_INFO("session#%llu connected from %s (total=%zu)",
                 static_cast<unsigned long long>(stS->id()), stS->peer().c_str(), stSessions.size());
    }
}

/*
 * 功能描述:  处理信号。
 * 作者:      JaTin
 * return:    无返回值；处理结果通过成员状态或信号对外体现。
 */
void CReactor::handleSignal() {
    signalfd_siginfo stSi{};
    for (;;) {
        const ssize_t uN = ::read(m_nSignalFd, &stSi, sizeof(stSi));
        if (uN != static_cast<ssize_t>(sizeof(stSi))) return;
        if (stSi.ssi_signo == SIGINT || stSi.ssi_signo == SIGTERM) {
            LOG_INFO("received signal %u, stopping", stSi.ssi_signo);
            m_nRunning = false;
            return;
        }
    }
}

/*
 * 功能描述:  处理readable。
 * 作者:      JaTin
 * param [out]  stS              目标会话对象，类型 CSession&。
 * return:    无返回值；处理结果通过成员状态或信号对外体现。
 */
void CReactor::handleReadable(CSession& stS) {
    std::vector<Frame_S> aFrames;
    const ReadResult_EN enR = stS.readAvailable(aFrames);

    if (!aFrames.empty()) stS.touch();

    for (const Frame_S& stF : aFrames) {
        dispatch(stS, stF);
        if (stS.state() == SessionState_EN::Closing) return;
    }

    switch (enR) {
        case ReadResult_EN::Ok:
            break;
        case ReadResult_EN::PeerClosed:
            requestClose(stS, "peer closed");
            break;
        case ReadResult_EN::Error:
            requestClose(stS, std::string("read error: ") + std::strerror(errno));
            break;
        case ReadResult_EN::ProtocolError:
            sendError(stS, "protocol_error", stS.protocolError());
            requestClose(stS, "protocol error: " + stS.protocolError());
            break;
    }
}

/*
 * 功能描述:  处理writable。
 * 作者:      JaTin
 * param [out]  stS              目标会话对象，类型 CSession&。
 * return:    无返回值；处理结果通过成员状态或信号对外体现。
 */
void CReactor::handleWritable(CSession& stS) {
    if (stS.flush() == FlushResult_EN::Error) {
        requestClose(stS, "write failed");
        return;
    }
    updateInterest(stS);
}

/*
 * 功能描述:  处理定时器。
 * 作者:      JaTin
 * return:    无返回值；处理结果通过成员状态或信号对外体现。
 */
void CReactor::handleTimer() {
    const auto stNow = std::chrono::steady_clock::now();
    const auto stTimeout = std::chrono::seconds(m_stCtx.config().heartbeatTimeoutSec);

    std::vector<uint64_t> uExpired;
    for (const auto& stKv : m_stCtx.sessions().all()) {
        CSession* stS = stKv.second.get();
        if (stS->state() == SessionState_EN::Closing) continue;
        if (stNow - stS->lastActive() > stTimeout) uExpired.push_back(stS->id());
    }
    for (uint64_t uId : uExpired) {
        CSession* stS = m_stCtx.sessions().find(uId);
        if (stS != nullptr) requestClose(*stS, "heartbeat timeout");
    }

    const uint64_t uMs = nowMs();
    if (uMs >= m_uNextStatsAt) {
        m_uNextStatsAt = uMs + 60000;
        size_t uLoggedIn = 0;
        for (const auto& stKv : m_stCtx.sessions().all()) {
            if (stKv.second->loggedIn()) ++uLoggedIn;
        }
        LOG_INFO("stats | sessions=%zu loggedIn=%zu rooms=%zu",
                 m_stCtx.sessions().size(), uLoggedIn, m_stCtx.rooms().size());
    }

    /* 周期性维护（数据归档等）：CReactor 只管每秒调一次，频率由钩子决定 */
    if (secondHook_) secondHook_();

    /* 回收超过滞留期的空房间（同时释放其历史消息内存） */
    const int nTtl = m_stCtx.config().roomTtlSec;
    if (nTtl > 0) {
        const int64_t lDeadline = static_cast<int64_t>(uMs) - static_cast<int64_t>(nTtl) * 1000;
        std::vector<std::string> strRooms;
        for (const auto& stKv : m_stCtx.rooms().all()) {
            const CRoom* stR = stKv.second.get();
            if (!stR->isLingering()) continue;
            if (stR->emptySince() <= lDeadline) strRooms.push_back(stKv.first);
        }
        for (const std::string& uId : strRooms) {
            m_stCtx.rooms().removeIfEmpty(uId);
            LOG_INFO("room '%s' expired after %ds idle", uId.c_str(), nTtl);
        }
    }
}

/*
 * 功能描述:  执行reap 会话集合；实现上向 epoll 实例注册或注销描述符。
 * 作者:      JaTin
 * return:    无返回值；处理结果通过成员状态或信号对外体现。
 */
void CReactor::reapSessions() {
    /* 关闭动作可能级联（关闭时广播导致对端写失败），故循环交换批次。 */
    while (!m_uPendingClose.empty()) {
        std::vector<uint64_t> uBatch;
        uBatch.swap(m_uPendingClose);

        for (uint64_t uId : uBatch) {
            CSession* stS = m_stCtx.sessions().find(uId);
            if (stS == nullptr) continue;

            /* 业务清理（退出房间、通知房友）通过钩子交给 business 层， */
            /* CReactor 本身不理解房间语义。 */
            if (m_stCloseHook) m_stCloseHook(*stS);

            const int hFd = stS->fd();
            const uint64_t uRx = stS->rxBytes();
            const uint64_t uTx = stS->txBytes();
            const std::string strPeer = stS->peer();

            if (m_nEpollFd >= 0 && hFd >= 0) {
                ::epoll_ctl(m_nEpollFd, EPOLL_CTL_DEL, hFd, nullptr);
            }
            /* 析构中 close(fd) */
            m_stCtx.sessions().remove(uId);

            LOG_INFO("session#%llu disconnected from %s (rx=%llu B, tx=%llu B, total=%zu)",
                     static_cast<unsigned long long>(uId), strPeer.c_str(),
                     static_cast<unsigned long long>(uRx),
                     static_cast<unsigned long long>(uTx), m_stCtx.sessions().size());
        }
    }
}

/* ---------------------------------------------------------------- 协议分发 */

/*
 * 功能描述:  分发dispatch。
 * 作者:      JaTin
 * param [out]  stS              目标会话对象，类型 CSession&。
 * param [in]   stF              输入数据，类型 const Frame_S&。
 * return:    无返回值；处理结果通过成员状态或信号对外体现。
 */
void CReactor::dispatch(CSession& stS, const Frame_S& stF) {
    Json stJ = Json::object();
    if (!stF.payload.empty()) {
        /* allow_exceptions = false */
        stJ = Json::parse(stF.payload, nullptr, false);
        if (stJ.is_discarded()) {
            sendError(stS, "bad_json", "payload is not valid JSON");
            return;
        }
        if (!stJ.is_object()) {
            sendError(stS, "bad_payload", "payload must be a JSON object");
            return;
        }
    }

    /* 业务处理全部由注册表转交，CReactor 不含任何 opcode 分支。 */
    if (!CServiceRegistry::instance().dispatch(stS, stF.op, stJ)) {
        sendError(stS, "unknown_op",
                  std::string("unsupported opcode: ") + opCodeName(stF.op));
    }
}

/* ---------------------------------------------------------------- 出站 */

/*
 * 功能描述:  发送to；实现上异常路径记录错误或警告日志、向目标会话或整个房间下发数据。
 * 作者:      JaTin
 * param [out]  stS              目标会话对象，类型 CSession&。
 * param [in]   enOp             协议操作码，类型 OpCode_EN。
 * param [in]   stJ              输入数据，类型 const Json&。
 * return:    无返回值；处理结果通过成员状态或信号对外体现。
 */
void CReactor::sendTo(CSession& stS, OpCode_EN enOp, const Json& stJ) {
    std::string strFrame;
    try {
        strFrame = encodeFrame(enOp, stJ);
    } catch (const std::exception& stE) {
        LOG_ERROR("encode %s failed: %s", opCodeName(enOp), stE.what());
        return;
    }

    stS.enqueue(strFrame);
    if (stS.flush() == FlushResult_EN::Error) {
        requestClose(stS, "write failed");
        return;
    }
    updateInterest(stS);
}

/*
 * 功能描述:  发送错误；实现上向目标会话或整个房间下发数据。
 * 作者:      JaTin
 * param [out]  stS              目标会话对象，类型 CSession&。
 * param [in]   strCode          输入数据，类型 const std::string&。
 * param [in]   strText          消息文本，类型 const std::string&。
 * return:    无返回值；处理结果通过成员状态或信号对外体现。
 */
void CReactor::sendError(CSession& stS, const std::string& strCode, const std::string& strText) {
    Json stJ = Json::object();
    stJ["code"] = strCode;
    stJ["text"] = strText;
    sendTo(stS, OpCode_EN::ErrorMsg, stJ);
}

/*
 * 功能描述:  广播房间；实现上异常路径记录错误或警告日志、向目标会话或整个房间下发数据。
 * 作者:      JaTin
 * param [in]   strRoomId        输入数据，类型 const std::string&。
 * param [in]   enOp             协议操作码，类型 OpCode_EN。
 * param [in]   stJ              输入数据，类型 const Json&。
 * param [in]   uExceptId        输入数据，类型 uint64_t。
 * return:    无返回值；处理结果通过成员状态或信号对外体现。
 */
void CReactor::broadcastRoom(const std::string& strRoomId, OpCode_EN enOp, const Json& stJ,
                            uint64_t uExceptId) {
    CRoom* pRoom = m_stCtx.rooms().find(strRoomId);
    if (pRoom == nullptr) return;

    /* 帧只编码一次，房间越大收益越明显。 */
    std::string strFrame;
    try {
        strFrame = encodeFrame(enOp, stJ);
    } catch (const std::exception& stE) {
        LOG_ERROR("encode %s failed: %s", opCodeName(enOp), stE.what());
        return;
    }

    pRoom->broadcast([this, &strFrame](CSession& target) {
        target.enqueue(strFrame);
        if (target.flush() == FlushResult_EN::Error) {
            requestClose(target, "write failed");
            return;
        }
        updateInterest(target);
    }, uExceptId);
}

/*
 * 功能描述:  更新interest；实现上向 epoll 实例注册或注销描述符、异常路径记录错误或警告日志。
 * 作者:      JaTin
 * param [out]  stS              目标会话对象，类型 CSession&。
 * return:    无返回值；处理结果通过成员状态或信号对外体现。
 */
void CReactor::updateInterest(CSession& stS) {
    const bool nWant = stS.hasPendingOutput();
    if (nWant == stS.outInterest()) return;

    epoll_event stEv{};
    stEv.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    if (nWant) stEv.events |= EPOLLOUT;
    stEv.data.u64 = stS.id();

    if (::epoll_ctl(m_nEpollFd, EPOLL_CTL_MOD, stS.fd(), &stEv) < 0) {
        LOG_WARN("epoll_ctl MOD fd=%d failed: %s", stS.fd(), std::strerror(errno));
        requestClose(stS, "epoll_ctl MOD failed");
        return;
    }
    stS.setOutInterest(nWant);
}

/*
 * 功能描述:  请求close。
 * 作者:      JaTin
 * param [out]  stS              目标会话对象，类型 CSession&。
 * param [in]   strReason        失败原因描述，类型 const std::string&。
 * return:    无返回值；处理结果通过成员状态或信号对外体现。
 */
void CReactor::requestClose(CSession& stS, const std::string& strReason) {
    if (stS.state() == SessionState_EN::Closing) return;
    stS.setState(SessionState_EN::Closing);
    m_uPendingClose.push_back(stS.id());
    LOG_INFO("session#%llu -> closing (%s)",
             static_cast<unsigned long long>(stS.id()), strReason.c_str());
}

/* namespace dp */
}
