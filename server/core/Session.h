/**
 * @file  Session.h
 * @brief 一条 TCP 连接及其上的身份状态。
 * @details 职责边界：只管「这条连接的 I/O 与身份」，不处理业务规则。
 *          房间归属、广播策略在 business 层，本类只提供 `room()` 这样的状态字段。
 *
 *          生命周期由 SessionManager 以 `std::unique_ptr` 独占持有；
 *          析构时关闭 fd，因此调用方必须先 `epoll_ctl(DEL)` 再销毁。
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
enum class SessionState {
    Connected,   ///< 已建连，尚未登录
    LoggedIn,    ///< 已登录，可能尚未进入房间
    Closing,     ///< 待回收（批次结束后由 Reactor 统一关闭）
};

/**
 * @brief 一次 readAvailable() 的结果。
 */
enum class ReadResult {
    Ok,             ///< 已读到 EAGAIN，连接正常
    PeerClosed,     ///< 对端有序关闭（recv 返回 0）
    Error,          ///< 读出错，errno 有效
    ProtocolError,  ///< 帧格式非法，原因见 protocolError()
};

/**
 * @brief 一条连接。
 *
 * @details 同时承担三件事：非阻塞读写、帧解码、身份与房间归属。
 *          所有方法都只在事件循环线程调用，不加锁。
 */
class Session {
public:
    /**
     * @brief 接管一个已建立的 socket。
     * @param fd   非阻塞 socket，所有权转移给本对象（析构时 close）。
     * @param id   会话 id，由 SessionManager 分配，全局单调递增。
     * @param peer 对端地址描述，仅用于日志。
     */
    Session(int fd, uint64_t id, std::string peer);

    ~Session();

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    /// @brief 底层 socket 描述符。
    int fd() const { return fd_; }

    /// @brief 会话 id，广播与去重都依赖它。
    uint64_t id() const { return id_; }

    /// @brief 对端地址，形如 `192.168.1.5:52344`。
    const std::string& peer() const { return peer_; }

    /// @brief 当前阶段。
    SessionState state() const { return state_; }

    /// @brief 设置阶段。
    void setState(SessionState s) { state_ = s; }

    /// @brief 是否已完成登录。
    bool loggedIn() const { return state_ == SessionState::LoggedIn; }

    /// @brief 本连接已尝试注册的次数（用于限流，防止单连接刷号）。
    int registerAttempts() const { return registerAttempts_; }

    /// @brief 记录一次注册尝试。
    void bumpRegisterAttempts() { ++registerAttempts_; }

    /// @brief 持久用户 id（用户库里的 id），0 表示未匹配到账号。
    /// @details 与 id() 不同：id() 是每次连接的临时编号，重连就变；
    ///          本字段在用户库里有对应记录时才非 0。
    int64_t accountId() const { return accountId_; }

    /// @brief 设置持久用户 id。
    void setAccountId(int64_t id) { accountId_ = id; }

    /// @brief 昵称。
    const std::string& name() const { return name_; }

    /// @brief 设置昵称（长度与字符集由 business 层校验）。
    void setName(std::string n) { name_ = std::move(n); }

    /// @brief 年龄，0 表示未填写。
    int age() const { return age_; }

    /// @brief 设置年龄。
    void setAge(int a) { age_ = a; }

    /// @brief 个人简介。
    const std::string& desc() const { return desc_; }

    /// @brief 设置个人简介。
    void setDesc(std::string d) { desc_ = std::move(d); }

    /// @brief 当前所在房间号，空串表示不在任何房间。
    const std::string& room() const { return room_; }

    /// @brief 设置房间归属（由 RoomService 维护）。
    void setRoom(std::string r) { room_ = std::move(r); }

    /// @brief 清空房间归属（离房时调用）。
    void clearRoom() { room_.clear(); }

    /// @brief 刷新最后活跃时间，心跳判定依据。
    void touch() { lastActive_ = std::chrono::steady_clock::now(); }

    /// @brief 最后活跃时间点。
    std::chrono::steady_clock::time_point lastActive() const { return lastActive_; }

    /**
     * @brief 一次读干 socket 并解出所有完整帧。
     * @param frames 输出参数，追加解析出的完整帧（不清空原有内容）。
     * @retval Ok            读到 EAGAIN，连接正常。
     * @retval PeerClosed    对端有序关闭。
     * @retval Error         读出错，errno 有效。
     * @retval ProtocolError 帧格式非法，原因见 protocolError()。
     * @details 边沿触发模式下必须循环读到 EAGAIN，否则会漏事件。
     */
    ReadResult readAvailable(std::vector<Frame>& frames);

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
    FlushResult flush();

    /// @brief 是否还有待发数据。
    bool hasPendingOutput() const { return out_.pending() > 0; }

    /// @brief 待发字节数。
    size_t pendingOutput() const { return out_.pending(); }

    /// @brief 是否已在 epoll 中订阅 EPOLLOUT（避免每条消息都做一次 epoll_ctl）。
    bool outInterest() const { return outInterest_; }

    /// @brief 记录 EPOLLOUT 订阅状态。
    void setOutInterest(bool v) { outInterest_ = v; }

    /// @brief 最近一次协议错误的说明。
    const std::string& protocolError() const { return decoder_.lastError(); }

    /// @brief 累计接收字节数（统计用）。
    uint64_t rxBytes() const { return rxBytes_; }

    /// @brief 累计发送字节数（统计用）。
    uint64_t txBytes() const { return txBytes_; }

private:
    int             fd_;                       ///< socket 描述符，析构时关闭
    uint64_t        id_;                       ///< 会话 id
    std::string     peer_;                     ///< 对端地址（日志用）
    SessionState    state_ = SessionState::Connected; ///< 当前阶段
    int64_t         accountId_ = 0;            ///< 持久用户 id（0 = 匿名）
    int             registerAttempts_ = 0;     ///< 本连接注册尝试次数
    std::string     name_;                     ///< 昵称
    int             age_ = 0;                  ///< 年龄，0 表示未填写
    std::string     desc_;                     ///< 个人简介
    std::string     room_;                     ///< 所在房间号
    std::chrono::steady_clock::time_point lastActive_; ///< 最后活跃时间
    OutBuffer       out_;                      ///< 发送缓冲
    FrameDecoder    decoder_;                  ///< 接收侧帧解码器
    bool            outInterest_ = false;      ///< 是否已订阅 EPOLLOUT
    uint64_t        rxBytes_ = 0;              ///< 累计接收字节
    uint64_t        txBytes_ = 0;              ///< 累计发送字节
};

}  // namespace dp
