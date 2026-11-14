/**
 * @file       Protocol.h
 * @brief      Protocol 头文件，声明相关类型与接口。
 * @author     JaTin
 * @date       2026-11-13
 * @note       修改记录：2026-11-14 JaTin 统一为 Doxygen 风格注释。
 */

/**
 * @brief  common/Protocol.h。
 */

/**
 * @brief  通信协议：客户端 <-> 服务端 共用的帧编解码。 该头文件只依赖 C++ 标准库与 nlohmann/json，不含任何 POSIX 调用， 因此同一份代码可被服务端（epoll）与 Qt6 客户端（QTcpSocket）共同复用。
 */

/**
 * @brief  线上格式（大端）： +----------------+-----------+---------------------------+ | length: u32 | op: u8 | payload: JSON (length-1) | +----------------+-----------+---------------------------+ length = 1 + payload.size()，最大 64 KiB + 1。
 */

/**
 * @brief  采用「长度前缀」而非分隔符，可彻底避免 TCP 粘包/半包与分隔符注入问题。
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "nlohmann/json.hpp"

namespace dp {

using Json = nlohmann::json;

/**
 * @brief  4B length + 1B opcode
 * @note   取值 5。
 */
constexpr size_t kHeaderSize = 5;
/**
 * @brief  单帧 JSON 上限
 * @note   取值 64 * 1024。
 */
constexpr size_t kMaxPayload = 64 * 1024;
/**
 * @brief  length 字段允许的最大值
 * @note   取值 kMaxPayload + 1。
 */
constexpr size_t kMaxFrameSize = kMaxPayload + 1;

/**
 * @brief   操作码。0x0x 为客户端上行，0x8x 为服务端下行。
 * @author  JaTin
 */
enum class OpCode_EN : uint8_t {
    /**
     * @brief  ---- 客户端 -> 服务端 ----。
     * @details {"name":"alice","password":"123456","age":23,"desc":"..."}。
     */
    Login         = 0x01,
                            /**
                             * @brief  password 可选：带了就按账号密码校验，不带则按昵称匿名登录 （服务端可用 requireAuth 强制要求密码）；age/desc 可选。
                             * @details {"room":"1001"}。
                             */
    JoinRoom      = 0x02,
    /**
     * @brief  {}
     */
    LeaveRoom     = 0x03,
    /**
     * @brief  {"text":"hello"}
     */
    Chat          = 0x04,
    /**
     * @brief  {}
     */
    ListRooms     = 0x05,
    /**
     * @brief  {}
     */
    Ping          = 0x06,
    /**
     * @brief  {"name":"alice","age":24,"desc":"..."}
     */
    UpdateProfile = 0x07,
    /**
     * @brief  {"username":"bob","password":"secret","nickname":"Bob","age":28,"desc":"..."}
     */
    Register      = 0x08,
                            /**
                             * @brief  nickname/age/desc 可选，nickname 缺省时用 username。
                             * @details {"durationMs":3200,"opus":"<base64 DPV1 容器>"}  语音消息。
                             */
    Voice         = 0x09,

    /**
     * @brief  ---- 服务端 -> 客户端 ----。
     * @details {"ok":true,"userId":1,"name":"alice","age":23,"desc":"","serverTime":...}。
     */
    LoginResp = 0x81,
    /**
     * @brief  {"room":"1001","created":false,"members":[{"id":1,"name":"alice"}]}
     */
    RoomState = 0x82,
    /**
     * @brief  {"seq":12,"from":1,"name":"alice","room":"1001","text":"hi","ts":...}
     */
    ChatPush  = 0x83,
    /**
     * @brief  {"event":"join|leave|left|welcome|rename","room":"1001","text":"...","ts":...}
     */
    SystemMsg = 0x84,
    /**
     * @brief  {"code":"bad_payload","text":"..."}
     */
    ErrorMsg  = 0x85,
    /**
     * @brief  {"ts":...}
     */
    Pong      = 0x86,
    /**
     * @brief  {"rooms":[{"id":"1001","count":2}]}
     */
    RoomList  = 0x87,
    /**
     * @brief  {"ok":true,"userId":1,"name":..,"age":..,"desc":..}
     */
    ProfileResp = 0x88,
    /**
     * @brief  {"room":"1001","messages":[{"seq":12,"kind":"text","from":1,"name":"alice","text":"hi","ts":..}]}
     */
    HistoryResp = 0x89,
                            /**
                             * @brief  语音条目：{"seq":31,"kind":"voice","durationMs":3200,"opus":"<base64>",...} 注意：一条语音就占 30~55 KB，所以服务端会把语音条目**单独成帧**发送， 客户端可能连续收到多个 HistoryResp，按 seq 合并即可。
                             * @details {"seq":31,"from":7,"name":"bob","room":"1001","durationMs":3200,"opus":"<base64>","ts":...}。
                             */
    VoicePush = 0x8A,
    /**
     * @brief  {"ok":true,"userId":5,"username":"bob","nickname":"Bob"}
     */
    RegisterResp = 0x8B,
};

