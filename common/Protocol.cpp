/*
 * 文件名:    Protocol.cpp
 * 创建日期:  2026-11-13
 * 作者:      JaTin
 * 功能描述:  Protocol 实现文件，包含相关接口的具体实现。
 * 修改记录:  2026-11-13 JaTin 按 C/C++ 编码规范整改注释与命名。
 */

/* common/Protocol.cpp */
#include "common/Protocol.h"

#include <cstring>
#include <stdexcept>

namespace dp {
namespace {

/*
 * 功能描述:  读取32be。
 * 作者:      JaTin
 * param [in]   uP               输入数据，类型 const unsigned char*。
 * return:    返回 inline uint32_t 类型的处理结果，具体含义见功能描述。
 */
inline uint32_t readU32BE(const unsigned char* uP) {
    return (static_cast<uint32_t>(uP[0]) << 24) |
           (static_cast<uint32_t>(uP[1]) << 16) |
           (static_cast<uint32_t>(uP[2]) << 8)  |
            static_cast<uint32_t>(uP[3]);
}

/* namespace */
}

/*
 * 功能描述:  获取操作码 code 名称。
 * 作者:      JaTin
 * param [in]   enOp             协议操作码，类型 OpCode_EN。
 * return:    成功返回对象指针；未找到或失败时返回 nullptr。
 */
const char* opCodeName(OpCode_EN enOp) {
    switch (enOp) {
        case OpCode_EN::Login:     return "Login";
        case OpCode_EN::JoinRoom:  return "JoinRoom";
        case OpCode_EN::LeaveRoom: return "LeaveRoom";
        case OpCode_EN::Chat:      return "Chat";
        case OpCode_EN::ListRooms: return "ListRooms";
        case OpCode_EN::Ping:      return "Ping";
        case OpCode_EN::UpdateProfile: return "UpdateProfile";
        case OpCode_EN::Register:  return "Register";
        case OpCode_EN::Voice:     return "Voice";
        case OpCode_EN::LoginResp: return "LoginResp";
        case OpCode_EN::RoomState: return "RoomState";
        case OpCode_EN::ChatPush:  return "ChatPush";
        case OpCode_EN::SystemMsg: return "SystemMsg";
        case OpCode_EN::ErrorMsg:  return "ErrorMsg";
        case OpCode_EN::Pong:      return "Pong";
        case OpCode_EN::RoomList:  return "RoomList";
        case OpCode_EN::ProfileResp: return "ProfileResp";
        case OpCode_EN::HistoryResp: return "HistoryResp";
        case OpCode_EN::VoicePush: return "VoicePush";
        case OpCode_EN::RegisterResp: return "RegisterResp";
    }
    return "Unknown";
}

/*
 * 功能描述:  编码协议帧；实现上校验失败时抛出异常；校验不通过时抛出异常。
 * 作者:      JaTin
 * param [in]   enOp             协议操作码，类型 OpCode_EN。
 * param [in]   strPayload       协议载荷内容，类型 const std::string&。
 * return:    返回构造好的字符串；无内容时返回空字符串。
 */
std::string encodeFrame(OpCode_EN enOp, const std::string& strPayload) {
    if (strPayload.size() > kMaxPayload) {
        throw std::length_error("frame payload exceeds kMaxPayload");
    }
    const uint32_t uLen = static_cast<uint32_t>(strPayload.size() + 1);

    std::string stOut;
    stOut.resize(kHeaderSize + strPayload.size());
    stOut[0] = static_cast<char>((uLen >> 24) & 0xFF);
    stOut[1] = static_cast<char>((uLen >> 16) & 0xFF);
    stOut[2] = static_cast<char>((uLen >> 8) & 0xFF);
    stOut[3] = static_cast<char>(uLen & 0xFF);
    stOut[4] = static_cast<char>(enOp);
    if (!strPayload.empty()) {
        std::memcpy(&stOut[kHeaderSize], strPayload.data(), strPayload.size());
    }
    return stOut;
}

/*
 * 功能描述:  编码协议帧。
 * 作者:      JaTin
 * param [in]   enOp             协议操作码，类型 OpCode_EN。
 * param [in]   strPayload       协议载荷内容，类型 const Json&。
 * return:    返回构造好的字符串；无内容时返回空字符串。
 */
std::string encodeFrame(OpCode_EN enOp, const Json& strPayload) {
    return encodeFrame(enOp, strPayload.dump(-1, ' ', false, Json::error_handler_t::replace));
}

/*
 * 功能描述:  追加append。
 * 作者:      JaTin
 * param [in]   strData          原始字节数据，类型 const char*。
 * param [in]   uLen             输入数据，类型 size_t。
 * return:    无返回值；处理结果通过成员状态或信号对外体现。
 */
void CFrameDecoder::append(const char* strData, size_t uLen) {
    if (m_uReadPos > 0 && m_uReadPos == m_strBuf.size()) {
        m_strBuf.clear();
        m_uReadPos = 0;
    } else if (m_uReadPos > 64 * 1024) {
        m_strBuf.erase(0, m_uReadPos);
        m_uReadPos = 0;
    }
    m_strBuf.append(strData, uLen);
}

/*
 * 功能描述:  从接收缓冲区中尝试解出一个完整协议帧。
 * 作者:      JaTin
 * param [out]  stOut            输入数据，类型 Frame_S&。
 * return:    返回 CFrameDecoder::Result_EN 类型的处理结果，具体含义见功能描述。
 */
CFrameDecoder::Result_EN CFrameDecoder::next(Frame_S& stOut) {
    if (m_strBuf.size() - m_uReadPos < kHeaderSize) {
        return NeedMore;
    }

    const auto* uP = reinterpret_cast<const unsigned char*>(m_strBuf.data() + m_uReadPos);
    const uint32_t uLen = readU32BE(uP);

    if (uLen == 0 || uLen > kMaxFrameSize) {
        m_strErr = "invalid frame length: " + std::to_string(uLen);
        return Fatal;
    }
    if (m_strBuf.size() - m_uReadPos < static_cast<size_t>(4) + uLen) {
        return NeedMore;
    }

    const auto stOpByte = static_cast<unsigned char>(m_strBuf[m_uReadPos + 4]);
    stOut.op = static_cast<OpCode_EN>(stOpByte);
    stOut.payload.assign(m_strBuf, m_uReadPos + kHeaderSize, uLen - 1);

    m_uReadPos += 4 + uLen;
    return Ok;
}

/* namespace dp */
}
