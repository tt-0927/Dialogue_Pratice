/**
 * @file       ServerContext.cpp
 * @brief      CServerContext 的实现：把出站请求转交给事件循环。
 * @author     JaTin
 * @date       2026-11-13
 * @note       修改记录：2026-11-14 JaTin 统一为 Doxygen 风格注释。
 */

#include "server/core/ServerContext.h"

#include <utility>

#include "server/core/Reactor.h"

namespace dp {

/**
 * @brief      构造 CServerContext 对象，完成成员初始化与依赖注入。
 * @note       本函数无输入参数。
 * @return     构造/析构函数无返回值。
 * @author     JaTin
 */
CServerContext::CServerContext(ServerConfig_S cfg) : m_stCfg(std::move(cfg)) {}

CServerContext::~CServerContext() = default;

/**
 * @brief      发送 to；实现上向目标会话或整个房间下发数据。
 * @param[out] stSession      目标会话对象；类型 CSession&。
 * @param[in] enOp           协议操作码；类型 OpCode_EN。
 * @param[in] stPayload      协议载荷内容；类型 const Json&。
 * @return     无返回值；处理结果通过成员状态或信号对外体现。
 * @author     JaTin
 */
void CServerContext::sendTo(CSession& stSession, OpCode_EN enOp, const Json& stPayload) {
    if (reactor_ == nullptr) return;
    reactor_->sendTo(stSession, enOp, stPayload);
}

/**
 * @brief      发送错误。
 * @param[out] stSession      目标会话对象；类型 CSession&。
 * @param[in] strCode        输入数据；类型 const std::string&。
 * @param[in] strText        消息文本；类型 const std::string&。
 * @return     无返回值；处理结果通过成员状态或信号对外体现。
 * @author     JaTin
 */
void CServerContext::sendError(CSession& stSession, const std::string& strCode,
                              const std::string& strText) {
    if (reactor_ == nullptr) return;
    reactor_->sendError(stSession, strCode, strText);
}

/**
 * @brief      广播房间。
 * @param[in] strRoomId      输入数据；类型 const std::string&。
 * @param[in] enOp           协议操作码；类型 OpCode_EN。
 * @param[in] stPayload      协议载荷内容；类型 const Json&。
 * @param[in] uExceptId      输入数据；类型 uint64_t。
 * @return     无返回值；处理结果通过成员状态或信号对外体现。
 * @author     JaTin
 */
void CServerContext::broadcastRoom(const std::string& strRoomId, OpCode_EN enOp, const Json& stPayload,
                                  uint64_t uExceptId) {
    if (reactor_ == nullptr) return;
    reactor_->broadcastRoom(strRoomId, enOp, stPayload, uExceptId);
}

/**
 * @brief      请求 close。
 * @param[out] stSession      目标会话对象；类型 CSession&。
 * @param[in] strReason      失败原因描述；类型 const std::string&。
 * @return     无返回值；处理结果通过成员状态或信号对外体现。
 * @author     JaTin
 */
void CServerContext::requestClose(CSession& stSession, const std::string& strReason) {
    if (reactor_ == nullptr) return;
    reactor_->requestClose(stSession, strReason);
}

/**
 * @brief  namespace dp。
 */
}
