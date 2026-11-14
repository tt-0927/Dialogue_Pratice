/*
 * 文件名:    SessionManager.cpp
 * 创建日期:  2026-11-13
 * 作者:      JaTin
 * 功能描述:  CSessionManager 实现文件，包含相关接口的具体实现。
 * 修改记录:  2026-11-13 JaTin 按 C/C++ 编码规范整改注释与命名。
 */

/* server/CSessionManager.cpp */
#include "server/SessionManager.h"

#include <utility>

namespace dp {

/*
 * 功能描述:  添加add。
 * 作者:      JaTin
 * param [in]   hFd              文件描述符，类型 int。
 * param [in]   strPeer          输入数据，类型 std::string。
 * return:    成功返回对象指针；未找到或失败时返回 nullptr。
 */
CSession* CSessionManager::add(int hFd, std::string strPeer) {
    const uint64_t uId = m_uNextId++;
    auto stSession = std::make_unique<CSession>(hFd, uId, std::move(strPeer));
    CSession* pRaw = stSession.get();
    m_uStore.emplace(uId, std::move(stSession));
    return pRaw;
}

/*
 * 功能描述:  查找find。
 * 作者:      JaTin
 * param [in]   uId              编号，类型 uint64_t。
 * return:    成功返回对象指针；未找到或失败时返回 nullptr。
 */
CSession* CSessionManager::find(uint64_t uId) {
    auto stIt = m_uStore.find(uId);
    return stIt == m_uStore.end() ? nullptr : stIt->second.get();
}

/*
 * 功能描述:  移除remove。
 * 作者:      JaTin
 * param [in]   uId              编号，类型 uint64_t。
 * return:    成功返回 true；参数非法或执行失败时返回 false。
 */
bool CSessionManager::remove(uint64_t uId) {
    return m_uStore.erase(uId) > 0;
}

/* namespace dp */
}