/**
 * @brief  语音消息的约束（服务端校验、客户端录音上限共用）。 码率 12 kbps 单声道：20 秒约 30 KB，Base64 后约 40 KB， 加上 JSON 其他字段仍远低于单帧 64 KiB 上限。
 * @details 单条语音最长 20 秒。
 */
constexpr uint32_t kMaxVoiceDurationMs = 20000;
/**
 * @brief  解码后的 DPV1 容器上限
 * @note   取值 32 * 1024。
 */
constexpr size_t   kMaxVoiceBytes      = 32 * 1024;

/**
 * @brief      获取操作码 code 名称。
 * @param[in] op             协议操作码；类型 OpCode_EN。
 * @return     成功返回对象指针；未找到或失败时返回 nullptr。
 * @author     JaTin
 */
const char* opCodeName(OpCode_EN op);

/**
 * @brief   协议帧S。
 * @author  JaTin
 */
struct Frame_S {
    /**
     * @brief  协议操作码。 初值 = OpCode_EN::ErrorMsg。
     * @note   类型 OpCode_EN；类/结构体成员变量。
     */
    OpCode_EN      op = OpCode_EN::ErrorMsg;
    /**
     * @brief  JSON 文本，可能为空串
     * @note   类型 std::string；类/结构体成员变量。
     */
    std::string payload;
};

/**
 * @brief      组帧。payload 超过 kMaxPayload 时抛 std::length_error。
 * @param[in] op             协议操作码；类型 OpCode_EN。
 * @param[in] payload        协议载荷内容；类型 const std::string&。
 * @return     返回构造好的字符串；无内容时返回空字符串。
 * @author     JaTin
 */
std::string encodeFrame(OpCode_EN op, const std::string& payload);
/**
 * @brief      编码协议帧。
 * @param[in] op             协议操作码；类型 OpCode_EN。
 * @param[in] payload        协议载荷内容；类型 const Json&。
 * @return     返回构造好的字符串；无内容时返回空字符串。
 * @author     JaTin
 */
std::string encodeFrame(OpCode_EN op, const Json& payload);

/**
 * @brief   增量解帧器：按字节流喂入，逐帧取出，天然处理粘包与半包。
 * @author  JaTin
 */
class CFrameDecoder {
public:
    enum Result_EN { Fatal = -1, NeedMore = 0, Ok = 1 };

    /**
     * @brief      取出一个完整帧。Ok=已取出；NeedMore=数据不足；Fatal=协议非法（应断开）。
     * @param[out] out            输入参数；类型 Frame_S&。
     * @return     返回 Result_EN 类型的处理结果。
     * @author     JaTin
     */
    Result_EN next(Frame_S& out);

    /**
     * @brief      追加 append。
     * @param[in] data           原始字节数据；类型 const char*。
     * @param[in] len            输入参数；类型 size_t。
     * @return     无返回值；处理结果通过成员状态或信号对外体现。
     * @author     JaTin
     */
    void   append(const char* data, size_t len);
    size_t buffered() const { return m_strBuf.size() - m_uReadPos; }

    const std::string& lastError() const { return m_strErr; }

private:
    /**
     * @brief  缓冲区。
     * @note   类型 std::string；类/结构体成员变量。
     */
    std::string m_strBuf;
    /**
     * @brief  ReadPos。 初值 = 0。
     * @note   类型 size_t；类/结构体成员变量。
     */
    size_t      m_uReadPos = 0;
    /**
     * @brief  Err。
     * @note   类型 std::string；类/结构体成员变量。
     */
    std::string m_strErr;
};

/**
 * @brief  namespace dp。
 */
}
