// common/Protocol.h
//
// 通信协议：客户端 <-> 服务端 共用的帧编解码。
// 该头文件只依赖 C++ 标准库与 nlohmann/json，不含任何 POSIX 调用，
// 因此同一份代码可被服务端（epoll）与 Qt6 客户端（QTcpSocket）共同复用。
//
// 线上格式（大端）：
//   +----------------+-----------+---------------------------+
//   |  length: u32   | op: u8    |  payload: JSON (length-1) |
//   +----------------+-----------+---------------------------+
//   length = 1 + payload.size()，最大 64 KiB + 1。
//
// 采用「长度前缀」而非分隔符，可彻底避免 TCP 粘包/半包与分隔符注入问题。
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "nlohmann/json.hpp"

namespace dp {

using Json = nlohmann::json;

constexpr size_t kHeaderSize = 5;                     // 4B length + 1B opcode
constexpr size_t kMaxPayload = 64 * 1024;             // 单帧 JSON 上限
constexpr size_t kMaxFrameSize = kMaxPayload + 1;     // length 字段允许的最大值

/// 操作码。0x0x 为客户端上行，0x8x 为服务端下行。
enum class OpCode : uint8_t {
    // ---- 客户端 -> 服务端 ----
    Login         = 0x01,   // {"name":"alice","password":"123456","age":23,"desc":"..."}
                            //   password 可选：带了就按账号密码校验，不带则按昵称匿名登录
                            //   （服务端可用 requireAuth 强制要求密码）；age/desc 可选
    JoinRoom      = 0x02,   // {"room":"1001"}
    LeaveRoom     = 0x03,   // {}
    Chat          = 0x04,   // {"text":"hello"}
    ListRooms     = 0x05,   // {}
    Ping          = 0x06,   // {}
    UpdateProfile = 0x07,   // {"name":"alice","age":24,"desc":"..."}
    Register      = 0x08,   // {"username":"bob","password":"secret","nickname":"Bob","age":28,"desc":"..."}
                            //   nickname/age/desc 可选，nickname 缺省时用 username
    Voice         = 0x09,   // {"durationMs":3200,"opus":"<base64 DPV1 容器>"}  语音消息

    // ---- 服务端 -> 客户端 ----
    LoginResp = 0x81,       // {"ok":true,"userId":1,"name":"alice","age":23,"desc":"","serverTime":...}
    RoomState = 0x82,       // {"room":"1001","created":false,"members":[{"id":1,"name":"alice"}]}
    ChatPush  = 0x83,       // {"seq":12,"from":1,"name":"alice","room":"1001","text":"hi","ts":...}
    SystemMsg = 0x84,       // {"event":"join|leave|left|welcome|rename","room":"1001","text":"...","ts":...}
    ErrorMsg  = 0x85,       // {"code":"bad_payload","text":"..."}
    Pong      = 0x86,       // {"ts":...}
    RoomList  = 0x87,       // {"rooms":[{"id":"1001","count":2}]}
    ProfileResp = 0x88,     // {"ok":true,"userId":1,"name":..,"age":..,"desc":..}
    HistoryResp = 0x89,     // {"room":"1001","messages":[{"seq":12,"kind":"text","from":1,"name":"alice","text":"hi","ts":..}]}
                            // 语音条目：{"seq":31,"kind":"voice","durationMs":3200,"opus":"<base64>",...}
                            // 注意：一条语音就占 30~55 KB，所以服务端会把语音条目**单独成帧**发送，
                            // 客户端可能连续收到多个 HistoryResp，按 seq 合并即可。
    VoicePush = 0x8A,       // {"seq":31,"from":7,"name":"bob","room":"1001","durationMs":3200,"opus":"<base64>","ts":...}
    RegisterResp = 0x8B,    // {"ok":true,"userId":5,"username":"bob","nickname":"Bob"}
};

/// 语音消息的约束（服务端校验、客户端录音上限共用）。
/// 码率 12 kbps 单声道：20 秒约 30 KB，Base64 后约 40 KB，
/// 加上 JSON 其他字段仍远低于单帧 64 KiB 上限。
constexpr uint32_t kMaxVoiceDurationMs = 20000;      // 单条语音最长 20 秒
constexpr size_t   kMaxVoiceBytes      = 32 * 1024;  // 解码后的 DPV1 容器上限

const char* opCodeName(OpCode op);

struct Frame {
    OpCode      op = OpCode::ErrorMsg;
    std::string payload;   // JSON 文本，可能为空串
};

/// 组帧。payload 超过 kMaxPayload 时抛 std::length_error。
std::string encodeFrame(OpCode op, const std::string& payload);
std::string encodeFrame(OpCode op, const Json& payload);

/// 增量解帧器：按字节流喂入，逐帧取出，天然处理粘包与半包。
class FrameDecoder {
public:
    enum Result { Fatal = -1, NeedMore = 0, Ok = 1 };

    /// 取出一个完整帧。Ok=已取出；NeedMore=数据不足；Fatal=协议非法（应断开）。
    Result next(Frame& out);

    void   append(const char* data, size_t len);
    size_t buffered() const { return buf_.size() - readPos_; }

    const std::string& lastError() const { return err_; }

private:
    std::string buf_;
    size_t      readPos_ = 0;
    std::string err_;
};

}  // namespace dp
