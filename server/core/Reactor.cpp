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

CReactor::CReactor(CServerContext& ctx) : m_stCtx(ctx) {}

/*
 * 功能描述:  执行 CReactor 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * return:    返回值，含义见功能描述。
 */
CReactor::~CReactor() {
    shutdownAll();
}

/* ---------------------------------------------------------------- 启动 / 关闭 */

/*
 * 功能描述:  执行 setupSignalFd 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * return:    返回值，含义见功能描述。
 */
bool CReactor::setupSignalFd() {
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGPIPE);

    /* 先屏蔽，再用 signalfd 统一从事件循环里取信号，避免异步打断 epoll_wait。 */
    if (::sigprocmask(SIG_BLOCK, &mask, nullptr) < 0) {
        LOG_ERROR("sigprocmask: %s", std::strerror(errno));
        return false;
    }
    m_nSignalFd = ::signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    if (m_nSignalFd < 0) {
        LOG_ERROR("signalfd: %s", std::strerror(errno));
        return false;
    }
    return true;
}

/*
 * 功能描述:  执行 setupListenSocket 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * return:    返回值，含义见功能描述。
 */
bool CReactor::setupListenSocket() {
    const ServerConfig_S& cfg = m_stCtx.config();

    const int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        LOG_ERROR("socket: %s", std::strerror(errno));
        return false;
    }

    int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(cfg.port);
    if (::inet_pton(AF_INET, cfg.bindAddr.c_str(), &addr.sin_addr) != 1) {
        LOG_ERROR("invalid bind address: %s", cfg.bindAddr.c_str());
        ::close(fd);
        return false;
    }
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        LOG_ERROR("bind %s:%u failed: %s", cfg.bindAddr.c_str(), cfg.port, std::strerror(errno));
        ::close(fd);
        return false;
    }
    if (::listen(fd, cfg.backlog) < 0) {
        LOG_ERROR("listen failed: %s", std::strerror(errno));
        ::close(fd);
        return false;
    }

    m_nListenFd = fd;
    return true;
}

/*
 * 功能描述:  执行 setupEpoll 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * return:    返回值，含义见功能描述。
 */
bool CReactor::setupEpoll() {
    m_nEpollFd = ::epoll_create1(EPOLL_CLOEXEC);
    if (m_nEpollFd < 0) {
        LOG_ERROR("epoll_create1: %s", std::strerror(errno));
        return false;
    }

    /* 监听 fd 同样用边沿触发：accept 必须循环到 EAGAIN，否则会漏掉新连接。 */
    epoll_event ev{};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.u64 = kListenId;
    if (::epoll_ctl(m_nEpollFd, EPOLL_CTL_ADD, m_nListenFd, &ev) < 0) {
        LOG_ERROR("epoll_ctl ADD listen: %s", std::strerror(errno));
        return false;
    }

    epoll_event sev{};
    sev.events = EPOLLIN;
    sev.data.u64 = kSignalId;
    if (::epoll_ctl(m_nEpollFd, EPOLL_CTL_ADD, m_nSignalFd, &sev) < 0) {
        LOG_ERROR("epoll_ctl ADD signalfd: %s", std::strerror(errno));
        return false;
    }
    return true;
}

/*
 * 功能描述:  执行 start 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * return:    返回值，含义见功能描述。
 */
bool CReactor::start() {
    if (!setupSignalFd()) return false;
    if (!setupListenSocket()) return false;
    if (!setupEpoll()) return false;

    const ServerConfig_S& cfg = m_stCtx.config();
    LOG_INFO("dialogue server ready | listen=%s:%u backlog=%d heartbeat=%ds maxConn=%zu",
             cfg.bindAddr.c_str(), cfg.port, cfg.backlog,
             cfg.heartbeatTimeoutSec, cfg.maxConnections);
    LOG_INFO("history | keep=%zu msgs / %zu KiB per room, send<=%zu msgs / %zu KiB, roomTtl=%ds",
             cfg.historyEntries, cfg.historyBytes / 1024,
             cfg.historySendLimit, cfg.historySendBytes / 1024, cfg.roomTtlSec);
    return true;
}

/*
 * 功能描述:  执行 shutdownAll 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * return:    void
 */
