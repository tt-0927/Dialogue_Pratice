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
 * 功能描述:  执行 readU32BE 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [in]  p  参数。
 * return:    返回值，含义见功能描述。
 */
inline uint32_t readU32BE(const unsigned char* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  |
            static_cast<uint32_t>(p[3]);
}

/* namespace */
}

/*
 * 功能描述:  执行 opCodeName 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [in]  op  参数。
 * return:    返回值，含义见功能描述。
 */
const char* opCodeName(OpCode_EN op) {
    switch (op) {
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
 * 功能描述:  执行 encodeFrame 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [in]  op  参数。
 * param [in]  payload  参数。
 * return:    返回值，含义见功能描述。
 */
std::string encodeFrame(OpCode_EN op, const std::string& payload) {
    if (payload.size() > kMaxPayload) {
        throw std::length_error("frame payload exceeds kMaxPayload");
    }
    const uint32_t len = static_cast<uint32_t>(payload.size() + 1);

    std::string out;
    out.resize(kHeaderSize + payload.size());
    out[0] = static_cast<char>((len >> 24) & 0xFF);
    out[1] = static_cast<char>((len >> 16) & 0xFF);
    out[2] = static_cast<char>((len >> 8) & 0xFF);
    out[3] = static_cast<char>(len & 0xFF);
    out[4] = static_cast<char>(op);
    if (!payload.empty()) {
        std::memcpy(&out[kHeaderSize], payload.data(), payload.size());
    }
    return out;
}

/*
 * 功能描述:  执行 encodeFrame 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [in]  op  参数。
 * param [in]  payload  参数。
 * return:    返回值，含义见功能描述。
 */
std::string encodeFrame(OpCode_EN op, const Json& payload) {
    return encodeFrame(op, payload.dump(-1, ' ', false, Json::error_handler_t::replace));
}

/*
 * 功能描述:  执行 append 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [in]  data  参数。
 * param [in]  len  参数。
 * return:    void
 */
void CFrameDecoder::append(const char* data, size_t len) {
    if (m_uReadPos > 0 && m_uReadPos == m_strBuf.size()) {
        m_strBuf.clear();
        m_uReadPos = 0;
    } else if (m_uReadPos > 64 * 1024) {
        m_strBuf.erase(0, m_uReadPos);
        m_uReadPos = 0;
    }
    m_strBuf.append(data, len);
}

/*
 * 功能描述:  执行 next 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [out]  out  参数。
 * return:    返回值，含义见功能描述。
 */
CFrameDecoder::Result_EN CFrameDecoder::next(Frame_S& out) {
    if (m_strBuf.size() - m_uReadPos < kHeaderSize) {
        return NeedMore;
    }

    const auto* p = reinterpret_cast<const unsigned char*>(m_strBuf.data() + m_uReadPos);
    const uint32_t len = readU32BE(p);

    if (len == 0 || len > kMaxFrameSize) {
        m_strErr = "invalid frame length: " + std::to_string(len);
        return Fatal;
    }
    if (m_strBuf.size() - m_uReadPos < static_cast<size_t>(4) + len) {
        return NeedMore;
    }

    const auto opByte = static_cast<unsigned char>(m_strBuf[m_uReadPos + 4]);
    out.op = static_cast<OpCode_EN>(opByte);
    out.payload.assign(m_strBuf, m_uReadPos + kHeaderSize, len - 1);

    m_uReadPos += 4 + len;
    return Ok;
}

/* namespace dp */
}
