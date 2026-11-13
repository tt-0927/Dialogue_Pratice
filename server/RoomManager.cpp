/*
 * 文件名:    RoomManager.cpp
 * 创建日期:  2026-11-13
 * 作者:      JaTin
 * 功能描述:  CRoomManager 实现文件，包含相关接口的具体实现。
 * 修改记录:  2026-11-13 JaTin 按 C/C++ 编码规范整改注释与命名。
 */

/* server/CRoomManager.cpp */
#include "server/RoomManager.h"

namespace dp {

/*
 * 功能描述:  执行 getOrCreate 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [in]  id  参数。
 * return:    返回值，含义见功能描述。
 */
CRoom& CRoomManager::getOrCreate(const std::string& id) {
    auto it = rooms_.find(id);
    if (it != rooms_.end()) return *it->second;

    auto room = std::make_unique<CRoom>(id);
    CRoom* raw = room.get();
    rooms_.emplace(id, std::move(room));
    return *raw;
}

/*
 * 功能描述:  执行 find 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [in]  id  参数。
 * return:    返回值，含义见功能描述。
 */
CRoom* CRoomManager::find(const std::string& id) {
    auto it = rooms_.find(id);
    return it == rooms_.end() ? nullptr : it->second.get();
}

/*
 * 功能描述:  执行 removeIfEmpty 相关的处理，完成参数校验、核心逻辑与结果返回。
 * 作者:      JaTin
 * param [in]  id  参数。
 * return:    返回值，含义见功能描述。
 */
bool CRoomManager::removeIfEmpty(const std::string& id) {
    auto it = rooms_.find(id);
    if (it == rooms_.end()) return false;
    if (!it->second->empty()) return false;
    rooms_.erase(it);
    return true;
}

/* namespace dp */
}
