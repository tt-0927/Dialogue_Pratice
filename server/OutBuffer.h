/**
 * @file       OutBuffer.h
 * @brief      COutBuffer 头文件，声明相关类型与接口。
 * @author     JaTin
 * @date       2026-11-13
 * @note       修改记录：2026-11-14 JaTin 统一为 Doxygen 风格注释。
 */

/**
 * @brief  server/COutBuffer.h。
 */

/**
 * @brief  发送缓冲。非阻塞 socket 上 send() 可能只写出一部分， 剩余数据必须缓存并在 EPOLLOUT 可写时继续写，否则会丢消息。
 */
#pragma once

#include <cerrno>
#include <cstddef>
#include <string>

#include <sys/socket.h>

namespace dp {

enum class FlushResult_EN { Done, Blocked, Error };

/**
 * @brief   COut缓冲区。
 * @author  JaTin
 */
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

    /**
     * @brief  累计写出的字节数（用于统计）。
     */
    size_t totalSent() const { return m_uTotalSent; }

    /**
     * @brief  尽可能写出。Done=全部写完；Blocked=内核发送缓冲满，等 EPOLLOUT；Error=连接已不可用。
     */
    FlushResult_EN flush(int fd) {
        while (m_uSent < m_strBuf.size()) {
            /**
             * @brief  n。 初值 = ::send(fd, m_strBuf.data() + m_uSent, m_strBuf.size() - m_uSent, MSG_NOSIGNAL)。
             * @note   类型 ssize_t；类/结构体成员变量。
             */
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
            /**
             * @brief  n == 0 视为异常。
             */
            return FlushResult_EN::Error;
        }
        m_strBuf.clear();
        m_uSent = 0;
        return FlushResult_EN::Done;
    }

private:
    /**
     * @brief  缓冲区。
     * @note   类型 std::string；类/结构体成员变量。
     */
    std::string m_strBuf;
    /**
     * @brief  Sent。 初值 = 0。
     * @note   类型 size_t；类/结构体成员变量。
     */
    size_t      m_uSent = 0;
    /**
     * @brief  TotalSent。 初值 = 0。
     * @note   类型 size_t；类/结构体成员变量。
     */
    size_t      m_uTotalSent = 0;
};

/**
 * @brief  namespace dp。
 */
}
