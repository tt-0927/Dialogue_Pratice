// common/Protocol.cpp
#include "common/Protocol.h"

#include <cstring>
#include <stdexcept>

namespace dp {
namespace {

inline uint32_t readU32BE(const unsigned char* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  |
            static_cast<uint32_t>(p[3]);
}

}  // namespace

const char* opCodeName(OpCode op) {
    switch (op) {
        case OpCode::Login:     return "Login";
        case OpCode::JoinRoom:  return "JoinRoom";
        case OpCode::LeaveRoom: return "LeaveRoom";
        case OpCode::Chat:      return "Chat";
        case OpCode::ListRooms: return "ListRooms";
        case OpCode::Ping:      return "Ping";
        case OpCode::UpdateProfile: return "UpdateProfile";
        case OpCode::Register:  return "Register";
        case OpCode::Voice:     return "Voice";
        case OpCode::LoginResp: return "LoginResp";
        case OpCode::RoomState: return "RoomState";
        case OpCode::ChatPush:  return "ChatPush";
        case OpCode::SystemMsg: return "SystemMsg";
        case OpCode::ErrorMsg:  return "ErrorMsg";
        case OpCode::Pong:      return "Pong";
        case OpCode::RoomList:  return "RoomList";
        case OpCode::ProfileResp: return "ProfileResp";
        case OpCode::HistoryResp: return "HistoryResp";
        case OpCode::VoicePush: return "VoicePush";
        case OpCode::RegisterResp: return "RegisterResp";
    }
    return "Unknown";
}

std::string encodeFrame(OpCode op, const std::string& payload) {
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

std::string encodeFrame(OpCode op, const Json& payload) {
    return encodeFrame(op, payload.dump(-1, ' ', false, Json::error_handler_t::replace));
}

void FrameDecoder::append(const char* data, size_t len) {
    if (readPos_ > 0 && readPos_ == buf_.size()) {
        buf_.clear();
        readPos_ = 0;
    } else if (readPos_ > 64 * 1024) {
        buf_.erase(0, readPos_);
        readPos_ = 0;
    }
    buf_.append(data, len);
}

FrameDecoder::Result FrameDecoder::next(Frame& out) {
    if (buf_.size() - readPos_ < kHeaderSize) {
        return NeedMore;
    }

    const auto* p = reinterpret_cast<const unsigned char*>(buf_.data() + readPos_);
    const uint32_t len = readU32BE(p);

    if (len == 0 || len > kMaxFrameSize) {
        err_ = "invalid frame length: " + std::to_string(len);
        return Fatal;
    }
    if (buf_.size() - readPos_ < static_cast<size_t>(4) + len) {
        return NeedMore;
    }

    const auto opByte = static_cast<unsigned char>(buf_[readPos_ + 4]);
    out.op = static_cast<OpCode>(opByte);
    out.payload.assign(buf_, readPos_ + kHeaderSize, len - 1);

    readPos_ += 4 + len;
    return Ok;
}

}  // namespace dp
