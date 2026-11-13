/*
 * 文件名:    ChatService.h
 * 创建日期:  2026-11-13
 * 作者:      JaTin
 * 功能描述:  文本消息业务：校验、分配序号、落历史、广播。
 * 修改记录:  2026-11-13 JaTin 按 C/C++ 编码规范整改注释与命名。
 */

#pragma once

#include "server/core/Service.h"

namespace dp {

/**
 * @brief 处理 `Chat` 文本消息。
 *
 * @details 固定顺序是「先落历史、再广播」——这样后进房的人一定能看到这条消息，
 *          不会出现「已经广播出去但历史里没有」的窗口。
 *
 *          广播时**不排除发送者本人**：客户端依赖服务端回显来渲染自己的气泡，
 *          从而保证消息顺序与服务端完全一致，不需要本地插入。
 */
class ChatService final : public CService {
public:
    using CService::CService;

    /**
     * @brief 声明负责的 opcode。
     * @retval 只含 `Chat` 的单元素列表。
     */
    std::vector<OpCode_EN> opcodes() const override;

    /**
     * @brief 处理文本消息。
     * @param session 发送者会话，必须已登录且在房间内。
     * @param op      固定为 `Chat`。
     * @param payload 负载：`{"text":"..."}`，正文 1..4000 字节。
     */
    void handle(CSession& session, OpCode_EN op, const Json& payload) override;
};

/* namespace dp */
}