void CReactor::shutdownAll() {
    CSessionManager& sessions = m_stCtx.sessions();

    if (!sessions.all().empty()) {
        LOG_INFO("closing %zu session(s)", sessions.size());
    }
    for (auto& kv : sessions.all()) {
        CSession* s = kv.second.get();
        if (m_nEpollFd >= 0 && s->fd() >= 0) {
            ::epoll_ctl(m_nEpollFd, EPOLL_CTL_DEL, s->fd(), nullptr);
        }
    }
    /* CSessionManager 无 clear()，逐个删除以触发 fd 关闭。 */
    while (!sessions.all().empty()) {
        sessions.remove(sessions.all().begin()->first);
    }

    if (m_nListenFd >= 0) { ::close(m_nListenFd); m_nListenFd = -1; }
    if (m_nSignalFd >= 0) { ::close(m_nSignalFd); m_nSignalFd = -1; }
    if (m_nEpollFd >= 0)  { ::close(m_nEpollFd);  m_nEpollFd = -1; }
}

/* ---------------------------------------------------------------- 事件循环 */

/*
 * 功能描述:  执行 run 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * return:    void
 */
void CReactor::run() {
    m_nRunning = true;
    std::vector<epoll_event> events(kMaxEvents);
    auto lastTick = std::chrono::steady_clock::now();

    while (m_nRunning) {
        const int n = ::epoll_wait(m_nEpollFd, events.data(), kMaxEvents, 1000);
        if (n < 0) {
            if (errno == EINTR) continue;
            LOG_ERROR("epoll_wait: %s", std::strerror(errno));
            break;
        }

        for (int i = 0; i < n && m_nRunning; ++i) {
            const uint32_t ev = events[i].events;
            const uint64_t id = events[i].data.u64;

            if (id == kListenId) {
                handleAccept();
                continue;
            }
            if (id == kSignalId) {
                handleSignal();
                continue;
            }

            CSession* s = m_stCtx.sessions().find(id);
            if (s == nullptr) continue;

            if (ev & (EPOLLHUP | EPOLLERR)) {
                requestClose(*s, "hangup");
                continue;
            }
            if (ev & EPOLLIN) {
                handleReadable(*s);
            }
            if ((ev & EPOLLOUT) && s->state() != SessionState_EN::Closing) {
                handleWritable(*s);
            }
            if ((ev & EPOLLRDHUP) && s->state() != SessionState_EN::Closing) {
                requestClose(*s, "peer shutdown");
            }
        }

        /* 统一回收：保证任何 handler 都不会在遍历过程中销毁 CSession。 */
        reapSessions();

        const auto now = std::chrono::steady_clock::now();
        if (now - lastTick >= std::chrono::seconds(1)) {
            lastTick = now;
            handleTimer();
            reapSessions();
        }
    }

    LOG_INFO("event loop stopped");
}

/*
 * 功能描述:  执行 handleAccept 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * return:    void
 */
void CReactor::handleAccept() {
    CSessionManager& sessions = m_stCtx.sessions();
    const ServerConfig_S& cfg = m_stCtx.config();

    for (;;) {
        sockaddr_in addr{};
        socklen_t addrLen = sizeof(addr);
        const int fd = ::accept4(m_nListenFd, reinterpret_cast<sockaddr*>(&addr), &addrLen,
                                 SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd < 0) {
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

        if (sessions.size() >= cfg.maxConnections) {
            LOG_WARN("max connections (%zu) reached, rejecting fd=%d", cfg.maxConnections, fd);
            ::close(fd);
            continue;
        }

        int one = 1;
        ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        ::setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));

        char ip[INET_ADDRSTRLEN] = {0};
        ::inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
        std::string peer = std::string(ip) + ":" + std::to_string(ntohs(addr.sin_port));

        CSession* s = sessions.add(fd, std::move(peer));

        epoll_event ev{};
        ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
        ev.data.u64 = s->id();
        if (::epoll_ctl(m_nEpollFd, EPOLL_CTL_ADD, fd, &ev) < 0) {
            LOG_ERROR("epoll_ctl ADD fd=%d: %s", fd, std::strerror(errno));
            sessions.remove(s->id());
            continue;
        }

        LOG_INFO("session#%llu connected from %s (total=%zu)",
                 static_cast<unsigned long long>(s->id()), s->peer().c_str(), sessions.size());
    }
}

/*
 * 功能描述:  执行 handleSignal 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * return:    void
 */
void CReactor::handleSignal() {
    signalfd_siginfo si{};
    for (;;) {
        const ssize_t n = ::read(m_nSignalFd, &si, sizeof(si));
        if (n != static_cast<ssize_t>(sizeof(si))) return;
        if (si.ssi_signo == SIGINT || si.ssi_signo == SIGTERM) {
            LOG_INFO("received signal %u, stopping", si.ssi_signo);
            m_nRunning = false;
            return;
        }
    }
}

