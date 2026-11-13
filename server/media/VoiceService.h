/**
 * @file  VoiceService.h
 * @brief 媒体层：语音消息的校验、存储与转发。
 * @details 服务端不解析音频内容，只当作不透明字节流搬运。这是刻意设计——
 *          Opus 解码需要重采样与编解码器状态，放在服务端既耗 CPU 又毫无必要，
 *          客户端本地解码即可。将来引入实时音视频（RTC）时，
 *          本类是同一条链路上的扩展点。
 */

#pragma once

#include "server/core/Service.h"

namespace dp {

/**
 * @brief 处理 `Voice` 语音消息。
 *
 * @details 校验三件事：时长上限、Base64 合法性、解码后容器大小。
 *          通过后与文本消息一致地「先落历史、再广播、含发送者回显」。
 *
 *          转发时直接复用请求里的 Base64 字符串，省掉一次「解码再编码」，
 *          大房间高并发下这点开销并不小。
 */
class VoiceService final : public Service {
public:
    using Service::Service;

    /**
     * @brief 声明负责的 opcode。
     * @retval 只含 `Voice` 的单元素列表。
     */
    std::vector<OpCode> opcodes() const override;

    /**
     * @brief 处理语音消息。
     * @param session 发送者会话，必须已登录且在房间内。
     * @param op      固定为 `Voice`。
     * @param payload 负载：`{"durationMs":3200,"opus":"<base64 DPV1>"}`。
     */
    void handle(Session& session, OpCode op, const Json& payload) override;
};

}  // namespace dp
