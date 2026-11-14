/**
 * @file       ChatService.h
 * @brief      文本消息业务：校验、分配序号、落历史、广播。
 * @author     JaTin
 * @date       2026-11-13
 * @note       修改记录：2026-11-14 JaTin 统一为 Doxygen 风格注释。
 */

#pragma once

#include "server/core/Service.h"

namespace dp {

/**
 * @brief   处理 `Chat` 文本消息。
 * @details 固定顺序是「先落历史、再广播」——这样后进房的人一定能看到这条消息， 不会出现「已经广播出去但历史里没有」的窗口。 广播时**不排除发送者本人**：客户端依赖服务端回显来渲染自己的气泡， 从而保证消息顺序与服务端完全一致，不需要本地插入。
 * @author  JaTin
 */
class ChatService final : public CService {
public:
    using CService::CService;

    /**
     * @brief      声明负责的 opcode。
     * @note       本函数无输入参数。
     * @return     只含 `Chat` 的单元素列表。
     * @author     JaTin
     */
    std::vector<OpCode_EN> opcodes() const override;

    /**
     * @brief      处理文本消息。
     * @param[out] session        发送者会话，必须已登录且在房间内；类型 CSession&。
     * @param[in] op             固定为 `Chat`；类型 OpCode_EN。
     * @param[in] payload        负载：`{"text":"..."}`，正文 1..4000 字节；类型 const Json&。
     * @return     无返回值；处理结果通过成员状态或信号对外体现。
     * @author     JaTin
     */
    void handle(CSession& session, OpCode_EN op, const Json& payload) override;
};

/**
 * @brief  namespace dp。
 */
}
