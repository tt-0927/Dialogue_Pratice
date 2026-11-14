/*
 * 文件名:    RoomManager.cpp
 * 创建日期:  2026-11-13
 * 作者:      JaTin
 * 功能描述:  CRoomManager 实现文件，包含相关接口的具体实现。
 * 修改记录:  2026-11-13 JaTin 按 C/C++ 编码规范整改注释与命名。
 */

/* server/CRoomManager.cpp */
#include "server/core/RoomManager.h"

namespace dp {

/*
 * 功能描述:  获取orcreate。
 * 作者:      JaTin
 * param [in]   strId            编号，类型 const std::string&。
 * return:    返回 CRoom& 类型的处理结果，具体含义见功能描述。
 */
CRoom& CRoomManager::getOrCreate(const std::string& strId) {
    auto stIt = rooms_.find(strId);
    if (stIt != rooms_.end()) return *stIt->second;

    auto stRoom = std::make_unique<CRoom>(strId);
    CRoom* pRaw = stRoom.get();
    rooms_.emplace(strId, std::move(stRoom));
    return *pRaw;
}

/*
 * 功能描述:  查找find。
 * 作者:      JaTin
 * param [in]   strId            编号，类型 const std::string&。
 * return:    成功返回对象指针；未找到或失败时返回 nullptr。
 */
CRoom* CRoomManager::find(const std::string& strId) {
    auto stIt = rooms_.find(strId);
    return stIt == rooms_.end() ? nullptr : stIt->second.get();
}

/*
 * 功能描述:  移除ifempty；失败时返回 false。
 * 作者:      JaTin
 * param [in]   strId            编号，类型 const std::string&。
 * return:    成功返回 true；参数非法或执行失败时返回 false。
 */
bool CRoomManager::removeIfEmpty(const std::string& strId) {
    auto stIt = rooms_.find(strId);
    if (stIt == rooms_.end()) return false;
    if (!stIt->second->empty()) return false;
    rooms_.erase(stIt);
    return true;
}

/* namespace dp */
}
