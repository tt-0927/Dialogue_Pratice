/*
 * 文件名:    Session.h
 * 创建日期:  2026-11-13
 * 作者:      JaTin
 * 功能描述:  一条 TCP 连接及其上的身份状态。
 * 修改记录:  2026-11-13 JaTin 按 C/C++ 编码规范整改注释与命名。
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "common/Protocol.h"
#include "server/core/OutBuffer.h"

namespace dp {

/**
 * @brief 会话所处阶段。
 * @details `Closing` 只是标记，真正的回收发生在当前事件批次结束之后，
 *          避免在遍历容器时销毁元素。
 */
enum class SessionState_EN {
    /* < 已建连，尚未登录 */
    Connected,
    /* < 已登录，可能尚未进入房间 */
    LoggedIn,
    /* < 待回收（批次结束后由 CReactor 统一关闭） */
    Closing,
};

/**
 * @brief 一次 readAvailable() 的结果。
 */
enum class ReadResult_EN {
    /* < 已读到 EAGAIN，连接正常 */
    Ok,
    /* < 对端有序关闭（recv 返回 0） */
    PeerClosed,
    /* < 读出错，errno 有效 */
    Error,
    /* < 帧格式非法，原因见 protocolError() */
    ProtocolError,
};

/**
 * @brief 一条连接。
 *
 * @details 同时承担三件事：非阻塞读写、帧解码、身份与房间归属。
 *          所有方法都只在事件循环线程调用，不加锁。
 */
class CSession {
public:
    /**
     * @brief 接管一个已建立的 socket。
     * @param fd   非阻塞 socket，所有权转移给本对象（析构时 close）。
     * @param id   会话 id，由 CSessionManager 分配，全局单调递增。
     * @param peer 对端地址描述，仅用于日志。
     */
    CSession(int fd, uint64_t id, std::string peer);

    ~CSession();

    CSession(const CSession&) = delete;
    CSession& operator=(const CSession&) = delete;

    /* 底层 socket 描述符。 */
    int fd() const { return m_nFd; }

    /* 会话 id，广播与去重都依赖它。 */
    uint64_t id() const { return id_; }

    /* 对端地址，形如 `192.168.1.5:52344`。 */
    const std::string& peer() const { return m_strPeer; }

    /* 当前阶段。 */
    SessionState_EN state() const { return m_stState; }

    /* 设置阶段。 */
    void setState(SessionState_EN s) { m_stState = s; }

    /* 是否已完成登录。 */
    bool loggedIn() const { return m_stState == SessionState_EN::LoggedIn; }

    /* 本连接已尝试注册的次数（用于限流，防止单连接刷号）。 */
    int registerAttempts() const { return m_nRegisterAttempts; }

    /* 记录一次注册尝试。 */
    void bumpRegisterAttempts() { ++m_nRegisterAttempts; }

    /* 持久用户 id（用户库里的 id），0 表示未匹配到账号。 */
    /* 与 id() 不同：id() 是每次连接的临时编号，重连就变； */
    /* 本字段在用户库里有对应记录时才非 0。 */
    int64_t accountId() const { return m_uAccountId; }

    /* 设置持久用户 id。 */
    void setAccountId(int64_t id) { m_uAccountId = id; }

    /* 昵称。 */
    const std::string& name() const { return m_strName; }

    /* 设置昵称（长度与字符集由 business 层校验）。 */
    void setName(std::string n) { m_strName = std::move(n); }

    /* 年龄，0 表示未填写。 */
    int age() const { return m_nAge; }

    /* 设置年龄。 */
    void setAge(int a) { m_nAge = a; }

    /* 个人简介。 */
    const std::string& desc() const { return m_strDesc; }

    /* 设置个人简介。 */
    void setDesc(std::string d) { m_strDesc = std::move(d); }

    /* 当前所在房间号，空串表示不在任何房间。 */
    const std::string& room() const { return m_strRoom; }

    /* 设置房间归属（由 CRoomService 维护）。 */
    void setRoom(std::string r) { m_strRoom = std::move(r); }

    /* 清空房间归属（离房时调用）。 */
    void clearRoom() { m_strRoom.clear(); }

    /* 刷新最后活跃时间，心跳判定依据。 */
    void touch() { m_stLastActive = std::chrono::steady_clock::now(); }

    /* 最后活跃时间点。 */
    std::chrono::steady_clock::time_point lastActive() const { return m_stLastActive; }

    /**
     * @brief 一次读干 socket 并解出所有完整帧。
     * @param frames 输出参数，追加解析出的完整帧（不清空原有内容）。
     * @retval Ok            读到 EAGAIN，连接正常。
     * @retval PeerClosed    对端有序关闭。
     * @retval Error         读出错，errno 有效。
     * @retval ProtocolError 帧格式非法，原因见 protocolError()。
     * @details 边沿触发模式下必须循环读到 EAGAIN，否则会漏事件。
     */
    ReadResult_EN readAvailable(std::vector<Frame_S>& frames);

    /**
     * @brief 追加一帧到发送缓冲。
     * @param frame 已编码的完整帧（含 4 字节长度前缀）。
     */
    void enqueue(const std::string& frame);

    /**
     * @brief 尽量把发送缓冲写空。
     * @retval Done    全部写完。
     * @retval Blocked 内核发送缓冲满，需等 EPOLLOUT。
     * @retval Error   写失败，连接应关闭。
     */
    FlushResult_EN flush();

    /* 是否还有待发数据。 */
    bool hasPendingOutput() const { return m_stOut.pending() > 0; }

    /* 待发字节数。 */
    size_t pendingOutput() const { return m_stOut.pending(); }

    /* 是否已在 epoll 中订阅 EPOLLOUT（避免每条消息都做一次 epoll_ctl）。 */
    bool outInterest() const { return m_nOutInterest; }

    /* 记录 EPOLLOUT 订阅状态。 */
    void setOutInterest(bool v) { m_nOutInterest = v; }

    /* 最近一次协议错误的说明。 */
    const std::string& protocolError() const { return m_stDecoder.lastError(); }

    /* 累计接收字节数（统计用）。 */
    uint64_t rxBytes() const { return m_uRxBytes; }

    /* 累计发送字节数（统计用）。 */
    uint64_t txBytes() const { return m_uTxBytes; }

private:
    /* < socket 描述符，析构时关闭 */
    int             m_nFd;
    /* < 会话 id */
    uint64_t        id_;
    /* < 对端地址（日志用） */
    std::string     m_strPeer;
    /* < 当前阶段 */
    SessionState_EN    m_stState = SessionState_EN::Connected;
    /* < 持久用户 id（0 = 匿名） */
    int64_t         m_uAccountId = 0;
    /* < 本连接注册尝试次数 */
    int             m_nRegisterAttempts = 0;
    /* < 昵称 */
    std::string     m_strName;
    /* < 年龄，0 表示未填写 */
    int             m_nAge = 0;
    /* < 个人简介 */
    std::string     m_strDesc;
    /* < 所在房间号 */
    std::string     m_strRoom;
    /* < 最后活跃时间 */
    std::chrono::steady_clock::time_point m_stLastActive;
    /* < 发送缓冲 */
    COutBuffer       m_stOut;
    /* < 接收侧帧解码器 */
    CFrameDecoder    m_stDecoder;
    /* < 是否已订阅 EPOLLOUT */
    bool            m_nOutInterest = false;
    /* < 累计接收字节 */
    uint64_t        m_uRxBytes = 0;
    /* < 累计发送字节 */
    uint64_t        m_uTxBytes = 0;
};

/* namespace dp */
}
