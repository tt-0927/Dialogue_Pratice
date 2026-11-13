/**
 * @file  OutBuffer.h
 * @brief 非阻塞 socket 的发送缓冲。
 * @details 非阻塞 socket 上 `send()` 可能只写出一部分，剩余数据必须缓存并在
 *          EPOLLOUT 可写时继续写，否则会丢消息。
 *
 *          缓冲采用「读偏移」而非每次 erase，避免大量小包时反复搬移内存；
 *          已发偏移超过 8 KB 才做一次压缩。
 */

#pragma once

#include <cerrno>
#include <cstddef>
#include <string>

#include <sys/socket.h>

namespace dp {

/**
 * @brief 一次写出尝试的结果。
 */
enum class FlushResult {
    Done,     ///< 缓冲已全部写出
    Blocked,  ///< 内核发送缓冲满，需等 EPOLLOUT 再写
    Error,    ///< 连接已不可用，应关闭会话
};

/**
 * @brief 发送缓冲。
 *
 * @details 只在事件循环线程使用，无需加锁。
 */
class OutBuffer {
public:
    /**
     * @brief 追加待发数据。
     * @param data 已编码的完整帧。
     * @details 追加前按需压缩缓冲：全部写完则清空，否则已发偏移超过 8 KB 才搬移。
     */
    void append(const std::string& data) {
        if (sent_ > 0 && sent_ >= buf_.size()) {
            buf_.clear();
            sent_ = 0;
        } else if (sent_ > 8 * 1024) {
            buf_.erase(0, sent_);
            sent_ = 0;
        }
        buf_.append(data);
    }

    /// @brief 尚未写出的字节数。
    size_t pending() const { return buf_.size() - sent_; }

    /// @brief 缓冲是否已空。
    bool empty() const { return pending() == 0; }

    /// @brief 累计写出的字节数（统计用）。
    size_t totalSent() const { return totalSent_; }

    /**
     * @brief 尽可能把缓冲写空。
     * @param fd 目标 socket。
     * @retval Done    全部写完。
     * @retval Blocked 内核发送缓冲满，等 EPOLLOUT。
     * @retval Error   连接已不可用。
     * @details 用 `MSG_NOSIGNAL` 避免对端已关闭时触发 SIGPIPE 杀掉进程。
     */
    FlushResult flush(int fd) {
        while (sent_ < buf_.size()) {
            const ssize_t n = ::send(fd, buf_.data() + sent_, buf_.size() - sent_, MSG_NOSIGNAL);
            if (n > 0) {
                sent_ += static_cast<size_t>(n);
                totalSent_ += static_cast<size_t>(n);
                continue;
            }
            if (n < 0) {
                if (errno == EINTR) continue;
                if (errno == EAGAIN || errno == EWOULDBLOCK) return FlushResult::Blocked;
                return FlushResult::Error;
            }
            return FlushResult::Error;   // n == 0 视为异常
        }
        buf_.clear();
        sent_ = 0;
        return FlushResult::Done;
    }

private:
    std::string buf_;            ///< 待发数据（含已发部分，按 sent_ 偏移）
    size_t      sent_ = 0;       ///< 已写出的字节数
    size_t      totalSent_ = 0;  ///< 累计写出字节数（统计用）
};

}  // namespace dp
