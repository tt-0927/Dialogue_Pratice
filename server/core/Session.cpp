/*
 * 文件名:    Session.cpp
 * 创建日期:  2026-11-13
 * 作者:      JaTin
 * 功能描述:  CSession 实现文件，包含相关接口的具体实现。
 * 修改记录:  2026-11-13 JaTin 按 C/C++ 编码规范整改注释与命名。
 */

/* server/CSession.cpp */
#include "server/core/Session.h"

#include <cerrno>
#include <utility>

#include <sys/socket.h>
#include <unistd.h>

namespace dp {

CSession::CSession(int fd, uint64_t id, std::string peer)
    : m_nFd(fd), id_(id), m_strPeer(std::move(peer)), m_stLastActive(std::chrono::steady_clock::now()) {}

/*
 * 功能描述:  执行 CSession 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * return:    返回值，含义见功能描述。
 */
CSession::~CSession() {
    if (m_nFd >= 0) {
        ::close(m_nFd);
        m_nFd = -1;
    }
}

/*
 * 功能描述:  执行 readAvailable 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [out]  frames  参数。
 * return:    返回值，含义见功能描述。
 */
ReadResult_EN CSession::readAvailable(std::vector<Frame_S>& frames) {
    char buf[16 * 1024];

    for (;;) {
        const ssize_t n = ::recv(m_nFd, buf, sizeof(buf), 0);

        if (n > 0) {
            m_uRxBytes += static_cast<uint64_t>(n);
            m_stDecoder.append(buf, static_cast<size_t>(n));

            for (;;) {
                Frame_S frame;
                const CFrameDecoder::Result_EN r = m_stDecoder.next(frame);
                if (r == CFrameDecoder::NeedMore) break;
                if (r == CFrameDecoder::Fatal) return ReadResult_EN::ProtocolError;
                frames.push_back(std::move(frame));
            }
            /* 边沿触发：必须读到 EAGAIN 为止 */
            continue;
        }

        if (n == 0) return ReadResult_EN::PeerClosed;
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return ReadResult_EN::Ok;
        return ReadResult_EN::Error;
    }
}

/*
 * 功能描述:  执行 enqueue 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [in]  frame  参数。
 * return:    void
 */
void CSession::enqueue(const std::string& frame) {
    m_stOut.append(frame);
}

/*
 * 功能描述:  执行 flush 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * return:    返回值，含义见功能描述。
 */
FlushResult_EN CSession::flush() {
    const size_t before = m_stOut.totalSent();
    const FlushResult_EN r = m_stOut.flush(m_nFd);
    m_uTxBytes += m_stOut.totalSent() - before;
    return r;
}

/* namespace dp */
}
