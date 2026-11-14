/*
 * 文件名:    ServerContext.cpp
 * 创建日期:  2026-11-13
 * 作者:      JaTin
 * 功能描述:  CServerContext 的实现：把出站请求转交给事件循环。
 * 修改记录:  2026-11-13 JaTin 按 C/C++ 编码规范整改注释与命名。
 */

#include "server/core/ServerContext.h"

#include <utility>

#include "server/core/Reactor.h"

namespace dp {

/*
 * 功能描述:  构造 CServerContext 对象，完成成员初始化与依赖注入。
 * 作者:      JaTin
 * return:    无返回值（构造函数或析构函数）。
 */
CServerContext::CServerContext(ServerConfig_S cfg) : m_stCfg(std::move(cfg)) {}

CServerContext::~CServerContext() = default;

/*
 * 功能描述:  发送to；实现上向目标会话或整个房间下发数据。
 * 作者:      JaTin
 * param [out]  stSession        目标会话对象，类型 CSession&。
 * param [in]   enOp             协议操作码，类型 OpCode_EN。
 * param [in]   stPayload        协议载荷内容，类型 const Json&。
 * return:    无返回值；处理结果通过成员状态或信号对外体现。
 */
void CServerContext::sendTo(CSession& stSession, OpCode_EN enOp, const Json& stPayload) {
    if (reactor_ == nullptr) return;
    reactor_->sendTo(stSession, enOp, stPayload);
}

/*
 * 功能描述:  发送错误。
 * 作者:      JaTin
 * param [out]  stSession        目标会话对象，类型 CSession&。
 * param [in]   strCode          输入数据，类型 const std::string&。
 * param [in]   strText          消息文本，类型 const std::string&。
 * return:    无返回值；处理结果通过成员状态或信号对外体现。
 */
void CServerContext::sendError(CSession& stSession, const std::string& strCode,
                              const std::string& strText) {
    if (reactor_ == nullptr) return;
    reactor_->sendError(stSession, strCode, strText);
}

/*
 * 功能描述:  广播房间。
 * 作者:      JaTin
 * param [in]   strRoomId        输入数据，类型 const std::string&。
 * param [in]   enOp             协议操作码，类型 OpCode_EN。
 * param [in]   stPayload        协议载荷内容，类型 const Json&。
 * param [in]   uExceptId        输入数据，类型 uint64_t。
 * return:    无返回值；处理结果通过成员状态或信号对外体现。
 */
void CServerContext::broadcastRoom(const std::string& strRoomId, OpCode_EN enOp, const Json& stPayload,
                                  uint64_t uExceptId) {
    if (reactor_ == nullptr) return;
    reactor_->broadcastRoom(strRoomId, enOp, stPayload, uExceptId);
}

/*
 * 功能描述:  请求close。
 * 作者:      JaTin
 * param [out]  stSession        目标会话对象，类型 CSession&。
 * param [in]   strReason        失败原因描述，类型 const std::string&。
 * return:    无返回值；处理结果通过成员状态或信号对外体现。
 */
void CServerContext::requestClose(CSession& stSession, const std::string& strReason) {
    if (reactor_ == nullptr) return;
    reactor_->requestClose(stSession, strReason);
}

/* namespace dp */
}