/*
 * 功能描述:  执行 handleReadable 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [out]  s  参数。
 * return:    void
 */
void CReactor::handleReadable(CSession& s) {
    std::vector<Frame_S> frames;
    const ReadResult_EN r = s.readAvailable(frames);

    if (!frames.empty()) s.touch();

    for (const Frame_S& f : frames) {
        dispatch(s, f);
        if (s.state() == SessionState_EN::Closing) return;
    }

    switch (r) {
        case ReadResult_EN::Ok:
            break;
        case ReadResult_EN::PeerClosed:
            requestClose(s, "peer closed");
            break;
        case ReadResult_EN::Error:
            requestClose(s, std::string("read error: ") + std::strerror(errno));
            break;
        case ReadResult_EN::ProtocolError:
            sendError(s, "protocol_error", s.protocolError());
            requestClose(s, "protocol error: " + s.protocolError());
            break;
    }
}

/*
 * 功能描述:  执行 handleWritable 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [out]  s  参数。
 * return:    void
 */
void CReactor::handleWritable(CSession& s) {
    if (s.flush() == FlushResult_EN::Error) {
        requestClose(s, "write failed");
        return;
    }
    updateInterest(s);
}

/*
 * 功能描述:  执行 handleTimer 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * return:    void
 */
void CReactor::handleTimer() {
    const auto now = std::chrono::steady_clock::now();
    const auto timeout = std::chrono::seconds(m_stCtx.config().heartbeatTimeoutSec);

    std::vector<uint64_t> expired;
    for (const auto& kv : m_stCtx.sessions().all()) {
        CSession* s = kv.second.get();
        if (s->state() == SessionState_EN::Closing) continue;
        if (now - s->lastActive() > timeout) expired.push_back(s->id());
    }
    for (uint64_t id : expired) {
        CSession* s = m_stCtx.sessions().find(id);
        if (s != nullptr) requestClose(*s, "heartbeat timeout");
    }

    const uint64_t ms = nowMs();
    if (ms >= m_uNextStatsAt) {
        m_uNextStatsAt = ms + 60000;
        size_t loggedIn = 0;
        for (const auto& kv : m_stCtx.sessions().all()) {
            if (kv.second->loggedIn()) ++loggedIn;
        }
        LOG_INFO("stats | sessions=%zu loggedIn=%zu rooms=%zu",
                 m_stCtx.sessions().size(), loggedIn, m_stCtx.rooms().size());
    }

    /* 周期性维护（数据归档等）：CReactor 只管每秒调一次，频率由钩子决定 */
    if (secondHook_) secondHook_();

    /* 回收超过滞留期的空房间（同时释放其历史消息内存） */
    const int ttl = m_stCtx.config().roomTtlSec;
    if (ttl > 0) {
        const int64_t deadline = static_cast<int64_t>(ms) - static_cast<int64_t>(ttl) * 1000;
        std::vector<std::string> rooms;
        for (const auto& kv : m_stCtx.rooms().all()) {
            const CRoom* r = kv.second.get();
            if (!r->isLingering()) continue;
            if (r->emptySince() <= deadline) rooms.push_back(kv.first);
        }
        for (const std::string& id : rooms) {
            m_stCtx.rooms().removeIfEmpty(id);
            LOG_INFO("room '%s' expired after %ds idle", id.c_str(), ttl);
        }
    }
}

/*
 * 功能描述:  执行 reapSessions 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * return:    void
 */
void CReactor::reapSessions() {
    /* 关闭动作可能级联（关闭时广播导致对端写失败），故循环交换批次。 */
    while (!m_uPendingClose.empty()) {
        std::vector<uint64_t> batch;
        batch.swap(m_uPendingClose);

        for (uint64_t id : batch) {
            CSession* s = m_stCtx.sessions().find(id);
            if (s == nullptr) continue;

            /* 业务清理（退出房间、通知房友）通过钩子交给 business 层， */
            /* CReactor 本身不理解房间语义。 */
            if (m_stCloseHook) m_stCloseHook(*s);

            const int fd = s->fd();
            const uint64_t rx = s->rxBytes();
            const uint64_t tx = s->txBytes();
            const std::string peer = s->peer();

            if (m_nEpollFd >= 0 && fd >= 0) {
                ::epoll_ctl(m_nEpollFd, EPOLL_CTL_DEL, fd, nullptr);
            }
            /* 析构中 close(fd) */
            m_stCtx.sessions().remove(id);

            LOG_INFO("session#%llu disconnected from %s (rx=%llu B, tx=%llu B, total=%zu)",
                     static_cast<unsigned long long>(id), peer.c_str(),
                     static_cast<unsigned long long>(rx),
                     static_cast<unsigned long long>(tx), m_stCtx.sessions().size());
        }
    }
}

