/*
 * 文件名:    OutBuffer.h
 * 创建日期:  2026-11-13
 * 作者:      JaTin
 * 功能描述:  非阻塞 socket 的发送缓冲。
 * 修改记录:  2026-11-13 JaTin 按 C/C++ 编码规范整改注释与命名。
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
enum class FlushResult_EN {
    /* < 缓冲已全部写出 */
    Done,
    /* < 内核发送缓冲满，需等 EPOLLOUT 再写 */
    Blocked,
    /* < 连接已不可用，应关闭会话 */
    Error,
};

/**
 * @brief 发送缓冲。
 *
 * @details 只在事件循环线程使用，无需加锁。
 */
class COutBuffer {
public:
    /**
     * @brief 追加待发数据。
     * @param data 已编码的完整帧。
     * @details 追加前按需压缩缓冲：全部写完则清空，否则已发偏移超过 8 KB 才搬移。
     */
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

    /* 尚未写出的字节数。 */
    size_t pending() const { return m_strBuf.size() - m_uSent; }

    /* 缓冲是否已空。 */
    bool empty() const { return pending() == 0; }

    /* 累计写出的字节数（统计用）。 */
    size_t totalSent() const { return m_uTotalSent; }

    /**
     * @brief 尽可能把缓冲写空。
     * @param fd 目标 socket。
     * @retval Done    全部写完。
     * @retval Blocked 内核发送缓冲满，等 EPOLLOUT。
     * @retval Error   连接已不可用。
     * @details 用 `MSG_NOSIGNAL` 避免对端已关闭时触发 SIGPIPE 杀掉进程。
     */
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
    /* < 待发数据（含已发部分，按 sent_ 偏移） */
    std::string m_strBuf;
    /* < 已写出的字节数 */
    size_t      m_uSent = 0;
    /* < 累计写出字节数（统计用） */
    size_t      m_uTotalSent = 0;
};

/* namespace dp */
}
