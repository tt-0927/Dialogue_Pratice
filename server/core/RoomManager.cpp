/**
 * @file       RoomManager.cpp
 * @brief      CRoomManager 实现文件，包含相关接口的具体实现。
 * @author     JaTin
 * @date       2026-11-13
 * @note       修改记录：2026-11-14 JaTin 统一为 Doxygen 风格注释。
 */

/**
 * @brief  server/CRoomManager.cpp。
 */
#include "server/core/RoomManager.h"

namespace dp {

/**
 * @brief      获取 orcreate相关的数据并返回处理结果。
 * @param[in] strId          编号；类型 const std::string&。
 * @return     返回 CRoom& CRoomManager:: 类型的对象引用或指针，调用方按接口约定使用。
 * @author     JaTin
 */
CRoom& CRoomManager::getOrCreate(const std::string& strId) {
    auto stIt = rooms_.find(strId);
    if (stIt != rooms_.end()) return *stIt->second;

    auto stRoom = std::make_unique<CRoom>(strId);
    CRoom* pRaw = stRoom.get();
    rooms_.emplace(strId, std::move(stRoom));
    return *pRaw;
}

/**
 * @brief      查找 find。
 * @param[in] strId          编号；类型 const std::string&。
 * @return     成功返回对象指针；未找到或失败时返回 nullptr。
 * @author     JaTin
 */
CRoom* CRoomManager::find(const std::string& strId) {
    auto stIt = rooms_.find(strId);
    return stIt == rooms_.end() ? nullptr : stIt->second.get();
}

/**
 * @brief      移除 ifempty；失败时返回 false。
 * @param[in] strId          编号；类型 const std::string&。
 * @return     成功返回 true；参数非法或执行失败时返回 false。
 * @author     JaTin
 */
bool CRoomManager::removeIfEmpty(const std::string& strId) {
    auto stIt = rooms_.find(strId);
    if (stIt == rooms_.end()) return false;
    if (!stIt->second->empty()) return false;
    rooms_.erase(stIt);
    return true;
}

/**
 * @brief  namespace dp。
 */
}
