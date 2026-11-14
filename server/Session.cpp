/**
 * @file       Session.cpp
 * @brief      CSession 实现文件，包含相关接口的具体实现。
 * @author     JaTin
 * @date       2026-11-13
 * @note       修改记录：2026-11-14 JaTin 统一为 Doxygen 风格注释。
 */

/**
 * @brief  server/CSession.cpp。
 */
#include "server/Session.h"

#include <cerrno>
#include <utility>

#include <sys/socket.h>
#include <unistd.h>

namespace dp {

/**
 * @brief      构造 CSession 对象，完成成员初始化与依赖注入。
 * @param[in] hFd            文件描述符；类型 int。
 * @param[in] uId            编号；类型 uint64_t。
 * @param[in] strPeer        输入数据；类型 std::string。
 * @return     构造/析构函数无返回值。
 * @author     JaTin
 */
CSession::CSession(int hFd, uint64_t uId, std::string strPeer)
    : m_nFd(hFd), id_(uId), m_strPeer(std::move(strPeer)), m_stLastActive(std::chrono::steady_clock::now()) {}

/**
 * @brief      析构 CSession 对象，释放运行期持有的全部资源。
 * @note       本函数无输入参数。
 * @return     无返回值（构造函数或析构函数）。
 * @author     JaTin
 */
CSession::~CSession() {
    if (m_nFd >= 0) {
        ::close(m_nFd);
        m_nFd = -1;
    }
}

/**
 * @brief      读取 available相关的数据并返回处理结果。
 * @param[out] aFrames        协议帧集合；类型 std::vector<Frame_S>&。
 * @return     返回 ReadResult_EN CSession:: 类型的处理结果。
 * @author     JaTin
 */
ReadResult_EN CSession::readAvailable(std::vector<Frame_S>& aFrames) {
    char chBuf[16 * 1024];

    for (;;) {
        const ssize_t uN = ::recv(m_nFd, chBuf, sizeof(chBuf), 0);

        if (uN > 0) {
            m_uRxBytes += static_cast<uint64_t>(uN);
            m_stDecoder.append(chBuf, static_cast<size_t>(uN));

            for (;;) {
                Frame_S strFrame;
                const CFrameDecoder::Result_EN stR = m_stDecoder.next(strFrame);
                if (stR == CFrameDecoder::NeedMore) break;
                if (stR == CFrameDecoder::Fatal) return ReadResult_EN::ProtocolError;
                aFrames.push_back(std::move(strFrame));
            }
            /**
             * @brief  边沿触发：必须读到 EAGAIN 为止。
             */
            continue;
        }

        if (uN == 0) return ReadResult_EN::PeerClosed;
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return ReadResult_EN::Ok;
        return ReadResult_EN::Error;
    }
}

/**
 * @brief      入队 enqueue。
 * @param[in] strFrame       协议帧；类型 const std::string&。
 * @return     无返回值；处理结果通过成员状态或信号对外体现。
 * @author     JaTin
 */
void CSession::enqueue(const std::string& strFrame) {
    m_stOut.append(strFrame);
}

/**
 * @brief      冲刷 flush。
 * @note       本函数无输入参数。
 * @return     返回 FlushResult_EN CSession:: 类型的处理结果。
 * @author     JaTin
 */
FlushResult_EN CSession::flush() {
    const size_t uBefore = m_stOut.totalSent();
    const FlushResult_EN enR = m_stOut.flush(m_nFd);
    m_uTxBytes += m_stOut.totalSent() - uBefore;
    return enR;
}

/**
 * @brief  namespace dp。
 */
}
