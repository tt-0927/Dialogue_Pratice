/**
 * @file       Session.h
 * @brief      CSession 头文件，声明相关类型与接口。
 * @author     JaTin
 * @date       2026-11-13
 * @note       修改记录：2026-11-14 JaTin 统一为 Doxygen 风格注释。
 */

/**
 * @brief  server/CSession.h。
 */

/**
 * @brief  一个 CSession == 一条已建立的 TCP 连接 + 其上的业务状态。 职责边界：只管「这条连接的 I/O 与身份状态」，不处理业务规则（房间/广播在 CServer 与 CRoom 中）。
 */
#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "common/Protocol.h"
#include "server/OutBuffer.h"

namespace dp {

/**
 * @brief   会话状态EN。
 * @author  JaTin
 */
enum class SessionState_EN {
    /**
     * @brief  已建连，未登录
     */
    Connected,
    /**
     * @brief  已登录，可能不在房间
     */
    LoggedIn,
    /**
     * @brief  待回收（本批次事件处理完后由 CServer 统一关闭）
     */
    Closing,
};

/**
 * @brief   读取处理结果 en。
 * @author  JaTin
 */
enum class ReadResult_EN {
    /**
     * @brief  已读到 EAGAIN，连接正常
     */
    Ok,
    /**
     * @brief  对端有序关闭（recv 返回 0）
     */
    PeerClosed,
    /**
     * @brief  读出错
     */
    Error,
    /**
     * @brief  帧格式非法
     */
    ProtocolError,
};

/**
 * @brief   C会话。
 * @author  JaTin
 */
class CSession {
public:
    /**
     * @brief      构造本对象，完成成员初始化与依赖注入。
     * @param[in] fd             文件描述符；类型 int。
     * @param[in] id             编号；类型 uint64_t。
     * @param[in] peer           输入参数；类型 std::string。
     * @return     构造/析构函数无返回值。
     * @author     JaTin
     */
    CSession(int fd, uint64_t id, std::string peer);
    /**
     * @brief      构造本对象，完成成员初始化与依赖注入。
     * @note       本函数无输入参数。
     * @return     构造/析构函数无返回值。
     * @author     JaTin
     */
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

    /**
     * @brief      边沿触发模式下必须循环读到 EAGAIN，故这里一次读干并解析出全部完整帧。
     * @param[out] frames         协议帧集合；类型 std::vector<Frame_S>&。
     * @return     返回 ReadResult_EN 类型的处理结果。
     * @author     JaTin
     */
    ReadResult_EN      readAvailable(std::vector<Frame_S>& frames);

    /**
     * @brief      入队 enqueue。
     * @param[in] frame          协议帧；类型 const std::string&。
     * @return     无返回值；处理结果通过成员状态或信号对外体现。
     * @author     JaTin
     */
    void            enqueue(const std::string& frame);
    /**
     * @brief      冲刷 flush。
     * @note       本函数无输入参数。
     * @return     返回 FlushResult_EN 类型的处理结果。
     * @author     JaTin
     */
    FlushResult_EN     flush();
    bool            hasPendingOutput() const { return m_stOut.pending() > 0; }
    size_t          pendingOutput() const { return m_stOut.pending(); }

    /**
     * @brief  是否已在 epoll 中订阅 EPOLLOUT，避免每条消息都做一次 epoll_ctl。
     */
    bool            outInterest() const { return m_nOutInterest; }
    void            setOutInterest(bool v) { m_nOutInterest = v; }

    const std::string& protocolError() const { return m_stDecoder.lastError(); }
    uint64_t        rxBytes() const { return m_uRxBytes; }
    uint64_t        txBytes() const { return m_uTxBytes; }

private:
    /**
     * @brief  文件描述符。
     * @note   类型 int；类/结构体成员变量。
     */
    int             m_nFd;
    /**
     * @brief  编号。
     * @note   类型 uint64_t；类/结构体成员变量。
     */
    uint64_t        id_;
    /**
     * @brief  Peer。
     * @note   类型 std::string；类/结构体成员变量。
     */
    std::string     m_strPeer;
    /**
     * @brief  状态值。 初值 = SessionState_EN::Connected。
     * @note   类型 SessionState_EN；类/结构体成员变量。
     */
    SessionState_EN    m_stState = SessionState_EN::Connected;
    /**
     * @brief  名称或昵称。
     * @note   类型 std::string；类/结构体成员变量。
     */
    std::string     m_strName;
    /**
     * @brief  0 表示未填写
     * @note   类型 int；类/结构体成员变量。
     */
    int             m_nAge = 0;
    /**
     * @brief  个人简介。
     * @note   类型 std::string；类/结构体成员变量。
     */
    std::string     m_strDesc;
    /**
     * @brief  房间号。
     * @note   类型 std::string；类/结构体成员变量。
     */
    std::string     m_strRoom;
    /**
     * @brief  LastActive。
     * @note   类型 std::chrono::steady_clock::time_point；类/结构体成员变量。
     */
    std::chrono::steady_clock::time_point m_stLastActive;
    /**
     * @brief  Out。
     * @note   类型 COutBuffer；类/结构体成员变量。
     */
    COutBuffer       m_stOut;
    /**
     * @brief  Decoder。
     * @note   类型 CFrameDecoder；类/结构体成员变量。
     */
    CFrameDecoder    m_stDecoder;
    /**
     * @brief  OutInterest。 初值 = false。
     * @note   类型 bool；类/结构体成员变量。
     */
    bool            m_nOutInterest = false;
    /**
     * @brief  Rx字节数。 初值 = 0。
     * @note   类型 uint64_t；类/结构体成员变量。
     */
    uint64_t        m_uRxBytes = 0;
    /**
     * @brief  Tx字节数。 初值 = 0。
     * @note   类型 uint64_t；类/结构体成员变量。
     */
    uint64_t        m_uTxBytes = 0;
};

/**
 * @brief  namespace dp。
 */
}
