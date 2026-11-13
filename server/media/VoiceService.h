/*
 * 文件名:    VoiceService.h
 * 创建日期:  2026-11-13
 * 作者:      JaTin
 * 功能描述:  媒体层：语音消息的校验、存储与转发。
 * 修改记录:  2026-11-13 JaTin 按 C/C++ 编码规范整改注释与命名。
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
class CVoiceService final : public CService {
public:
    using CService::CService;

    /**
     * @brief 声明负责的 opcode。
     * @retval 只含 `Voice` 的单元素列表。
     */
    std::vector<OpCode_EN> opcodes() const override;

    /**
     * @brief 处理语音消息。
     * @param session 发送者会话，必须已登录且在房间内。
     * @param op      固定为 `Voice`。
     * @param payload 负载：`{"durationMs":3200,"opus":"<base64 DPV1>"}`。
     */
    void handle(CSession& session, OpCode_EN op, const Json& payload) override;
};

/* namespace dp */
}
