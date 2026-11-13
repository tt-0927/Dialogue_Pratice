// server/OutBuffer.h
//
// 发送缓冲。非阻塞 socket 上 send() 可能只写出一部分，
// 剩余数据必须缓存并在 EPOLLOUT 可写时继续写，否则会丢消息。
#pragma once

#include <cerrno>
#include <cstddef>
#include <string>

#include <sys/socket.h>

namespace dp {

enum class FlushResult { Done, Blocked, Error };

class OutBuffer {
public:
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

    size_t pending() const { return buf_.size() - sent_; }
    bool   empty() const { return pending() == 0; }

    /// 累计写出的字节数（用于统计）。
    size_t totalSent() const { return totalSent_; }

    /// 尽可能写出。Done=全部写完；Blocked=内核发送缓冲满，等 EPOLLOUT；Error=连接已不可用。
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
    std::string buf_;
    size_t      sent_ = 0;
    size_t      totalSent_ = 0;
};

}  // namespace dp
