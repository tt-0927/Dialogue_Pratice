/**
 * @file       VoiceService.h
 * @brief      媒体层：语音消息的校验、存储与转发。
 * @author     JaTin
 * @date       2026-11-13
 * @note       修改记录：2026-11-14 JaTin 统一为 Doxygen 风格注释。
 */

#pragma once

#include "server/core/Service.h"

namespace dp {

/**
 * @brief   处理 `Voice` 语音消息。
 * @details 校验三件事：时长上限、Base64 合法性、解码后容器大小。 通过后与文本消息一致地「先落历史、再广播、含发送者回显」。 转发时直接复用请求里的 Base64 字符串，省掉一次「解码再编码」， 大房间高并发下这点开销并不小。
 * @author  JaTin
 */
class CVoiceService final : public CService {
public:
    using CService::CService;

    /**
     * @brief      声明负责的 opcode。
     * @note       本函数无输入参数。
     * @return     只含 `Voice` 的单元素列表。
     * @author     JaTin
     */
    std::vector<OpCode_EN> opcodes() const override;

    /**
     * @brief      处理语音消息。
     * @param[out] session        发送者会话，必须已登录且在房间内；类型 CSession&。
     * @param[in] op             固定为 `Voice`；类型 OpCode_EN。
     * @param[in] payload        负载：`{"durationMs":3200,"opus":"<base64 DPV1>"}`；类型 const Json&。
     * @return     无返回值；处理结果通过成员状态或信号对外体现。
     * @author     JaTin
     */
    void handle(CSession& session, OpCode_EN op, const Json& payload) override;
};

/**
 * @brief  namespace dp。
 */
}
