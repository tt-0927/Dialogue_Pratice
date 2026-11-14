/**
 * @file       Session.h
 * @brief      一条 TCP 连接及其上的身份状态。
 * @author     JaTin
 * @date       2026-11-13
 * @note       修改记录：2026-11-14 JaTin 统一为 Doxygen 风格注释。
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
 * @brief   会话所处阶段。
 * @details `Closing` 只是标记，真正的回收发生在当前事件批次结束之后， 避免在遍历容器时销毁元素。
 * @author  JaTin
 */
enum class SessionState_EN {
    /**
     * @brief  已建连，尚未登录
     */
    Connected,
    /**
     * @brief  已登录，可能尚未进入房间
     */
    LoggedIn,
    /**
     * @brief  待回收（批次结束后由 CReactor 统一关闭）
     */
    Closing,
};

/**
 * @brief   一次 readAvailable() 的结果。
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
     * @brief  读出错，errno 有效
     */
    Error,
    /**
     * @brief  帧格式非法，原因见 protocolError()
     */
    ProtocolError,
};

/**
 * @brief   一条连接。
 * @details 同时承担三件事：非阻塞读写、帧解码、身份与房间归属。 所有方法都只在事件循环线程调用，不加锁。
 * @author  JaTin
 */
class CSession {
public:
    /**
     * @brief      接管一个已建立的 socket。
     * @param[in] fd             非阻塞 socket，所有权转移给本对象（析构时 close）；类型 int。
     * @param[in] id             会话 id，由 CSessionManager 分配，全局单调递增；类型 uint64_t。
     * @param[in] peer           对端地址描述，仅用于日志；类型 std::string。
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

    /**
     * @brief  底层 socket 描述符。
     */
    int fd() const { return m_nFd; }

    /**
     * @brief  会话 id，广播与去重都依赖它。
     */
    uint64_t id() const { return id_; }

    /**
     * @brief  对端地址，形如 `192.168.1.5:52344`。
     */
    const std::string& peer() const { return m_strPeer; }

    /**
     * @brief  当前阶段。
     */
    SessionState_EN state() const { return m_stState; }

    /**
     * @brief  设置阶段。
     */
    void setState(SessionState_EN s) { m_stState = s; }

    /**
     * @brief  是否已完成登录。
     */
    bool loggedIn() const { return m_stState == SessionState_EN::LoggedIn; }

    /**
     * @brief  本连接已尝试注册的次数（用于限流，防止单连接刷号）。
     */
    int registerAttempts() const { return m_nRegisterAttempts; }

    /**
     * @brief  记录一次注册尝试。
     */
    void bumpRegisterAttempts() { ++m_nRegisterAttempts; }

    /**
     * @brief  持久用户 id（用户库里的 id），0 表示未匹配到账号。 与 id() 不同：id() 是每次连接的临时编号，重连就变； 本字段在用户库里有对应记录时才非 0。
     */
    int64_t accountId() const { return m_uAccountId; }

    /**
     * @brief  设置持久用户 id。
     */
    void setAccountId(int64_t id) { m_uAccountId = id; }

    /**
     * @brief  昵称。
     */
    const std::string& name() const { return m_strName; }

    /**
     * @brief  设置昵称（长度与字符集由 business 层校验）。
     */
    void setName(std::string n) { m_strName = std::move(n); }

    /**
     * @brief  年龄，0 表示未填写。
     */
    int age() const { return m_nAge; }

    /**
     * @brief  设置年龄。
     */
    void setAge(int a) { m_nAge = a; }

    /**
     * @brief  个人简介。
     */
    const std::string& desc() const { return m_strDesc; }

    /**
     * @brief  设置个人简介。
     */
    void setDesc(std::string d) { m_strDesc = std::move(d); }

    /**
     * @brief  当前所在房间号，空串表示不在任何房间。
     */
    const std::string& room() const { return m_strRoom; }

    /**
     * @brief  设置房间归属（由 CRoomService 维护）。
     */
    void setRoom(std::string r) { m_strRoom = std::move(r); }

    /**
     * @brief  清空房间归属（离房时调用）。
     */
    void clearRoom() { m_strRoom.clear(); }

    /**
     * @brief  刷新最后活跃时间，心跳判定依据。
     */
    void touch() { m_stLastActive = std::chrono::steady_clock::now(); }

    /**
     * @brief  最后活跃时间点。
     */
    std::chrono::steady_clock::time_point lastActive() const { return m_stLastActive; }