/* ---------------------------------------------------------------- 协议分发 */

/*
 * 功能描述:  执行 dispatch 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [out]  s  参数。
 * param [in]  f  参数。
 * return:    void
 */
void CReactor::dispatch(CSession& s, const Frame_S& f) {
    Json j = Json::object();
    if (!f.payload.empty()) {
        /* allow_exceptions = false */
        j = Json::parse(f.payload, nullptr, false);
        if (j.is_discarded()) {
            sendError(s, "bad_json", "payload is not valid JSON");
            return;
        }
        if (!j.is_object()) {
            sendError(s, "bad_payload", "payload must be a JSON object");
            return;
        }
    }

    /* 业务处理全部由注册表转交，CReactor 不含任何 opcode 分支。 */
    if (!CServiceRegistry::instance().dispatch(s, f.op, j)) {
        sendError(s, "unknown_op",
                  std::string("unsupported opcode: ") + opCodeName(f.op));
    }
}

/* ---------------------------------------------------------------- 出站 */

/*
 * 功能描述:  执行 sendTo 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [out]  s  参数。
 * param [in]  op  参数。
 * param [in]  j  参数。
 * return:    void
 */
void CReactor::sendTo(CSession& s, OpCode_EN op, const Json& j) {
    std::string frame;
    try {
        frame = encodeFrame(op, j);
    } catch (const std::exception& e) {
        LOG_ERROR("encode %s failed: %s", opCodeName(op), e.what());
        return;
    }

    s.enqueue(frame);
    if (s.flush() == FlushResult_EN::Error) {
        requestClose(s, "write failed");
        return;
    }
    updateInterest(s);
}

/*
 * 功能描述:  执行 sendError 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [out]  s  参数。
 * param [in]  code  参数。
 * param [in]  text  参数。
 * return:    void
 */
void CReactor::sendError(CSession& s, const std::string& code, const std::string& text) {
    Json j = Json::object();
    j["code"] = code;
    j["text"] = text;
    sendTo(s, OpCode_EN::ErrorMsg, j);
}

void CReactor::broadcastRoom(const std::string& roomId, OpCode_EN op, const Json& j,
                            uint64_t exceptId) {
    CRoom* room = m_stCtx.rooms().find(roomId);
    if (room == nullptr) return;

    /* 帧只编码一次，房间越大收益越明显。 */
    std::string frame;
    try {
        frame = encodeFrame(op, j);
    } catch (const std::exception& e) {
        LOG_ERROR("encode %s failed: %s", opCodeName(op), e.what());
        return;
    }

    room->broadcast([this, &frame](CSession& target) {
        target.enqueue(frame);
        if (target.flush() == FlushResult_EN::Error) {
            requestClose(target, "write failed");
            return;
        }
        updateInterest(target);
    }, exceptId);
}

/*
 * 功能描述:  执行 updateInterest 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [out]  s  参数。
 * return:    void
 */
void CReactor::updateInterest(CSession& s) {
    const bool want = s.hasPendingOutput();
    if (want == s.outInterest()) return;

    epoll_event ev{};
    ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    if (want) ev.events |= EPOLLOUT;
    ev.data.u64 = s.id();

    if (::epoll_ctl(m_nEpollFd, EPOLL_CTL_MOD, s.fd(), &ev) < 0) {
        LOG_WARN("epoll_ctl MOD fd=%d failed: %s", s.fd(), std::strerror(errno));
        requestClose(s, "epoll_ctl MOD failed");
        return;
    }
    s.setOutInterest(want);
}

/*
 * 功能描述:  执行 requestClose 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [out]  s  参数。
 * param [in]  reason  参数。
 * return:    void
 */
void CReactor::requestClose(CSession& s, const std::string& reason) {
    if (s.state() == SessionState_EN::Closing) return;
    s.setState(SessionState_EN::Closing);
    m_uPendingClose.push_back(s.id());
    LOG_INFO("session#%llu -> closing (%s)",
             static_cast<unsigned long long>(s.id()), reason.c_str());
}

/* namespace dp */
}
