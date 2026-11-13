// server/Session.h
//
// 一个 Session == 一条已建立的 TCP 连接 + 其上的业务状态。
// 职责边界：只管「这条连接的 I/O 与身份状态」，不处理业务规则（房间/广播在 Server 与 Room 中）。
#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "common/Protocol.h"
#include "server/OutBuffer.h"

namespace dp {

enum class SessionState {
    Connected,   // 已建连，未登录
    LoggedIn,    // 已登录，可能不在房间
    Closing,     // 待回收（本批次事件处理完后由 Server 统一关闭）
};

enum class ReadResult {
    Ok,            // 已读到 EAGAIN，连接正常
    PeerClosed,    // 对端有序关闭（recv 返回 0）
    Error,         // 读出错
    ProtocolError, // 帧格式非法
};

class Session {
public:
    Session(int fd, uint64_t id, std::string peer);
    ~Session();

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    int             fd() const { return fd_; }
    uint64_t        id() const { return id_; }
    const std::string& peer() const { return peer_; }

    SessionState    state() const { return state_; }
    void            setState(SessionState s) { state_ = s; }
    bool            loggedIn() const { return state_ == SessionState::LoggedIn; }

    const std::string& name() const { return name_; }
    void            setName(std::string n) { name_ = std::move(n); }

    int             age() const { return age_; }
    void            setAge(int a) { age_ = a; }

    const std::string& desc() const { return desc_; }
    void            setDesc(std::string d) { desc_ = std::move(d); }

    const std::string& room() const { return room_; }
    void            setRoom(std::string r) { room_ = std::move(r); }
    void            clearRoom() { room_.clear(); }

    void            touch() { lastActive_ = std::chrono::steady_clock::now(); }
    std::chrono::steady_clock::time_point lastActive() const { return lastActive_; }

    /// 边沿触发模式下必须循环读到 EAGAIN，故这里一次读干并解析出全部完整帧。
    ReadResult      readAvailable(std::vector<Frame>& frames);

    void            enqueue(const std::string& frame);
    FlushResult     flush();
    bool            hasPendingOutput() const { return out_.pending() > 0; }
    size_t          pendingOutput() const { return out_.pending(); }

    /// 是否已在 epoll 中订阅 EPOLLOUT，避免每条消息都做一次 epoll_ctl。
    bool            outInterest() const { return outInterest_; }
    void            setOutInterest(bool v) { outInterest_ = v; }

    const std::string& protocolError() const { return decoder_.lastError(); }
    uint64_t        rxBytes() const { return rxBytes_; }
    uint64_t        txBytes() const { return txBytes_; }

private:
    int             fd_;
    uint64_t        id_;
    std::string     peer_;
    SessionState    state_ = SessionState::Connected;
    std::string     name_;
    int             age_ = 0;        // 0 表示未填写
    std::string     desc_;
    std::string     room_;
    std::chrono::steady_clock::time_point lastActive_;
    OutBuffer       out_;
    FrameDecoder    decoder_;
    bool            outInterest_ = false;
    uint64_t        rxBytes_ = 0;
    uint64_t        txBytes_ = 0;
};

}  // namespace dp
