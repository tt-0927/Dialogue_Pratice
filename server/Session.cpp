/*
 * 文件名:    Session.cpp
 * 创建日期:  2026-11-13
 * 作者:      JaTin
 * 功能描述:  CSession 实现文件，包含相关接口的具体实现。
 * 修改记录:  2026-11-13 JaTin 按 C/C++ 编码规范整改注释与命名。
 */

/* server/CSession.cpp */
#include "server/Session.h"

#include <cerrno>
#include <utility>

#include <sys/socket.h>
#include <unistd.h>

namespace dp {

/*
 * 功能描述:  构造 CSession 对象，完成成员初始化与依赖注入。
 * 作者:      JaTin
 * param [in]   hFd              文件描述符，类型 int。
 * param [in]   uId              编号，类型 uint64_t。
 * param [in]   strPeer          输入数据，类型 std::string。
 * return:    无返回值（构造函数或析构函数）。
 */
CSession::CSession(int hFd, uint64_t uId, std::string strPeer)
    : m_nFd(hFd), id_(uId), m_strPeer(std::move(strPeer)), m_stLastActive(std::chrono::steady_clock::now()) {}

/*
 * 功能描述:  析构 CSession 对象，释放运行期持有的全部资源。
 * 作者:      JaTin
 * return:    无返回值（构造函数或析构函数）。
 */
CSession::~CSession() {
    if (m_nFd >= 0) {
        ::close(m_nFd);
        m_nFd = -1;
    }
}

/*
 * 功能描述:  读取available。
 * 作者:      JaTin
 * param [out]  aFrames          协议帧集合，类型 std::vector<Frame_S>&。
 * return:    返回 ReadResult_EN 类型的处理结果，具体含义见功能描述。
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
            /* 边沿触发：必须读到 EAGAIN 为止 */
            continue;
        }

        if (uN == 0) return ReadResult_EN::PeerClosed;
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return ReadResult_EN::Ok;
        return ReadResult_EN::Error;
    }
}

/*
 * 功能描述:  入队enqueue。
 * 作者:      JaTin
 * param [in]   strFrame         协议帧，类型 const std::string&。
 * return:    无返回值；处理结果通过成员状态或信号对外体现。
 */
void CSession::enqueue(const std::string& strFrame) {
    m_stOut.append(strFrame);
}

/*
 * 功能描述:  冲刷flush。
 * 作者:      JaTin
 * return:    返回 FlushResult_EN 类型的处理结果，具体含义见功能描述。
 */
FlushResult_EN CSession::flush() {
    const size_t uBefore = m_stOut.totalSent();
    const FlushResult_EN enR = m_stOut.flush(m_nFd);
    m_uTxBytes += m_stOut.totalSent() - uBefore;
    return enR;
}

/* namespace dp */
}