    /**
     * @brief      一次读干 socket 并解出所有完整帧。
     * @details    边沿触发模式下必须循环读到 EAGAIN，否则会漏事件。
     * @param[out] frames         输出参数，追加解析出的完整帧（不清空原有内容）；类型 std::vector<Frame_S>&。
     * @return     Ok            读到 EAGAIN，连接正常；PeerClosed    对端有序关闭；Error         读出错，errno 有效；ProtocolError 帧格式非法，原因见 protocolError()。
     * @author     JaTin
     */
    ReadResult_EN readAvailable(std::vector<Frame_S>& frames);

    /**
     * @brief      追加一帧到发送缓冲。
     * @param[in] frame          已编码的完整帧（含 4 字节长度前缀）；类型 const std::string&。
     * @return     无返回值；处理结果通过成员状态或信号对外体现。
     * @author     JaTin
     */
    void enqueue(const std::string& frame);

    /**
     * @brief      尽量把发送缓冲写空。
     * @note       本函数无输入参数。
     * @return     Done    全部写完；Blocked 内核发送缓冲满，需等 EPOLLOUT；Error   写失败，连接应关闭。
     * @author     JaTin
     */
    FlushResult_EN flush();

    /**
     * @brief  是否还有待发数据。
     */
    bool hasPendingOutput() const { return m_stOut.pending() > 0; }

    /**
     * @brief  待发字节数。
     */
    size_t pendingOutput() const { return m_stOut.pending(); }

    /**
     * @brief  是否已在 epoll 中订阅 EPOLLOUT（避免每条消息都做一次 epoll_ctl）。
     */
    bool outInterest() const { return m_nOutInterest; }

    /**
     * @brief  记录 EPOLLOUT 订阅状态。
     */
    void setOutInterest(bool v) { m_nOutInterest = v; }

    /**
     * @brief  最近一次协议错误的说明。
     */
    const std::string& protocolError() const { return m_stDecoder.lastError(); }

    /**
     * @brief  累计接收字节数（统计用）。
     */
    uint64_t rxBytes() const { return m_uRxBytes; }

    /**
     * @brief  累计发送字节数（统计用）。
     */
    uint64_t txBytes() const { return m_uTxBytes; }

private:
    /**
     * @brief  socket 描述符，析构时关闭
     * @note   类型 int；类/结构体成员变量。
     */
    int             m_nFd;
    /**
     * @brief  会话 id
     * @note   类型 uint64_t；类/结构体成员变量。
     */
    uint64_t        id_;
    /**
     * @brief  对端地址（日志用）
     * @note   类型 std::string；类/结构体成员变量。
     */
    std::string     m_strPeer;
    /**
     * @brief  当前阶段
     * @note   类型 SessionState_EN；类/结构体成员变量。
     */
    SessionState_EN    m_stState = SessionState_EN::Connected;
    /**
     * @brief  持久用户 id（0 = 匿名）
     * @note   类型 int64_t；类/结构体成员变量。
     */
    int64_t         m_uAccountId = 0;
    /**
     * @brief  本连接注册尝试次数
     * @note   类型 int；类/结构体成员变量。
     */
    int             m_nRegisterAttempts = 0;
    /**
     * @brief  昵称
     * @note   类型 std::string；类/结构体成员变量。
     */
    std::string     m_strName;
    /**
     * @brief  年龄，0 表示未填写
     * @note   类型 int；类/结构体成员变量。
     */
    int             m_nAge = 0;
    /**
     * @brief  个人简介
     * @note   类型 std::string；类/结构体成员变量。
     */
    std::string     m_strDesc;
    /**
     * @brief  所在房间号
     * @note   类型 std::string；类/结构体成员变量。
     */
    std::string     m_strRoom;
    /**
     * @brief  最后活跃时间
     * @note   类型 std::chrono::steady_clock::time_point；类/结构体成员变量。
     */
    std::chrono::steady_clock::time_point m_stLastActive;
    /**
     * @brief  发送缓冲
     * @note   类型 COutBuffer；类/结构体成员变量。
     */
    COutBuffer       m_stOut;
    /**
     * @brief  接收侧帧解码器
     * @note   类型 CFrameDecoder；类/结构体成员变量。
     */
    CFrameDecoder    m_stDecoder;
    /**
     * @brief  是否已订阅 EPOLLOUT
     * @note   类型 bool；类/结构体成员变量。
     */
    bool            m_nOutInterest = false;
    /**
     * @brief  累计接收字节
     * @note   类型 uint64_t；类/结构体成员变量。
     */
    uint64_t        m_uRxBytes = 0;
    /**
     * @brief  累计发送字节
     * @note   类型 uint64_t；类/结构体成员变量。
     */
    uint64_t        m_uTxBytes = 0;
};

/**
 * @brief  namespace dp。
 */
}
