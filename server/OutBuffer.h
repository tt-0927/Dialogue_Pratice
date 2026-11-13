/*
 * 文件名:    OutBuffer.h
 * 创建日期:  2026-11-13
 * 作者:      JaTin
 * 功能描述:  COutBuffer 头文件，声明相关类型与接口。
 * 修改记录:  2026-11-13 JaTin 按 C/C++ 编码规范整改注释与命名。
 */

/* server/COutBuffer.h */

/* 发送缓冲。非阻塞 socket 上 send() 可能只写出一部分， */
/* 剩余数据必须缓存并在 EPOLLOUT 可写时继续写，否则会丢消息。 */
#pragma once

#include <cerrno>
#include <cstddef>
#include <string>

#include <sys/socket.h>

namespace dp {

enum class FlushResult_EN { Done, Blocked, Error };

class COutBuffer {
public:
    void append(const std::string& data) {
        if (m_uSent > 0 && m_uSent >= m_strBuf.size()) {
            m_strBuf.clear();
            m_uSent = 0;
        } else if (m_uSent > 8 * 1024) {
            m_strBuf.erase(0, m_uSent);
            m_uSent = 0;
        }
        m_strBuf.append(data);
    }

    size_t pending() const { return m_strBuf.size() - m_uSent; }
    bool   empty() const { return pending() == 0; }

    /* 累计写出的字节数（用于统计）。 */
    size_t totalSent() const { return m_uTotalSent; }

    /* 尽可能写出。Done=全部写完；Blocked=内核发送缓冲满，等 EPOLLOUT；Error=连接已不可用。 */
    FlushResult_EN flush(int fd) {
        while (m_uSent < m_strBuf.size()) {
            const ssize_t n = ::send(fd, m_strBuf.data() + m_uSent, m_strBuf.size() - m_uSent, MSG_NOSIGNAL);
            if (n > 0) {
                m_uSent += static_cast<size_t>(n);
                m_uTotalSent += static_cast<size_t>(n);
                continue;
            }
            if (n < 0) {
                if (errno == EINTR) continue;
                if (errno == EAGAIN || errno == EWOULDBLOCK) return FlushResult_EN::Blocked;
                return FlushResult_EN::Error;
            }
            /* n == 0 视为异常 */
            return FlushResult_EN::Error;
        }
        m_strBuf.clear();
        m_uSent = 0;
        return FlushResult_EN::Done;
    }

private:
    std::string m_strBuf;
    size_t      m_uSent = 0;
    size_t      m_uTotalSent = 0;
};

/* namespace dp */
}
