/*
 * 文件名:    Session.h
 * 创建日期:  2026-11-13
 * 作者:      JaTin
 * 功能描述:  CSession 头文件，声明相关类型与接口。
 * 修改记录:  2026-11-13 JaTin 按 C/C++ 编码规范整改注释与命名。
 */

/* server/CSession.h */

/* 一个 CSession == 一条已建立的 TCP 连接 + 其上的业务状态。 */
/* 职责边界：只管「这条连接的 I/O 与身份状态」，不处理业务规则（房间/广播在 CServer 与 CRoom 中）。 */
#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "common/Protocol.h"
#include "server/OutBuffer.h"

namespace dp {

enum class SessionState_EN {
    /* 已建连，未登录 */
    Connected,
    /* 已登录，可能不在房间 */
    LoggedIn,
    /* 待回收（本批次事件处理完后由 CServer 统一关闭） */
    Closing,
};

enum class ReadResult_EN {
    /* 已读到 EAGAIN，连接正常 */
    Ok,
    /* 对端有序关闭（recv 返回 0） */
    PeerClosed,
    /* 读出错 */
    Error,
    /* 帧格式非法 */
    ProtocolError,
};

class CSession {
public:
    CSession(int fd, uint64_t id, std::string peer);
    ~CSession();

    CSession(const CSession&) = delete;
    CSession& operator=(const CSession&) = delete;

    int             fd() const { return m_nFd; }
    uint64_t        id() const { return id_; }
    const std::string& peer() const { return m_strPeer; }

    SessionState_EN    state() const { return m_stState; }
    void            setState(SessionState_EN s) { m_stState = s; }
    bool            loggedIn() const { return m_stState == SessionState_EN::LoggedIn; }

    const std::string& name() const { return m_strName; }
    void            setName(std::string n) { m_strName = std::move(n); }

    int             age() const { return m_nAge; }
    void            setAge(int a) { m_nAge = a; }

    const std::string& desc() const { return m_strDesc; }
    void            setDesc(std::string d) { m_strDesc = std::move(d); }

    const std::string& room() const { return m_strRoom; }
    void            setRoom(std::string r) { m_strRoom = std::move(r); }
    void            clearRoom() { m_strRoom.clear(); }

    void            touch() { m_stLastActive = std::chrono::steady_clock::now(); }
    std::chrono::steady_clock::time_point lastActive() const { return m_stLastActive; }

    /* 边沿触发模式下必须循环读到 EAGAIN，故这里一次读干并解析出全部完整帧。 */
    ReadResult_EN      readAvailable(std::vector<Frame_S>& frames);

    void            enqueue(const std::string& frame);
    FlushResult_EN     flush();
    bool            hasPendingOutput() const { return m_stOut.pending() > 0; }
    size_t          pendingOutput() const { return m_stOut.pending(); }

    /* 是否已在 epoll 中订阅 EPOLLOUT，避免每条消息都做一次 epoll_ctl。 */
    bool            outInterest() const { return m_nOutInterest; }
    void            setOutInterest(bool v) { m_nOutInterest = v; }

    const std::string& protocolError() const { return m_stDecoder.lastError(); }
    uint64_t        rxBytes() const { return m_uRxBytes; }
    uint64_t        txBytes() const { return m_uTxBytes; }

private:
    int             m_nFd;
    uint64_t        id_;
    std::string     m_strPeer;
    SessionState_EN    m_stState = SessionState_EN::Connected;
    std::string     m_strName;
    /* 0 表示未填写 */
    int             m_nAge = 0;
    std::string     m_strDesc;
    std::string     m_strRoom;
    std::chrono::steady_clock::time_point m_stLastActive;
    COutBuffer       m_stOut;
    CFrameDecoder    m_stDecoder;
    bool            m_nOutInterest = false;
    uint64_t        m_uRxBytes = 0;
    uint64_t        m_uTxBytes = 0;
};

/* namespace dp */
}
