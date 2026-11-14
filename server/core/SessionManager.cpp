/**
 * @file       SessionManager.cpp
 * @brief      CSessionManager 实现文件，包含相关接口的具体实现。
 * @author     JaTin
 * @date       2026-11-13
 * @note       修改记录：2026-11-14 JaTin 统一为 Doxygen 风格注释。
 */

/**
 * @brief  server/CSessionManager.cpp。
 */
#include "server/core/SessionManager.h"

#include <utility>

namespace dp {

/**
 * @brief      添加 add。
 * @param[in] hFd            文件描述符；类型 int。
 * @param[in] strPeer        输入数据；类型 std::string。
 * @return     成功返回对象指针；未找到或失败时返回 nullptr。
 * @author     JaTin
 */
CSession* CSessionManager::add(int hFd, std::string strPeer) {
    const uint64_t uId = m_uNextId++;
    auto stSession = std::make_unique<CSession>(hFd, uId, std::move(strPeer));
    CSession* pRaw = stSession.get();
    m_uStore.emplace(uId, std::move(stSession));
    return pRaw;
}

/**
 * @brief      查找 find。
 * @param[in] uId            编号；类型 uint64_t。
 * @return     成功返回对象指针；未找到或失败时返回 nullptr。
 * @author     JaTin
 */
CSession* CSessionManager::find(uint64_t uId) {
    auto stIt = m_uStore.find(uId);
    return stIt == m_uStore.end() ? nullptr : stIt->second.get();
}

/**
 * @brief      移除 remove。
 * @param[in] uId            编号；类型 uint64_t。
 * @return     成功返回 true；参数非法或执行失败时返回 false。
 * @author     JaTin
 */
bool CSessionManager::remove(uint64_t uId) {
    return m_uStore.erase(uId) > 0;
}

/**
 * @brief  namespace dp。
 */
}
